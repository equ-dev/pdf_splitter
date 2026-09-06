#include <gtk/gtk.h>
#include <poppler.h>

#include "view/mainwindow.h"

static void
activate (GtkApplication *app, gpointer user_data)
{
  (void) user_data;
  /* Ownership note: intentionally leaked for the app's lifetime - a single
   * top-level window that lives until the process exits. Revisit if we
   * ever support multiple windows or explicit close-time cleanup. */
  mainwindow_new (app);
}

int
main (int argc, char **argv)
{
  GtkApplication *app = gtk_application_new ("org.example.PdfChapterSplitter",
                                              G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  int status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);
  return status;
}
