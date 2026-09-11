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
 *   ./bench/render_timing_harness <path-to.pdf> [thumbnail_width_px] [csv_out_path]
 *
 * thumbnail_width_px defaults to 140 to match THUMBNAIL_WIDTH_PX in
 * src/controller/app_controller.c, so the numbers are comparable.
 *
 * If csv_out_path is given, every page's (index, time_ms) is written out
 * in page order, so the full distribution can be plotted as a time series
 * - the shape (flat, scattered spikes, or a steady climb) tells you
 * whether slow pages are content-specific or something is accumulating.
 */

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <stdlib.h>

#include "pdfdoc.h"

#define DEFAULT_THUMBNAIL_WIDTH_PX 140
#define TOP_SLOWEST_COUNT 10

typedef struct {
  int page_index; /* 0-indexed */
  gint64 time_us;
} PageTiming;

static int
compare_time_desc (const void *a, const void *b)
{
  const PageTiming *pa = a;
  const PageTiming *pb = b;
  if (pa->time_us < pb->time_us) return 1;
  if (pa->time_us > pb->time_us) return -1;
  return 0;
}

int
main (int argc, char **argv)
{
  if (argc < 2) {
    fprintf (stderr, "usage: %s <path-to.pdf> [thumbnail_width_px]\n", argv[0]);
    return 1;
  }

  const char *path = argv[1];
  int thumb_width = (argc >= 3) ? atoi (argv[2]) : DEFAULT_THUMBNAIL_WIDTH_PX;
  const char *csv_out_path = (argc >= 4) ? argv[3] : NULL;
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

  /* One entry per successfully-rendered page, in page order - kept
   * around after the loop so we can report the slowest pages by index
   * and (optionally) dump the full series for plotting. */
  PageTiming *page_timings = g_new0 (PageTiming, n_pages);
  int n_timings = 0;

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

    page_timings[n_timings].page_index = i;
    page_timings[n_timings].time_us = page_us;
    n_timings++;
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

  /* Sort a copy so the top-N-slowest report doesn't disturb page_timings'
   * page-order, which the CSV dump below still needs. */
  PageTiming *sorted = g_memdup2 (page_timings, n_timings * sizeof (PageTiming));
  qsort (sorted, n_timings, sizeof (PageTiming), compare_time_desc);

  int top_n = n_timings < TOP_SLOWEST_COUNT ? n_timings : TOP_SLOWEST_COUNT;
  printf ("\n--- %d slowest pages ---\n", top_n);
  for (int i = 0; i < top_n; i++) {
    printf ("  page %-5d %8.3f ms\n",
             sorted[i].page_index + 1, sorted[i].time_us / 1000.0);
  }

  /* Clustering signal: compare the average page-index of the slowest 5%
   * of pages against the document's midpoint. If slow pages are just
   * scattered, image-heavy content, this average should sit close to the
   * midpoint regardless of document length. If it skews sharply toward
   * the end (or climbs steadily), that points at something accumulating
   * over the run (cache growth, fragmentation) rather than per-page
   * content differences. This is a heuristic, not a proof - eyeball the
   * CSV dump too if this looks suspicious. */
  int slow_slice = n_timings / 20; /* top 5% */
  if (slow_slice < 1) slow_slice = 1;
  if (slow_slice > n_timings) slow_slice = n_timings;

  double sum_index = 0.0;
  for (int i = 0; i < slow_slice; i++)
    sum_index += sorted[i].page_index;
  double avg_slow_index = sum_index / slow_slice;
  double midpoint = (n_timings - 1) / 2.0;
  double skew = (n_timings > 1) ? (avg_slow_index - midpoint) / midpoint : 0.0;

  printf ("\n--- clustering signal (top 5%% slowest = %d pages) ---\n", slow_slice);
  printf ("  avg page index of slowest pages: %.1f  (document midpoint: %.1f)\n",
           avg_slow_index, midpoint);
  if (skew > 0.3) {
    printf ("  -> skews toward the END of the document.\n"
             "     Consistent with something accumulating over the run\n"
             "     (poppler page-object caching, allocator fragmentation).\n"
             "     Worth checking with the CSV dump before concluding it's real.\n");
  } else if (skew < -0.3) {
    printf ("  -> skews toward the START of the document.\n"
             "     Could be one-time warmup cost (font loading, first-page\n"
             "     initialization) rather than steady-state per-page cost.\n");
  } else {
    printf ("  -> roughly centered / no strong skew.\n"
             "     Consistent with scattered, content-specific slow pages\n"
             "     (e.g. image-heavy pages) rather than an accumulating cost.\n");
  }

  g_free (sorted);

  if (csv_out_path != NULL) {
    FILE *csv = fopen (csv_out_path, "w");
    if (csv == NULL) {
      fprintf (stderr, "\nfailed to open '%s' for writing CSV\n", csv_out_path);
    } else {
      fprintf (csv, "page_index,time_ms\n");
      for (int i = 0; i < n_timings; i++)
        fprintf (csv, "%d,%.3f\n", page_timings[i].page_index, page_timings[i].time_us / 1000.0);
      fclose (csv);
      printf ("\nper-page timings written to %s (page_index,time_ms)\n", csv_out_path);
    }
  }

  g_free (page_timings);

  return (failed > 0) ? 1 : 0;
}
