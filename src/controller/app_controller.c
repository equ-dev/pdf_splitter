#include "app_controller.h"

#include "../model/pdfdoc.h"

#include <stdlib.h>

#define THUMBNAIL_WIDTH_PX 140

/* How often (in pages) to drain the event loop while populating
 * thumbnails. Draining every single page forces GTK to process the
 * flowbox's pending resize/relayout on every append, and that relayout
 * cost scales with however many children are already in the box -
 * pumping once per page turns an O(n) total layout cost into an O(n^2)
 * one over the whole load. Batching the drain trades a bit of animation
 * smoothness (the progress bar updates in steps instead of continuously)
 * for a much smaller number of full layout passes. */
#define PUMP_INTERVAL_PAGES 25

typedef struct _AppController AppController;

typedef struct {
  int first_page;    /* 0-indexed */
  int last_page;     /* 0-indexed, inclusive */
  int accent_index;  /* color slot, mod CHAPTER_ACCENT_COUNT */
  gchar *title;       /* user-editable; defaults to "Chapter N" at creation */
  AppController *owner; /* back-pointer so the view-layer row callbacks
                          * (which only carry a ChapterRange*) can reach
                          * the controller without a second lookup. */
} ChapterRange;

static void
chapter_range_free (gpointer data)
{
  ChapterRange *r = data;
  g_free (r->title);
  g_free (r);
}

struct _AppController {
  MainWindow *mw;
  PdfDoc *doc;
  GPtrArray *chapters; /* owns ChapterRange* elements */
  int selection_anchor; /* index of the last plain (non-shift) click, or -1 */
  int next_accent_index; /* monotonically increasing per document, so a
                           * chapter's color never gets silently reassigned
                           * to a different chapter after a removal. */
};

static void refresh_thumbnail_accents (AppController *controller);
static void update_unassigned_summary (AppController *controller);
static void rebuild_chapter_sidebar (AppController *controller);
static void on_row_remove_clicked (gpointer user_data);
static void on_row_rename_changed (const char *new_title, gpointer user_data);

static int
compare_chapter_start (gconstpointer a, gconstpointer b)
{
  const ChapterRange *ra = *(ChapterRange * const *) a;
  const ChapterRange *rb = *(ChapterRange * const *) b;
  return ra->first_page - rb->first_page;
}

/* Returns a GPtrArray of the same ChapterRange pointers as
 * controller->chapters, sorted by first_page. Caller must
 * g_ptr_array_free(result, TRUE) - this is a shallow index, it does not
 * own or free the ChapterRange elements themselves. */
static GPtrArray *
chapters_sorted_by_start (AppController *controller)
{
  GPtrArray *sorted = g_ptr_array_sized_new (controller->chapters->len);
  for (guint i = 0; i < controller->chapters->len; i++)
    g_ptr_array_add (sorted, g_ptr_array_index (controller->chapters, i));
  g_ptr_array_sort (sorted, compare_chapter_start);
  return sorted;
}

static void
populate_thumbnails (AppController *controller, GtkWidget *loading)
{
  gtk_flow_box_remove_all (GTK_FLOW_BOX (controller->mw->thumbnail_flowbox));

  int n_pages = pdfdoc_get_page_count (controller->doc);

  for (int i = 0; i < n_pages; i++) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = pdfdoc_render_page_thumbnail (controller->doc, i, THUMBNAIL_WIDTH_PX, &error);

    if (pixbuf == NULL) {
      g_warning ("failed to render page %d: %s", i, error->message);
      g_error_free (error);
      continue;
    }

    GdkTexture *texture = gdk_texture_new_for_pixbuf (pixbuf);
    g_object_unref (pixbuf);

    GtkWidget *card = mainwindow_new_thumbnail_card (texture, i + 1);
    g_object_unref (texture); /* GtkPicture holds its own reference. */

    gtk_flow_box_append (GTK_FLOW_BOX (controller->mw->thumbnail_flowbox), card);

    if (n_pages > 0)
      mainwindow_set_loading_progress (loading, (double) (i + 1) / (double) n_pages);

    /* Rendering every page synchronously can take a noticeable moment on
     * long textbooks, so we still need to drain the event loop
     * periodically - otherwise the window looks frozen and the loading
     * spinner/progress bar never animate. But draining on *every* page
     * forces a full flowbox relayout per page, so we batch it instead. */
    if (i % PUMP_INTERVAL_PAGES == 0 || i == n_pages - 1) {
      while (g_main_context_iteration (NULL, FALSE)) { }
    }
  }
}

/* Which chapter (if any) a 0-indexed page belongs to, as an accent index
 * already reduced mod CHAPTER_ACCENT_COUNT. Returns -1 if unassigned. */
