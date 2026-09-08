#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <gtk/gtk.h>

/*
 * MainWindow bundles the top-level window and the widgets the controller
 * layer (Phase 3) needs direct handles to, so it doesn't have to walk the
 * widget tree by name. This phase only builds and lays out the widgets -
 * no PDF loading or button logic is wired in yet.
 */
typedef struct {
  GtkWidget *window;

  /* Left pane: scrollable grid of page thumbnails. */
  GtkWidget *thumbnail_flowbox;

  /* Right pane: list of chapters being assembled, and controls. */
  GtkWidget *chapter_listbox;
  GtkWidget *add_chapter_button;
  GtkWidget *remove_chapter_button;

  /* Header bar actions. */
  GtkWidget *open_button;
  GtkWidget *export_button;
} MainWindow;

/* Build and show the main window (unpopulated - no PDF loaded, no
 * chapters yet). The returned struct is owned by the caller; free it with
 * mainwindow_free() when the window closes. Widgets themselves remain
 * owned by GTK's normal parent/child ownership. */
MainWindow *mainwindow_new (GtkApplication *app);

void mainwindow_free (MainWindow *mw);

/* Build a chapter sidebar row (title + page range text). The controller
 * calls this when a chapter is added; ownership passes to whatever
 * container it's appended to (normal GTK parent/child ownership). */
GtkWidget *mainwindow_new_chapter_row (const char *title, const char *page_range);

/* Build a thumbnail grid card wrapping a rendered page texture. Takes its
 * own reference to texture as GtkPicture normally does; caller retains
 * ownership of its own reference. */
GtkWidget *mainwindow_new_thumbnail_card (GdkTexture *texture, int page_number);

/* Show a small modal "Loading..." dialog with a spinner and a progress
 * bar, parented to the given window. Caller must close it with
 * mainwindow_close_loading_dialog once loading finishes. */
GtkWidget *mainwindow_show_loading_dialog (GtkWindow *parent);

/* Update the loading dialog's progress bar. fraction is clamped to
 * [0.0, 1.0]. Safe to call repeatedly as work completes. */
void mainwindow_set_loading_progress (GtkWidget *dialog, double fraction);

void mainwindow_close_loading_dialog (GtkWidget *dialog);

#endif /* MAINWINDOW_H */
