#include "mainwindow.h"

#define THUMB_PLACEHOLDER_WIDTH 140
#define THUMB_STRIPE_WIDTH 4

/* Closures let mainwindow_new_chapter_row accept plain function pointers
 * from the controller while still using normal GTK signal connections
 * under the hood. Each is freed automatically when its signal is
 * disconnected (e.g. when the row is destroyed on the next sidebar
 * rebuild), so callers don't need to track or free anything themselves. */

typedef struct {
  ChapterRemoveCallback callback;
  gpointer user_data;
} RemoveClosure;

static void
remove_closure_free (gpointer data, GClosure *closure)
{
  (void) closure;
  g_free (data);
}

static void
on_remove_button_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  RemoveClosure *closure = user_data;
  closure->callback (closure->user_data);
}

typedef struct {
  ChapterRenameCallback callback;
  gpointer user_data;
} RenameClosure;

static void
rename_closure_free (gpointer data, GClosure *closure)
{
  (void) closure;
  g_free (data);
}

static void
on_title_text_notify (GObject *object, GParamSpec *pspec, gpointer user_data)
{
  (void) pspec;
  RenameClosure *closure = user_data;
  closure->callback (gtk_editable_get_text (GTK_EDITABLE (object)), closure->user_data);
}

/* Loads the CSS classes used to color-code chapters: a saturated stripe
 * color for thumbnail cards (chapter-accent-N) and a paler tint for the
 * matching sidebar row (chapter-card-accent-N). Registered once on the
 * default display so both are available anywhere in the widget tree. */
static void
load_accent_css (void)
{
  static gboolean loaded = FALSE;
  if (loaded)
    return;
  loaded = TRUE;

  const char *css =
    ".thumb-stripe { min-width: " G_STRINGIFY (THUMB_STRIPE_WIDTH) "px; }"
    ".thumb-stripe-none { background-color: transparent; }"
    ".chapter-accent-0 { background-color: #B4472F; }"
    ".chapter-accent-1 { background-color: #2E5C4E; }"
    ".chapter-accent-2 { background-color: #3B4A78; }"
    ".chapter-accent-3 { background-color: #8A6A2A; }"
    ".chapter-accent-4 { background-color: #6B4472; }"
    ".chapter-card-accent-0 { background-color: alpha(#B4472F, 0.12); }"
    ".chapter-card-accent-1 { background-color: alpha(#2E5C4E, 0.12); }"
    ".chapter-card-accent-2 { background-color: alpha(#3B4A78, 0.12); }"
    ".chapter-card-accent-3 { background-color: alpha(#8A6A2A, 0.12); }"
    ".chapter-card-accent-4 { background-color: alpha(#6B4472, 0.12); }";

  GtkCssProvider *provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (provider, css);
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                               GTK_STYLE_PROVIDER (provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
}

static void
set_accent_class (GtkWidget *widget, const char *prefix, int accent_index)
{
  for (int i = 0; i < CHAPTER_ACCENT_COUNT; i++) {
    char klass[32];
    g_snprintf (klass, sizeof (klass), "%s-%d", prefix, i);
    gtk_widget_remove_css_class (widget, klass);
  }

  if (accent_index >= 0) {
    char klass[32];
    g_snprintf (klass, sizeof (klass), "%s-%d", prefix, accent_index % CHAPTER_ACCENT_COUNT);
    gtk_widget_add_css_class (widget, klass);
  }
}

GtkWidget *
mainwindow_new_chapter_row (const char *title,
                             const char *page_range,
                             int accent_index,
                             ChapterRemoveCallback on_remove,
                             ChapterRenameCallback on_rename,
                             gpointer user_data)
{
  GtkWidget *row = gtk_list_box_row_new ();
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);

  GtkWidget *card = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_add_css_class (card, "card");
  set_accent_class (card, "chapter-card-accent", accent_index);
  gtk_widget_set_margin_start (card, 4);
  gtk_widget_set_margin_end (card, 4);
  gtk_widget_set_margin_top (card, 4);
  gtk_widget_set_margin_bottom (card, 4);
  gtk_widget_set_margin_start (card, 10);
  gtk_widget_set_margin_end (card, 10);
  gtk_widget_set_margin_top (card, 8);
  gtk_widget_set_margin_bottom (card, 8);

  GtkWidget *text_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_hexpand (text_box, TRUE);

  /* Click-to-rename: GtkEditableLabel shows as a plain label until
   * clicked, then becomes an inline text entry - no separate "rename"
   * button or dialog needed. */
  GtkWidget *title_entry = gtk_editable_label_new (title);
  gtk_widget_add_css_class (title_entry, "heading");
  gtk_widget_set_halign (title_entry, GTK_ALIGN_START);

  GtkWidget *range_label = gtk_label_new (page_range);
  gtk_widget_set_halign (range_label, GTK_ALIGN_START);
  gtk_widget_add_css_class (range_label, "dim-label");

  gtk_box_append (GTK_BOX (text_box), title_entry);
  gtk_box_append (GTK_BOX (text_box), range_label);

  GtkWidget *remove_button = gtk_button_new_with_label ("Remove");
  gtk_widget_add_css_class (remove_button, "flat");
  gtk_widget_set_valign (remove_button, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text (remove_button, "Remove this chapter");

  gtk_box_append (GTK_BOX (card), text_box);
  gtk_box_append (GTK_BOX (card), remove_button);

  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), card);

  if (on_remove != NULL) {
    RemoveClosure *closure = g_new (RemoveClosure, 1);
    closure->callback = on_remove;
    closure->user_data = user_data;
    g_signal_connect_data (remove_button, "clicked",
                            G_CALLBACK (on_remove_button_clicked),
                            closure, remove_closure_free, 0);
  }

  if (on_rename != NULL) {
    RenameClosure *closure = g_new (RenameClosure, 1);
    closure->callback = on_rename;
    closure->user_data = user_data;
    g_signal_connect_data (title_entry, "notify::text",
                            G_CALLBACK (on_title_text_notify),
                            closure, rename_closure_free, 0);
  }

  return row;
}