static int
accent_for_page (AppController *controller, int page_index)
{
  for (guint i = 0; i < controller->chapters->len; i++) {
    ChapterRange *r = g_ptr_array_index (controller->chapters, i);
    if (page_index >= r->first_page && page_index <= r->last_page)
      return r->accent_index % CHAPTER_ACCENT_COUNT;
  }
  return -1;
}

/* Recolors every thumbnail card's accent stripe to match the chapter (if
 * any) it currently belongs to. Cheap: this only flips CSS classes on
 * already-built widgets, it doesn't re-render or rebuild anything. */
static void
refresh_thumbnail_accents (AppController *controller)
{
  if (controller->doc == NULL)
    return;

  int n_pages = pdfdoc_get_page_count (controller->doc);
  for (int i = 0; i < n_pages; i++) {
    GtkFlowBoxChild *child = gtk_flow_box_get_child_at_index (
        GTK_FLOW_BOX (controller->mw->thumbnail_flowbox), i);
    if (child == NULL)
      continue;

    GtkWidget *card = gtk_flow_box_child_get_child (child);
    mainwindow_set_thumbnail_accent (card, accent_for_page (controller, i));
  }
}

/* Rebuilds the "Not in a chapter: pages ..." summary under the chapter
 * list from scratch, coalescing consecutive unassigned pages into
 * ranges (e.g. "13-20" rather than "13, 14, 15, ..."). */
static void
update_unassigned_summary (AppController *controller)
{
  if (controller->doc == NULL) {
    gtk_label_set_text (GTK_LABEL (controller->mw->unassigned_label), "");
    return;
  }

  int n_pages = pdfdoc_get_page_count (controller->doc);
  GPtrArray *sorted = chapters_sorted_by_start (controller);

  GString *summary = g_string_new (NULL);
  int cursor = 0; /* next 0-indexed page not yet accounted for */

  for (guint i = 0; i < sorted->len; i++) {
    ChapterRange *r = g_ptr_array_index (sorted, i);

    if (r->first_page > cursor) {
      int gap_start_1 = cursor + 1;      /* 1-indexed, inclusive */
      int gap_end_1 = r->first_page;     /* 1-indexed, inclusive */
      if (summary->len > 0) g_string_append (summary, ", ");
      if (gap_start_1 == gap_end_1)
        g_string_append_printf (summary, "%d", gap_start_1);
      else
        g_string_append_printf (summary, "%d-%d", gap_start_1, gap_end_1);
    }
    cursor = MAX (cursor, r->last_page + 1);
  }

  if (cursor < n_pages) {
    int gap_start_1 = cursor + 1;
    int gap_end_1 = n_pages;
    if (summary->len > 0) g_string_append (summary, ", ");
    if (gap_start_1 == gap_end_1)
      g_string_append_printf (summary, "%d", gap_start_1);
    else
      g_string_append_printf (summary, "%d-%d", gap_start_1, gap_end_1);
  }

  g_ptr_array_free (sorted, TRUE);

  if (summary->len == 0)
    gtk_label_set_text (GTK_LABEL (controller->mw->unassigned_label), "Every page is assigned to a chapter.");
  else {
    char *text = g_strdup_printf ("Not in a chapter: pages %s", summary->str);
    gtk_label_set_text (GTK_LABEL (controller->mw->unassigned_label), text);
    g_free (text);
  }

  g_string_free (summary, TRUE);
}

static void
rebuild_chapter_sidebar (AppController *controller)
{
  gtk_list_box_remove_all (GTK_LIST_BOX (controller->mw->chapter_listbox));

  GPtrArray *sorted = chapters_sorted_by_start (controller);

  for (guint i = 0; i < sorted->len; i++) {
    ChapterRange *r = g_ptr_array_index (sorted, i);

    char range_text[64];
    g_snprintf (range_text, sizeof (range_text), "Pages %d-%d",
                r->first_page + 1, r->last_page + 1);

    GtkWidget *row = mainwindow_new_chapter_row (r->title, range_text, r->accent_index,
                                                  on_row_remove_clicked,
                                                  on_row_rename_changed,
                                                  r);
    gtk_list_box_append (GTK_LIST_BOX (controller->mw->chapter_listbox), row);
  }

  g_ptr_array_free (sorted, TRUE);
}

/* --- chapter row callbacks (fired from the view layer via the
 * ChapterRange* itself, so no separate index lookup is needed) --- */

static void
on_row_remove_clicked (gpointer user_data)
{
  ChapterRange *range = user_data;
  AppController *controller = range->owner;

  g_ptr_array_remove (controller->chapters, range); /* frees range via chapter_range_free */

  rebuild_chapter_sidebar (controller);
  refresh_thumbnail_accents (controller);
  update_unassigned_summary (controller);
}

