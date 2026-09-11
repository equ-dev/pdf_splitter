#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include <poppler.h>

#include "pdfdoc.h"
#include "app_controller.h"

/*
 * These tests exercise app_controller_export_chapters directly - the
 * GTK-free core of the "Export All" feature - without creating any GTK
 * widgets, window, or display. That's deliberate: the bug that shipped
 * (the button had no click handler at all, and once wired, the result
 * dialog crashed on a NULL printf-style format string) went unnoticed
 * because nothing but a human clicking the button ever exercised this
 * path. gtk_alert_dialog_new/on_export_folder_response's dialog wrapper
 * are intentionally NOT covered here - driving a native GTK dialog from
 * an automated test isn't practical - but the actual export logic
 * (building filenames, calling pdfdoc_export_range per chapter,
 * tolerating per-chapter failures) now can be.
 */

static char *test_pdf_path;
static char *test_out_dir;

/* Build a synthetic n_pages-page, 200x300pt PDF so tests don't depend on
 * any external fixture file. */
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
  test_pdf_path = g_build_filename (g_get_tmp_dir (), "app_controller_test_input.pdf", NULL);
  test_out_dir = g_dir_make_tmp ("app_controller_test_out_XXXXXX", NULL);
  create_synthetic_pdf (test_pdf_path, 10);
}

static void
teardown (void)
{
  g_remove (test_pdf_path);
  g_free (test_pdf_path);

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

/* Reopens a PDF independently via poppler and returns its page count, or
 * -1 if it can't be opened - used to verify exported files are actually
 * valid, correctly-sized PDFs rather than just checking they exist. */
static int
reopened_page_count (const char *path)
{
  char *uri = g_filename_to_uri (path, NULL, NULL);
  if (uri == NULL)
    return -1;

  PopplerDocument *doc = poppler_document_new_from_file (uri, NULL, NULL);
  g_free (uri);
  if (doc == NULL)
    return -1;

  int n = poppler_document_get_n_pages (doc);
  g_object_unref (doc);
  return n;
}

START_TEST (test_export_two_chapters_succeeds)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (test_pdf_path, &error);
  ck_assert_ptr_nonnull (doc);

  ChapterSpec chapters[] = {
    { .first_page = 0, .last_page = 2, .title = "Introduction" },  /* 3 pages */
    { .first_page = 3, .last_page = 9, .title = "The Rest" },      /* 7 pages */
  };

  guint n_failed = 99;
  char *failure_detail = (char *) 0x1; /* poison, should end up NULL */
  guint n_ok = app_controller_export_chapters (doc, chapters, 2, test_out_dir,
                                                &n_failed, &failure_detail);

  ck_assert_uint_eq (n_ok, 2);
  ck_assert_uint_eq (n_failed, 0);
  ck_assert_ptr_null (failure_detail);

  char *path1 = g_build_filename (test_out_dir, "Chapter_01_Introduction.pdf", NULL);
  char *path2 = g_build_filename (test_out_dir, "Chapter_02_The_Rest.pdf", NULL);

  ck_assert_int_eq (reopened_page_count (path1), 3);
  ck_assert_int_eq (reopened_page_count (path2), 7);

  g_free (path1);
  g_free (path2);
  pdfdoc_free (doc);
}
END_TEST

START_TEST (test_export_continues_past_one_bad_chapter)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (test_pdf_path, &error);
  ck_assert_ptr_nonnull (doc);

  /* Second chapter's range (20..25) is out of bounds for a 10-page doc;
   * the first and third chapters are valid and should still be written -
   * mirrors the real-world case of one bad chapter not costing the user
   * the rest of the book. */
  ChapterSpec chapters[] = {
    { .first_page = 0, .last_page = 1, .title = "Good One" },
    { .first_page = 20, .last_page = 25, .title = "Bad Range" },
    { .first_page = 2, .last_page = 3, .title = "Good Two" },
  };

  guint n_failed = 0;
  char *failure_detail = NULL;
  guint n_ok = app_controller_export_chapters (doc, chapters, 3, test_out_dir,
                                                &n_failed, &failure_detail);

  ck_assert_uint_eq (n_ok, 2);
  ck_assert_uint_eq (n_failed, 1);
  ck_assert_ptr_nonnull (failure_detail);
  ck_assert (strstr (failure_detail, "Chapter_02_Bad_Range.pdf") != NULL);

  char *good1 = g_build_filename (test_out_dir, "Chapter_01_Good_One.pdf", NULL);
  char *bad = g_build_filename (test_out_dir, "Chapter_02_Bad_Range.pdf", NULL);
  char *good2 = g_build_filename (test_out_dir, "Chapter_03_Good_Two.pdf", NULL);

  ck_assert_int_eq (reopened_page_count (good1), 2);
  ck_assert (!g_file_test (bad, G_FILE_TEST_EXISTS)); /* invalid range: no file written */
  ck_assert_int_eq (reopened_page_count (good2), 2);

  g_free (good1);
  g_free (bad);
  g_free (good2);
  g_free (failure_detail);
  pdfdoc_free (doc);
}
END_TEST

START_TEST (test_export_sanitizes_unsafe_title_characters)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (test_pdf_path, &error);
  ck_assert_ptr_nonnull (doc);

  /* A title with path separators, punctuation, and spaces must not
   * escape the output folder or otherwise produce a broken path. */
  ChapterSpec chapters[] = {
    { .first_page = 0, .last_page = 0, .title = "../Weird: Title? / Name!" },
  };

  guint n_failed = 0;
  guint n_ok = app_controller_export_chapters (doc, chapters, 1, test_out_dir,
                                                &n_failed, NULL);

  ck_assert_uint_eq (n_ok, 1);
  ck_assert_uint_eq (n_failed, 0);

  /* Sanitizer keeps alnum/-/_ and maps spaces to '_', dropping
   * everything else - so this should land as a single plain file
   * directly inside test_out_dir, not in a parent or nested directory. */
  char *expected = g_build_filename (test_out_dir, "Chapter_01_Weird_Title__Name.pdf", NULL);
  ck_assert_int_eq (reopened_page_count (expected), 1);

  g_free (expected);
  pdfdoc_free (doc);
}
END_TEST

START_TEST (test_export_zero_chapters_is_a_no_op)
{
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (test_pdf_path, &error);
  ck_assert_ptr_nonnull (doc);

  guint n_failed = 42;
  char *failure_detail = (char *) 0x1;
  guint n_ok = app_controller_export_chapters (doc, NULL, 0, test_out_dir,
                                                &n_failed, &failure_detail);

  ck_assert_uint_eq (n_ok, 0);
  ck_assert_uint_eq (n_failed, 0);
  ck_assert_ptr_null (failure_detail);

  pdfdoc_free (doc);
}
END_TEST

static Suite *
app_controller_suite (void)
{
  Suite *s = suite_create ("AppController");
  TCase *tc = tcase_create ("export");

  tcase_add_checked_fixture (tc, setup, teardown);
  tcase_add_test (tc, test_export_two_chapters_succeeds);
  tcase_add_test (tc, test_export_continues_past_one_bad_chapter);
  tcase_add_test (tc, test_export_sanitizes_unsafe_title_characters);
  tcase_add_test (tc, test_export_zero_chapters_is_a_no_op);

  suite_add_tcase (s, tc);
  return s;
}

int
main (void)
{
  Suite *s = app_controller_suite ();
  SRunner *runner = srunner_create (s);

  srunner_run_all (runner, CK_NORMAL);
  int failed = srunner_ntests_failed (runner);
  srunner_free (runner);

  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