GtkWidget *
mainwindow_new_thumbnail_card (GdkTexture *texture, int page_number)
{
  GtkWidget *frame = gtk_frame_new (NULL);

  GtkWidget *outer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

  GtkWidget *stripe = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class (stripe, "thumb-stripe");
  gtk_widget_add_css_class (stripe, "thumb-stripe-none");
  gtk_widget_set_vexpand (stripe, TRUE);

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_add_css_class (box, "card");
  gtk_widget_set_hexpand (box, TRUE);

  /* Request the picture's exact pixel size explicitly rather than giving
   * only a width and letting GTK infer height from the paintable's
   * intrinsic aspect ratio - that computation was observed to collapse to
   * zero height on at least one real GTK 4.14 build, hiding the image
   * entirely. Explicit width+height sidesteps that layout path. */
  int tex_w = gdk_texture_get_width (texture);
  int tex_h = gdk_texture_get_height (texture);
  if (tex_w <= 0) tex_w = THUMB_PLACEHOLDER_WIDTH;
  if (tex_h <= 0) tex_h = THUMB_PLACEHOLDER_WIDTH;

  GtkWidget *picture = gtk_picture_new_for_paintable (GDK_PAINTABLE (texture));
  gtk_picture_set_content_fit (GTK_PICTURE (picture), GTK_CONTENT_FIT_CONTAIN);
  gtk_widget_set_size_request (picture, tex_w, tex_h);

  char label_text[32];
  g_snprintf (label_text, sizeof (label_text), "Page %d", page_number);
  GtkWidget *label = gtk_label_new (label_text);

  gtk_box_append (GTK_BOX (box), picture);
  gtk_box_append (GTK_BOX (box), label);

  gtk_box_append (GTK_BOX (outer), stripe);
  gtk_box_append (GTK_BOX (outer), box);

  gtk_frame_set_child (GTK_FRAME (frame), outer);

  /* Stash the stripe so mainwindow_set_thumbnail_accent can find it later
   * without the caller having to track it separately. */
  g_object_set_data (G_OBJECT (frame), "chapter-stripe", stripe);

  return frame;
}

void
mainwindow_set_thumbnail_accent (GtkWidget *card, int accent_index)
{
  g_return_if_fail (card != NULL);

  GtkWidget *stripe = g_object_get_data (G_OBJECT (card), "chapter-stripe");
  if (stripe == NULL)
    return;

  set_accent_class (stripe, "chapter-accent", accent_index);

  if (accent_index < 0)
    gtk_widget_add_css_class (stripe, "thumb-stripe-none");
  else
    gtk_widget_remove_css_class (stripe, "thumb-stripe-none");
}