static void
on_row_rename_changed (const char *new_title, gpointer user_data)
{
  ChapterRange *range = user_data;

  if (g_strcmp0 (range->title, new_title) == 0)
    return;

  g_free (range->title);
  range->title = g_strdup (new_title);
  /* The sidebar's GtkEditableLabel already reflects the edit live, and
   * the page range/colors are unaffected by a rename, so no rebuild is
   * needed here. */
}

static void
on_file_chooser_response (GtkNativeDialog *native, int response, gpointer user_data)
{
  AppController *controller = user_data;

  if (response == GTK_RESPONSE_ACCEPT) {
    GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (native));
    char *path = g_file_get_path (file);
    if (path != NULL) {
      app_controller_load_pdf (controller, path);
      g_free (path);
    }
    g_object_unref (file);
  }

  g_object_unref (native);
}

/*
 * GtkFlowBox's built-in Shift+click only toggles the two endpoints, it
 * doesn't fill the range between them (confirmed by manual testing, not
 * assumed). We install our own capture-phase click gesture on the flowbox
 * to implement a proper contiguous range-select: plain click records an
 * anchor and otherwise falls through to the default single-select
 * behavior; Shift+click fills every page between the anchor and the
 * clicked page and claims the event so the default handler doesn't also
 * run and clobber it.
 */
static void
on_flowbox_pressed (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
  (void) n_press;
  AppController *controller = user_data;
  GtkWidget *flowbox = controller->mw->thumbnail_flowbox;

  GtkFlowBoxChild *child = gtk_flow_box_get_child_at_pos (GTK_FLOW_BOX (flowbox), (int) x, (int) y);
  if (child == NULL)
    return; /* clicked empty space - let default handling (deselect) run. */

  int clicked_index = gtk_flow_box_child_get_index (child);
  GdkModifierType state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));

  if ((state & GDK_SHIFT_MASK) && controller->selection_anchor >= 0) {
    int lo = MIN (controller->selection_anchor, clicked_index);
    int hi = MAX (controller->selection_anchor, clicked_index);

    gtk_flow_box_unselect_all (GTK_FLOW_BOX (flowbox));
    for (int i = lo; i <= hi; i++) {
      GtkFlowBoxChild *c = gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (flowbox), i);
      if (c != NULL)
        gtk_flow_box_select_child (GTK_FLOW_BOX (flowbox), c);
    }

    /* Fully handled - claim the sequence so the default click-to-select
     * behavior doesn't also run afterwards and collapse our range back
     * down to a single child. */
    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
  } else if (!(state & GDK_SHIFT_MASK) && gtk_flow_box_child_is_selected (child)) {
    /* Plain click on an already-selected page: GTK's default click
     * behavior only ever re-selects (never toggles off), so clicking a
     * selected page again was a dead end with no way to clear it. Treat
     * this as "clear the selection" and start fresh. */
    gtk_flow_box_unselect_all (GTK_FLOW_BOX (flowbox));
    controller->selection_anchor = -1;
    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
  } else {
    controller->selection_anchor = clicked_index;
    /* Not shift-held, not re-clicking a selected page: leave the sequence
     * unclaimed so the default single-select-on-click behavior proceeds
     * normally. */
  }
}

static void
on_open_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  AppController *controller = user_data;

  GtkFileChooserNative *native = gtk_file_chooser_native_new (
      "Open PDF",
      GTK_WINDOW (controller->mw->window),
      GTK_FILE_CHOOSER_ACTION_OPEN,
      "_Open", "_Cancel");

  GtkFileFilter *filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "PDF files");
  gtk_file_filter_add_mime_type (filter, "application/pdf");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (native), filter);

  g_signal_connect (native, "response", G_CALLBACK (on_file_chooser_response), controller);
  gtk_native_dialog_show (GTK_NATIVE_DIALOG (native));
}

static void
on_add_chapter_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  AppController *controller = user_data;

  if (controller->doc == NULL)
    return;

  GList *selected = gtk_flow_box_get_selected_children (GTK_FLOW_BOX (controller->mw->thumbnail_flowbox));
  if (selected == NULL)
    return;

  int min_index = G_MAXINT;
  int max_index = -1;
  for (GList *l = selected; l != NULL; l = l->next) {
    int idx = gtk_flow_box_child_get_index (GTK_FLOW_BOX_CHILD (l->data));
    if (idx < min_index) min_index = idx;
    if (idx > max_index) max_index = idx;
  }
  g_list_free (selected);

  ChapterRange *range = g_new0 (ChapterRange, 1);
  range->first_page = min_index;
  range->last_page = max_index;
  range->accent_index = controller->next_accent_index++;
  range->owner = controller;
  range->title = g_strdup_printf ("Chapter %u", controller->chapters->len + 1);
  g_ptr_array_add (controller->chapters, range);

  rebuild_chapter_sidebar (controller);
  refresh_thumbnail_accents (controller);
  update_unassigned_summary (controller);

  /* Clear the grid selection so the next click starts a fresh range. */
  gtk_flow_box_unselect_all (GTK_FLOW_BOX (controller->mw->thumbnail_flowbox));
  controller->selection_anchor = -1;
}

