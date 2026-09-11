#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include "../view/mainwindow.h"
#include "../model/pdfdoc.h"

typedef struct _AppController AppController;

/* One already-defined chapter's export inputs, deliberately decoupled
 * from GTK (no ChapterRange, no widgets) so app_controller_export_chapters
 * can be driven directly by a unit test without a window or display. */
typedef struct {
  int first_page;    /* 0-indexed */
  int last_page;      /* 0-indexed, inclusive */
  const char *title;  /* used to build the output filename; not copied,
                        * must outlive the call */
} ChapterSpec;

/* Creates the controller and wires up all signal handlers on mw's
 * widgets (Open PDF, Add Chapter, page jump, Export All). Does not take
 * ownership of mw. */
AppController *app_controller_new (MainWindow *mw);

/* Load a PDF from a local path and populate the thumbnail grid with real
 * rendered pages. Exposed directly (in addition to the Open PDF button's
 * file-chooser flow) so it can be driven programmatically, e.g. from a
 * command-line argument at startup. Returns TRUE on success. */
gboolean app_controller_load_pdf (AppController *controller, const char *path);

/* Exports each of the n_chapters entries in `chapters`, in the order
 * given, to "<folder_path>/Chapter_NN_<sanitized title>.pdf" (NN is the
 * 1-based position in the array, not tied to page numbers). Continues
 * past a per-chapter failure - e.g. an invalid range - so one bad
 * chapter doesn't lose the rest of the book.
 *
 * Returns the number of chapters exported successfully. If out_failed
 * is non-NULL, *out_failed is set to the number that failed. If
 * out_failure_detail is non-NULL, *out_failure_detail is set to a
 * newline-joined "<filename>: <error>" list describing the failures
 * (or NULL if there were none) - caller must g_free() it.
 *
 * This has no GTK dependency beyond PdfDoc, so it's safe to call from a
 * headless unit test; the Export All button is a thin GTK wrapper
 * around this that adds the folder-chooser dialog and result alert. */
guint app_controller_export_chapters (PdfDoc *doc,
                                       const ChapterSpec *chapters,
                                       guint n_chapters,
                                       const char *folder_path,
                                       guint *out_failed,
                                       char **out_failure_detail);

void app_controller_free (AppController *controller);

#endif /* APP_CONTROLLER_H */
