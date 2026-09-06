#include <check.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include <poppler.h>

#include "pdfdoc.h"

static char *test_pdf_path;
static char *test_out_dir;

/* Build a synthetic 4-page, 200x300pt PDF so tests don't depend on any
 * external fixture file. Page N gets a distinct fill color so we could
 * visually tell pages apart if needed. */
static void
create_synthetic_pdf (const char *path, int n_pages)
{
  cairo_surface_t *surface = cairo_pdf_surface_create (path, 200, 300);
  cairo_t *cr = cairo_create (surface);

  for (int i = 0; i < n_pages; i++) {
    cairo_set_source_rgb (cr, (double) i / n_pages, 0.2, 0.5);
    cairo_paint (cr);
    cairo_show_page (cr);
  }

  cairo_destroy (cr);
  cairo_surface_destroy (surface);
}

static void
setup (void)
{
  test_pdf_path = g_build_filename (g_get_tmp_dir (), "pdfdoc_test_input.pdf", NULL);
  test_out_dir = g_dir_make_tmp ("pdfdoc_test_out_XXXXXX", NULL);
  create_synthetic_pdf (test_pdf_path, 4);
}

static void
teardown (void)
{
  g_remove (test_pdf_path);
  g_free (test_pdf_path);

  /* Best-effort cleanup of any files left in the temp output dir. */
  GDir *dir = g_dir_open (test_out_dir, 0, NULL);
  if (dir != NULL) {
    const char *name;
    while ((name = g_dir_read_name (dir)) != NULL) {
      char *full = g_build_filename (test_out_dir, name, NULL);
      g_remove (full);
      g_free (full);
    }
    g_dir_close (dir);
  }
  g_rmdir (test_out_dir);
  g_free (test_out_dir);
}

START_TEST (test_load_valid_pdf)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (test_pdf_path, &error);
  ck_assert_ptr_nonnull (doc);
  ck_assert_ptr_null (error);
  pdfdoc_free (doc);
}
END_TEST

START_TEST (test_load_missing_file_fails)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load ("/nonexistent/path/does_not_exist.pdf", &error);
  ck_assert_ptr_null (doc);
  ck_assert_ptr_nonnull (error);
  g_error_free (error);
}
END_TEST

START_TEST (test_page_count)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (test_pdf_path, &error);
  ck_assert_ptr_nonnull (doc);
  ck_assert_int_eq (pdfdoc_get_page_count (doc), 4);
  pdfdoc_free (doc);
}
END_TEST

START_TEST (test_render_thumbnail_dimensions)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (test_pdf_path, &error);
  ck_assert_ptr_nonnull (doc);

  GdkPixbuf *pixbuf = pdfdoc_render_page_thumbnail (doc, 0, 100, &error);
  ck_assert_ptr_nonnull (pixbuf);
  ck_assert_ptr_null (error);

  /* Source page is 200x300pt; requesting width 100 should give height 150. */
  ck_assert_int_eq (gdk_pixbuf_get_width (pixbuf), 100);
  ck_assert_int_eq (gdk_pixbuf_get_height (pixbuf), 150);

  g_object_unref (pixbuf);
  pdfdoc_free (doc);
}
END_TEST

START_TEST (test_render_thumbnail_out_of_range_fails)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (test_pdf_path, &error);
  ck_assert_ptr_nonnull (doc);

  GdkPixbuf *pixbuf = pdfdoc_render_page_thumbnail (doc, 99, 100, &error);
  ck_assert_ptr_null (pixbuf);
  ck_assert_ptr_nonnull (error);

  g_error_free (error);
  pdfdoc_free (doc);
}
END_TEST

START_TEST (test_export_range_produces_correct_page_count)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (test_pdf_path, &error);
  ck_assert_ptr_nonnull (doc);

  char *out_path = g_build_filename (test_out_dir, "chapter_out.pdf", NULL);

  /* Export pages 1..2 (0-indexed) -> 2 pages in output. */
  gboolean ok = pdfdoc_export_range (doc, 1, 2, out_path, &error);
  ck_assert (ok);
  ck_assert_ptr_null (error);

  /* Re-open the exported file with poppler directly to verify page count,
   * independent of our own loader. */
  char *uri = g_filename_to_uri (out_path, NULL, NULL);
  PopplerDocument *reopened = poppler_document_new_from_file (uri, NULL, &error);
  ck_assert_ptr_nonnull (reopened);
  ck_assert_int_eq (poppler_document_get_n_pages (reopened), 2);

  g_object_unref (reopened);
  g_free (uri);
  g_free (out_path);
  pdfdoc_free (doc);
}
END_TEST

START_TEST (test_export_invalid_range_fails)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (test_pdf_path, &error);
  ck_assert_ptr_nonnull (doc);

  char *out_path = g_build_filename (test_out_dir, "should_not_exist.pdf", NULL);

  /* last_page (10) is beyond the 4-page document. */
  gboolean ok = pdfdoc_export_range (doc, 0, 10, out_path, &error);
  ck_assert (!ok);
  ck_assert_ptr_nonnull (error);

  g_error_free (error);
  g_free (out_path);
  pdfdoc_free (doc);
}
END_TEST

START_TEST (test_export_reversed_range_fails)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (test_pdf_path, &error);
  ck_assert_ptr_nonnull (doc);

  char *out_path = g_build_filename (test_out_dir, "should_not_exist2.pdf", NULL);

  /* first_page > last_page should be rejected. */
  gboolean ok = pdfdoc_export_range (doc, 3, 1, out_path, &error);
  ck_assert (!ok);
  ck_assert_ptr_nonnull (error);

  g_error_free (error);
  g_free (out_path);
  pdfdoc_free (doc);
}
END_TEST

static Suite *
pdfdoc_suite (void)
{
  Suite *s = suite_create ("PdfDoc");
  TCase *tc = tcase_create ("core");

  tcase_add_checked_fixture (tc, setup, teardown);
  tcase_add_test (tc, test_load_valid_pdf);
  tcase_add_test (tc, test_load_missing_file_fails);
  tcase_add_test (tc, test_page_count);
  tcase_add_test (tc, test_render_thumbnail_dimensions);
  tcase_add_test (tc, test_render_thumbnail_out_of_range_fails);
  tcase_add_test (tc, test_export_range_produces_correct_page_count);
  tcase_add_test (tc, test_export_invalid_range_fails);
  tcase_add_test (tc, test_export_reversed_range_fails);

  suite_add_tcase (s, tc);
  return s;
}

int
main (void)
{
  Suite *s = pdfdoc_suite ();
  SRunner *runner = srunner_create (s);

  srunner_run_all (runner, CK_NORMAL);
  int failed = srunner_ntests_failed (runner);
  srunner_free (runner);

  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
