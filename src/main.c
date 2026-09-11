#include <gtk/gtk.h>
#include <poppler.h>

#include "view/mainwindow.h"
#include "controller/app_controller.h"

static char *startup_pdf_path = NULL;

static void
activate (GtkApplication *app, gpointer user_data)
{
  (void) user_data;

  /* Ownership note: mw and controller are intentionally leaked for the
   * app's lifetime - a single top-level window/controller pair that lives
   * until the process exits. Revisit if we ever support multiple windows
   * or explicit close-time cleanup. */
 MainWindow *mw = mainwindow_new (app);
 AppController *controller = app_controller_new (mw);

 if (startup_pdf_path != NULL)
    app_controller_load_pdf (controller, startup_pdf_path);
}

int
main (int argc, char **argv)
{
  /* Optional convenience: `pdf-chapter-splitter some.pdf` loads it at
   * startup instead of requiring the Open PDF dialog. Captured here and
   * consumed manually so it doesn't interfere with GApplication's own
   * argument handling below. */
  if (argc > 1)
    startup_pdf_path = argv[1];

  GtkApplication *app = gtk_application_new ("org.example.PdfChapterSplitter",
                                              G_APPLICATION_NON_UNIQUE);
  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  int status = g_application_run (G_APPLICATION (app), 1, argv);
  g_object_unref (app);
  return status;
}
