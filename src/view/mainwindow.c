#include "mainwindow.h"

#define THUMB_PLACEHOLDER_WIDTH 140

static GtkWidget *
make_placeholder_chapter_row (const char *title, const char *page_range)
{
  GtkWidget *row = gtk_list_box_row_new ();

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_margin_start (box, 8);
  gtk_widget_set_margin_end (box, 8);
  gtk_widget_set_margin_top (box, 6);
  gtk_widget_set_margin_bottom (box, 6);

  GtkWidget *title_label = gtk_label_new (title);
  gtk_widget_set_halign (title_label, GTK_ALIGN_START);
  gtk_widget_add_css_class (title_label, "heading");

  GtkWidget *range_label = gtk_label_new (page_range);
  gtk_widget_set_halign (range_label, GTK_ALIGN_START);
  gtk_widget_add_css_class (range_label, "dim-label");

  gtk_box_append (GTK_BOX (box), title_label);
  gtk_box_append (GTK_BOX (box), range_label);

  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
  return row;
}

GtkWidget *
mainwindow_new_chapter_row (const char *title, const char *page_range)
{
  return make_placeholder_chapter_row (title, page_range);
}

GtkWidget *
mainwindow_new_thumbnail_card (GdkTexture *texture, int page_number)
{
  GtkWidget *frame = gtk_frame_new (NULL);

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_add_css_class (box, "card");

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

  gtk_frame_set_child (GTK_FRAME (frame), box);
  return frame;
}

static GtkWidget *
build_left_pane (MainWindow *mw)
{
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
  return scrolled;
}

static GtkWidget *
build_right_pane (MainWindow *mw)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_size_request (box, 260, -1);
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
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (mw->chapter_listbox), GTK_SELECTION_SINGLE);
  gtk_widget_add_css_class (mw->chapter_listbox, "boxed-list");

  /* Starts empty; populated with real chapters as the user assigns
   * page ranges (wired in the controller). */

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), mw->chapter_listbox);
  gtk_box_append (GTK_BOX (box), scrolled);

  GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  mw->add_chapter_button = gtk_button_new_with_label ("Add Chapter");
  mw->remove_chapter_button = gtk_button_new_with_label ("Remove");
  gtk_widget_set_hexpand (mw->add_chapter_button, TRUE);
  gtk_box_append (GTK_BOX (button_box), mw->add_chapter_button);
  gtk_box_append (GTK_BOX (button_box), mw->remove_chapter_button);
  gtk_box_append (GTK_BOX (box), button_box);

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
  gtk_paned_set_position (GTK_PANED (paned), 720);

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
