#include "app_controller.h"

#include "../model/pdfdoc.h"

#define THUMBNAIL_WIDTH_PX 140

typedef struct {
  int first_page; /* 0-indexed */
  int last_page;  /* 0-indexed, inclusive */
} ChapterRange;

struct _AppController {
  MainWindow *mw;
  PdfDoc *doc;
  GPtrArray *chapters; /* owns ChapterRange* elements */
  int selection_anchor; /* index of the last plain (non-shift) click, or -1 */
};

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
     * long textbooks. Drain pending main-loop events each iteration so
     * the loading spinner/progress bar actually animate and the window
     * stays responsive instead of appearing frozen. */
    while (g_main_context_iteration (NULL, FALSE)) { }
  }
}

static void
rebuild_chapter_sidebar (AppController *controller)
{
  gtk_list_box_remove_all (GTK_LIST_BOX (controller->mw->chapter_listbox));

  for (guint i = 0; i < controller->chapters->len; i++) {
    ChapterRange *r = g_ptr_array_index (controller->chapters, i);

    char title[32];
    g_snprintf (title, sizeof (title), "Chapter %u", i + 1);

    char range_text[64];
    g_snprintf (range_text, sizeof (range_text), "Pages %d-%d",
                r->first_page + 1, r->last_page + 1);

    GtkWidget *row = mainwindow_new_chapter_row (title, range_text);
    gtk_list_box_append (GTK_LIST_BOX (controller->mw->chapter_listbox), row);
  }
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

  ChapterRange *range = g_new (ChapterRange, 1);
  range->first_page = min_index;
  range->last_page = max_index;
  g_ptr_array_add (controller->chapters, range);

  rebuild_chapter_sidebar (controller);

  /* Clear the grid selection so the next click starts a fresh range. */
  gtk_flow_box_unselect_all (GTK_FLOW_BOX (controller->mw->thumbnail_flowbox));
  controller->selection_anchor = -1;
}

static void
on_remove_chapter_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  AppController *controller = user_data;

  GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (controller->mw->chapter_listbox));
  if (row == NULL)
    return;

  int idx = gtk_list_box_row_get_index (row);
  if (idx < 0 || (guint) idx >= controller->chapters->len)
    return;

  g_ptr_array_remove_index (controller->chapters, idx);
  rebuild_chapter_sidebar (controller);
}

AppController *
app_controller_new (MainWindow *mw)
{
  AppController *controller = g_new0 (AppController, 1);
  controller->mw = mw;
  controller->doc = NULL;
  controller->chapters = g_ptr_array_new_with_free_func (g_free);
  controller->selection_anchor = -1;

  g_signal_connect (mw->open_button, "clicked", G_CALLBACK (on_open_clicked), controller);
  g_signal_connect (mw->add_chapter_button, "clicked", G_CALLBACK (on_add_chapter_clicked), controller);
  g_signal_connect (mw->remove_chapter_button, "clicked", G_CALLBACK (on_remove_chapter_clicked), controller);

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
   * previous one. */
  g_ptr_array_set_size (controller->chapters, 0);
  rebuild_chapter_sidebar (controller);

  populate_thumbnails (controller, loading);

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
