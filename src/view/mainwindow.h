#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <gtk/gtk.h>

/* Number of distinct chapter accent colors the view knows how to render
 * (CSS classes chapter-accent-0..chapter-accent-N-1 and
 * chapter-card-accent-0..chapter-card-accent-N-1). Callers should take
 * their color index mod this count. */
#define CHAPTER_ACCENT_COUNT 5

/* Chapter row callbacks. Kept as plain function pointers (rather than
 * requiring the caller to know GTK signal signatures) so the controller
 * layer doesn't need any GTK-callback boilerplate of its own. */
typedef void (*ChapterRemoveCallback) (gpointer user_data);
typedef void (*ChapterRenameCallback) (const char *new_title, gpointer user_data);

/*
 * MainWindow bundles the top-level window and the widgets the controller
 * layer needs direct handles to, so it doesn't have to walk the widget
 * tree by name.
 */
typedef struct {
  GtkWidget *window;

  /* Left pane: page-jump controls and the scrollable grid of thumbnails. */
  GtkWidget *page_jump_entry;
  GtkWidget *page_jump_button;
  GtkWidget *thumbnail_flowbox;

  /* Right pane: list of chapters being assembled, controls, and a summary
   * of any pages not yet assigned to a chapter. */
  GtkWidget *chapter_listbox;
  GtkWidget *add_chapter_button;
  GtkWidget *unassigned_label;

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

/* Build a chapter sidebar row: an editable title, page-range text, and a
 * Remove button, tinted with the given accent color (mod
 * CHAPTER_ACCENT_COUNT). on_remove fires when the row's Remove button is
 * clicked; on_rename fires whenever the title's text changes (the widget
 * itself stays in an always-editable state, so this can fire per
 * keystroke). Either callback may be NULL. Ownership of the returned
 * widget passes to whatever container it's appended to. */
GtkWidget *mainwindow_new_chapter_row (const char *title,
                                        const char *page_range,
                                        int accent_index,
                                        ChapterRemoveCallback on_remove,
                                        ChapterRenameCallback on_rename,
                                        gpointer user_data);

/* Build a thumbnail grid card wrapping a rendered page texture. Takes its
 * own reference to texture as GtkPicture normally does; caller retains
 * ownership of its own reference. The card starts unassigned to any
 * chapter (no accent stripe) - use mainwindow_set_thumbnail_accent to
 * color it once chapters exist. */
GtkWidget *mainwindow_new_thumbnail_card (GdkTexture *texture, int page_number);

/* Color (or clear) a thumbnail card's chapter-accent stripe. Pass an
 * accent_index (mod CHAPTER_ACCENT_COUNT) for a page that belongs to a
 * chapter, or a negative value to mark it unassigned. card must be a
 * widget previously returned by mainwindow_new_thumbnail_card. */
void mainwindow_set_thumbnail_accent (GtkWidget *card, int accent_index);

/* Show a small modal "Loading..." dialog with a spinner and a progress
 * bar, parented to the given window. Caller must close it with
 * mainwindow_close_loading_dialog once loading finishes. */
GtkWidget *mainwindow_show_loading_dialog (GtkWindow *parent);

/* Update the loading dialog's progress bar. fraction is clamped to
 * [0.0, 1.0]. Safe to call repeatedly as work completes. */
void mainwindow_set_loading_progress (GtkWidget *dialog, double fraction);

void mainwindow_close_loading_dialog (GtkWidget *dialog);

#endif /* MAINWINDOW_H */
