#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include "../view/mainwindow.h"

typedef struct _AppController AppController;

/* Creates the controller and wires up all signal handlers on mw's
 * widgets (Open PDF, Add Chapter, Remove). Does not take ownership of mw. */
AppController *app_controller_new (MainWindow *mw);

/* Load a PDF from a local path and populate the thumbnail grid with real
 * rendered pages. Exposed directly (in addition to the Open PDF button's
 * file-chooser flow) so it can be driven programmatically, e.g. from a
 * command-line argument at startup. Returns TRUE on success. */
gboolean app_controller_load_pdf (AppController *controller, const char *path);

void app_controller_free (AppController *controller);

#endif /* APP_CONTROLLER_H */
