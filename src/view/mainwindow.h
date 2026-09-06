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

#endif /* MAINWINDOW_H */