static GtkWidget *
build_left_pane (MainWindow *mw)
{
  GtkWidget *pane = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

  GtkWidget *toolbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_margin_start (toolbar, 8);
  gtk_widget_set_margin_end (toolbar, 8);
  gtk_widget_set_margin_top (toolbar, 8);

  GtkWidget *jump_label = gtk_label_new ("Go to page");
  gtk_widget_add_css_class (jump_label, "dim-label");

  mw->page_jump_entry = gtk_entry_new ();
  gtk_entry_set_input_purpose (GTK_ENTRY (mw->page_jump_entry), GTK_INPUT_PURPOSE_DIGITS);
  gtk_entry_set_max_length (GTK_ENTRY (mw->page_jump_entry), 5);
  gtk_widget_set_size_request (mw->page_jump_entry, 80, -1);
  gtk_widget_set_tooltip_text (mw->page_jump_entry, "Page number");

  mw->page_jump_button = gtk_button_new_with_label ("Go");

  gtk_box_append (GTK_BOX (toolbar), jump_label);
  gtk_box_append (GTK_BOX (toolbar), mw->page_jump_entry);
  gtk_box_append (GTK_BOX (toolbar), mw->page_jump_button);

  GtkWidget *scrolled = gtk_scrolled_window_new ();
  gtk_widget_set_hexpand (scrolled, TRUE);
  gtk_widget_set_vexpand (scrolled, TRUE);

  mw->thumbnail_flowbox = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (mw->thumbnail_flowbox), GTK_SELECTION_MULTIPLE);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (mw->thumbnail_flowbox), TRUE);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (mw->thumbnail_flowbox), 8);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (mw->thumbnail_flowbox), 8);
  gtk_widget_set_margin_start (mw->thumbnail_flowbox, 8);
  gtk_widget_set_margin_end (mw->thumbnail_flowbox, 8);
  gtk_widget_set_margin_top (mw->thumbnail_flowbox, 8);
  gtk_widget_set_margin_bottom (mw->thumbnail_flowbox, 8);

  /* Starts empty; populated with real rendered thumbnails once a PDF is
   * loaded (wired in the controller). */

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), mw->thumbnail_flowbox);

  gtk_box_append (GTK_BOX (pane), toolbar);
  gtk_box_append (GTK_BOX (pane), scrolled);

  return pane;
}

static GtkWidget *
build_right_pane (MainWindow *mw)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_size_request (box, 280, -1);
  gtk_widget_set_margin_start (box, 8);
  gtk_widget_set_margin_end (box, 8);
  gtk_widget_set_margin_top (box, 8);
  gtk_widget_set_margin_bottom (box, 8);

  GtkWidget *heading = gtk_label_new ("Chapters");
  gtk_widget_add_css_class (heading, "title-4");
  gtk_widget_set_halign (heading, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (box), heading);

  GtkWidget *scrolled = gtk_scrolled_window_new ();
  gtk_widget_set_vexpand (scrolled, TRUE);

  mw->chapter_listbox = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (mw->chapter_listbox), GTK_SELECTION_NONE);
  gtk_widget_add_css_class (mw->chapter_listbox, "boxed-list");

  /* Starts empty; populated with real chapters as the user assigns
   * page ranges (wired in the controller). Each row carries its own
   * Remove button, so there's no separate selection-based remove flow. */

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), mw->chapter_listbox);
  gtk_box_append (GTK_BOX (box), scrolled);

  mw->add_chapter_button = gtk_button_new_with_label ("Add Chapter");
  gtk_widget_add_css_class (mw->add_chapter_button, "suggested-action");
  gtk_box_append (GTK_BOX (box), mw->add_chapter_button);

  mw->unassigned_label = gtk_label_new ("");
  gtk_widget_add_css_class (mw->unassigned_label, "dim-label");
  gtk_widget_add_css_class (mw->unassigned_label, "caption");
  gtk_widget_set_halign (mw->unassigned_label, GTK_ALIGN_START);
  gtk_label_set_wrap (GTK_LABEL (mw->unassigned_label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (mw->unassigned_label), 0.0);
  gtk_box_append (GTK_BOX (box), mw->unassigned_label);

  return box;
}