/* Shared by both the "Go" button and pressing Enter in the page-jump
 * entry: parses the entry's text, and if it's a clean in-range page
 * number, selects that page and scrolls it into view. Silently ignores
 * anything else (empty, garbage, or out-of-range input) rather than
 * showing an error for what is a low-stakes convenience control. */
static void
jump_to_requested_page (AppController *controller)
{
  if (controller->doc == NULL)
    return;

  const char *text = gtk_editable_get_text (GTK_EDITABLE (controller->mw->page_jump_entry));
  if (text == NULL || *text == '\0')
    return;

  char *endptr = NULL;
  long page_number = strtol (text, &endptr, 10);
  if (endptr == text || *endptr != '\0')
    return; /* not a clean integer */

  int n_pages = pdfdoc_get_page_count (controller->doc);
  if (page_number < 1 || page_number > n_pages)
    return;

  int page_index = (int) page_number - 1;
  GtkFlowBoxChild *child = gtk_flow_box_get_child_at_index (
      GTK_FLOW_BOX (controller->mw->thumbnail_flowbox), page_index);
  if (child == NULL)
    return;

  gtk_flow_box_unselect_all (GTK_FLOW_BOX (controller->mw->thumbnail_flowbox));
  gtk_flow_box_select_child (GTK_FLOW_BOX (controller->mw->thumbnail_flowbox), child);
  controller->selection_anchor = page_index;

  /* GtkScrolledWindow's implicit viewport auto-scrolls to keep the
   * focused descendant visible, so grabbing focus here doubles as the
   * "scroll to page" behavior without any manual adjustment math. */
  gtk_widget_grab_focus (GTK_WIDGET (child));
}

static void
on_jump_button_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  jump_to_requested_page ((AppController *) user_data);
}

static void
on_jump_entry_activate (GtkEntry *entry, gpointer user_data)
{
  (void) entry;
  jump_to_requested_page ((AppController *) user_data);
}

AppController *
app_controller_new (MainWindow *mw)
{
  AppController *controller = g_new0 (AppController, 1);
  controller->mw = mw;
  controller->doc = NULL;
  controller->chapters = g_ptr_array_new_with_free_func (chapter_range_free);
  controller->selection_anchor = -1;
  controller->next_accent_index = 0;

  g_signal_connect (mw->open_button, "clicked", G_CALLBACK (on_open_clicked), controller);
  g_signal_connect (mw->add_chapter_button, "clicked", G_CALLBACK (on_add_chapter_clicked), controller);
  g_signal_connect (mw->page_jump_button, "clicked", G_CALLBACK (on_jump_button_clicked), controller);
  g_signal_connect (mw->page_jump_entry, "activate", G_CALLBACK (on_jump_entry_activate), controller);

  GtkGesture *range_click_gesture = gtk_gesture_click_new ();
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (range_click_gesture), GTK_PHASE_CAPTURE);
  g_signal_connect (range_click_gesture, "pressed", G_CALLBACK (on_flowbox_pressed), controller);
  gtk_widget_add_controller (mw->thumbnail_flowbox, GTK_EVENT_CONTROLLER (range_click_gesture));

  return controller;
}

gboolean
app_controller_load_pdf (AppController *controller, const char *path)
{
  GtkWidget *loading = mainwindow_show_loading_dialog (GTK_WINDOW (controller->mw->window));
  /* Pump the loop once so the loading window actually gets drawn before
   * we move on to the (synchronous) load+render work below. */
  while (g_main_context_iteration (NULL, FALSE)) { }

  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (path, &error);
  if (doc == NULL) {
    g_warning ("failed to load '%s': %s", path, error != NULL ? error->message : "unknown error");
    g_clear_error (&error);
    mainwindow_close_loading_dialog (loading);
    return FALSE;
  }

  if (controller->doc != NULL)
    pdfdoc_free (controller->doc);
  controller->doc = doc;

  /* A newly loaded document invalidates any chapters defined against the
   * previous one, and colors start fresh too. */
  g_ptr_array_set_size (controller->chapters, 0);
  controller->next_accent_index = 0;
  rebuild_chapter_sidebar (controller);

  populate_thumbnails (controller, loading);
  update_unassigned_summary (controller);

  mainwindow_close_loading_dialog (loading);

  return TRUE;
}

void
app_controller_free (AppController *controller)
{
  if (controller == NULL)
    return;
  if (controller->doc != NULL)
    pdfdoc_free (controller->doc);
  g_ptr_array_free (controller->chapters, TRUE);
  g_free (controller);
}
