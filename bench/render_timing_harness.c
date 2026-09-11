/*
 * render_timing_harness: measures pdfdoc_load() and
 * pdfdoc_render_page_thumbnail() cost in isolation, with no GTK window,
 * no widget creation, and no main-loop event pumping.
 *
 * This exists to answer one question before any optimization work starts:
 * of the wall-clock time the real GUI app spends on "Open PDF", how much
 * is PDF decoding/rasterization (poppler + cairo) versus GTK overhead
 * (widget construction, GtkFlowBox layout, the event-pump loop in
 * app_controller.c)? Comparing this harness's output against a stopwatch
 * measurement of the real app answers that directly.
 *
 * Usage:
 *   ./bench/render_timing_harness <path-to.pdf> [thumbnail_width_px]
 *
 * thumbnail_width_px defaults to 140 to match THUMBNAIL_WIDTH_PX in
 * src/controller/app_controller.c, so the numbers are comparable.
 */

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <stdlib.h>

#include "pdfdoc.h"

#define DEFAULT_THUMBNAIL_WIDTH_PX 140

int
main (int argc, char **argv)
{
  if (argc < 2) {
    fprintf (stderr, "usage: %s <path-to.pdf> [thumbnail_width_px]\n", argv[0]);
    return 1;
  }

  const char *path = argv[1];
  int thumb_width = (argc >= 3) ? atoi (argv[2]) : DEFAULT_THUMBNAIL_WIDTH_PX;
  if (thumb_width <= 0) {
    fprintf (stderr, "thumbnail_width_px must be > 0\n");
    return 1;
  }

  gint64 t_start = g_get_monotonic_time ();

  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (path, &error);

  gint64 t_after_load = g_get_monotonic_time ();

  if (doc == NULL) {
    fprintf (stderr, "failed to load '%s': %s\n",
              path, error != NULL ? error->message : "unknown error");
    g_clear_error (&error);
    return 1;
  }

  int n_pages = pdfdoc_get_page_count (doc);
  printf ("loaded '%s': %d pages\n", path, n_pages);
  printf ("thumbnail width: %d px\n", thumb_width);

  int rendered = 0;
  int failed = 0;

  /* Per-page timing so we can report not just the total but the
   * distribution (min/max/avg) - useful for spotting whether cost is
   * uniform per page or grows as the loop progresses (which would point
   * at something accumulating, e.g. poppler-internal caching/fragmentation
   * rather than a flat per-page cost). */
  gint64 min_page_us = G_MAXINT64;
  gint64 max_page_us = 0;

  gint64 t_render_start = g_get_monotonic_time ();

  for (int i = 0; i < n_pages; i++) {
    gint64 page_t0 = g_get_monotonic_time ();

    GError *page_error = NULL;
    GdkPixbuf *pixbuf = pdfdoc_render_page_thumbnail (doc, i, thumb_width, &page_error);

    gint64 page_t1 = g_get_monotonic_time ();
    gint64 page_us = page_t1 - page_t0;

    if (pixbuf == NULL) {
      fprintf (stderr, "  page %d failed: %s\n",
                i, page_error != NULL ? page_error->message : "unknown error");
      g_clear_error (&page_error);
      failed++;
      continue;
    }

    g_object_unref (pixbuf);
    rendered++;

    if (page_us < min_page_us) min_page_us = page_us;
    if (page_us > max_page_us) max_page_us = page_us;
  }

  gint64 t_render_end = g_get_monotonic_time ();

  pdfdoc_free (doc);
  gint64 t_end = g_get_monotonic_time ();

  double load_s = (t_after_load - t_start) / 1e6;
  double render_s = (t_render_end - t_render_start) / 1e6;
  double free_s = (t_end - t_render_end) / 1e6;
  double total_s = (t_end - t_start) / 1e6;
  double avg_page_ms = rendered > 0 ? (render_s * 1000.0) / rendered : 0.0;

  printf ("\n--- results (GTK-free: no window, no widgets, no event loop) ---\n");
  printf ("pdfdoc_load:            %.3f s\n", load_s);
  printf ("render loop (%d pages): %.3f s  (%d ok, %d failed)\n",
           n_pages, render_s, rendered, failed);
  printf ("  avg per page:          %.3f ms\n", avg_page_ms);
  printf ("  min per page:           %.3f ms\n", min_page_us / 1000.0);
  printf ("  max per page:           %.3f ms\n", max_page_us / 1000.0);
  printf ("pdfdoc_free:            %.3f s\n", free_s);
  printf ("TOTAL:                  %.3f s\n", total_s);
  printf ("\nCompare TOTAL above against a stopwatch measurement of the real\n"
          "app's \"Open PDF\" -> thumbnails-visible time for the same file.\n"
          "The gap between the two is GTK/widget/event-loop overhead.\n");

  return (failed > 0) ? 1 : 0;
}