static GtkWidget *
build_header_bar (MainWindow *mw)
{
  GtkWidget *header = gtk_header_bar_new ();

  mw->open_button = gtk_button_new_with_label ("Open PDF...");
  gtk_header_bar_pack_start (GTK_HEADER_BAR (header), mw->open_button);

  mw->export_button = gtk_button_new_with_label ("Export All");
  gtk_widget_add_css_class (mw->export_button, "suggested-action");
  gtk_header_bar_pack_end (GTK_HEADER_BAR (header), mw->export_button);

  return header;
}

MainWindow *
mainwindow_new (GtkApplication *app)
{
  load_accent_css ();

  MainWindow *mw = g_new0 (MainWindow, 1);

  mw->window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (mw->window), "PDF Chapter Splitter");
  gtk_window_set_default_size (GTK_WINDOW (mw->window), 1000, 700);
  gtk_window_set_titlebar (GTK_WINDOW (mw->window), build_header_bar (mw));

  GtkWidget *paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_paned_set_start_child (GTK_PANED (paned), build_left_pane (mw));
  gtk_paned_set_resize_start_child (GTK_PANED (paned), TRUE);
  gtk_paned_set_end_child (GTK_PANED (paned), build_right_pane (mw));
  gtk_paned_set_resize_end_child (GTK_PANED (paned), FALSE);
  /* Never let the sidebar shrink below its natural size to fit whatever
   * position value below happens to leave - on a system with larger
   * default fonts/DPI than assumed here, the sidebar's real required
   * width can exceed the leftover space, and shrink-end-child defaults
   * to TRUE, which clips content rather than growing to fit it. */
  gtk_paned_set_shrink_end_child (GTK_PANED (paned), FALSE);
  gtk_paned_set_position (GTK_PANED (paned), 700);

  gtk_window_set_child (GTK_WINDOW (mw->window), paned);
  gtk_window_present (GTK_WINDOW (mw->window));

  return mw;
}

GtkWidget *
mainwindow_show_loading_dialog (GtkWindow *parent)
{
  GtkWidget *dialog = gtk_window_new ();
  gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_decorated (GTK_WINDOW (dialog), FALSE);
  gtk_window_set_default_size (GTK_WINDOW (dialog), 260, 130);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top (box, 20);
  gtk_widget_set_margin_bottom (box, 20);
  gtk_widget_set_margin_start (box, 20);
  gtk_widget_set_margin_end (box, 20);
  gtk_widget_set_vexpand (box, TRUE);

  GtkWidget *spinner = gtk_spinner_new ();
  gtk_widget_set_size_request (spinner, 32, 32);
  gtk_spinner_start (GTK_SPINNER (spinner));

  GtkWidget *label = gtk_label_new ("Loading PDF...");

  GtkWidget *progress = gtk_progress_bar_new ();
  gtk_widget_set_size_request (progress, 200, -1);
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress), 0.0);

  gtk_box_append (GTK_BOX (box), spinner);
  gtk_box_append (GTK_BOX (box), label);
  gtk_box_append (GTK_BOX (box), progress);

  /* Stash the progress bar on the dialog so mainwindow_set_loading_progress
   * can find it later without the caller having to track it separately. */
  g_object_set_data (G_OBJECT (dialog), "progress-bar", progress);

  gtk_window_set_child (GTK_WINDOW (dialog), box);
  gtk_window_present (GTK_WINDOW (dialog));

  return dialog;
}

void
mainwindow_set_loading_progress (GtkWidget *dialog, double fraction)
{
  if (dialog == NULL)
    return;

  if (fraction < 0.0) fraction = 0.0;
  if (fraction > 1.0) fraction = 1.0;

  GtkWidget *progress = g_object_get_data (G_OBJECT (dialog), "progress-bar");
  if (progress != NULL)
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress), fraction);
}

void
mainwindow_close_loading_dialog (GtkWidget *dialog)
{
  if (dialog == NULL)
    return;
  gtk_window_destroy (GTK_WINDOW (dialog));
}

void
mainwindow_free (MainWindow *mw)
{
  g_free (mw);
}
