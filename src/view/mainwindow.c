#include "mainwindow.h"

#define THUMB_PLACEHOLDER_WIDTH 140
#define THUMB_PLACEHOLDER_HEIGHT 180

/* A placeholder "page" widget standing in for a rendered thumbnail, so we
 * can verify the flowbox layout, spacing, and scrolling before any real
 * PDF is loaded (Phase 3). */
static GtkWidget *
make_placeholder_thumbnail (int page_number)
{
  GtkWidget *frame = gtk_frame_new (NULL);

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_size_request (box, THUMB_PLACEHOLDER_WIDTH, THUMB_PLACEHOLDER_HEIGHT);
  gtk_widget_add_css_class (box, "card");

  GtkWidget *page_icon = gtk_image_new_from_icon_name ("text-x-generic-symbolic");
  gtk_image_set_pixel_size (GTK_IMAGE (page_icon), 48);
  gtk_widget_set_valign (page_icon, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand (page_icon, TRUE);

  char label_text[32];
  g_snprintf (label_text, sizeof (label_text), "Page %d", page_number);
  GtkWidget *label = gtk_label_new (label_text);

  gtk_box_append (GTK_BOX (box), page_icon);
  gtk_box_append (GTK_BOX (box), label);

  gtk_frame_set_child (GTK_FRAME (frame), box);
  return frame;
}

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

  /* Placeholder pages to prove the grid wraps and scrolls correctly.
   * Phase 3 replaces this with real rendered thumbnails from the loaded
   * PDF. */
  for (int i = 1; i <= 12; i++) {
    gtk_flow_box_append (GTK_FLOW_BOX (mw->thumbnail_flowbox),
                          make_placeholder_thumbnail (i));
  }

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

  /* Placeholder chapters to prove the sidebar layout before real chapter
   * assignment is wired in (Phase 3). */
  gtk_list_box_append (GTK_LIST_BOX (mw->chapter_listbox),
                        make_placeholder_chapter_row ("Chapter 1", "Pages 1-12"));
  gtk_list_box_append (GTK_LIST_BOX (mw->chapter_listbox),
                        make_placeholder_chapter_row ("Chapter 2", "Pages 13-27"));

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
  gtk_paned_set_position (GTK_PANED (paned), 720);

  gtk_window_set_child (GTK_WINDOW (mw->window), paned);
  gtk_window_present (GTK_WINDOW (mw->window));

  return mw;
}

void
mainwindow_free (MainWindow *mw)
{
  g_free (mw);
}
