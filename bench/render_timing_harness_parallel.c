/*
 * render_timing_harness_parallel: measures wall-clock speedup from
 * rendering pages across multiple threads instead of one.
 *
 * Each thread opens its OWN independent PdfDoc (via pdfdoc_load on the
 * same path) rather than sharing one PopplerDocument across threads -
 * poppler's per-document thread-safety guarantees for concurrent
 * rendering aren't something to rely on, and opening N cheap document
 * handles sidesteps the question entirely. Each thread then renders a
 * contiguous, non-overlapping slice of the page range.
 *
 * This exists to answer: given N cores, does splitting the render step
 * across threads actually buy close to an N x speedup, or does thread
 * overhead / contention eat most of it? Measure before deciding whether
 * to build this into the real app.
 *
 * Usage:
 *   ./bench/render_timing_harness_parallel <path.pdf> [thumb_width_px] [n_threads]
 *
 * n_threads defaults to the number of available processors (g_get_num_processors).
 */

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <stdlib.h>

#include "pdfdoc.h"

#define DEFAULT_THUMBNAIL_WIDTH_PX 140

typedef struct {
  const char *path;
  int thumb_width;
  int start_page;   /* inclusive, 0-indexed */
  int end_page;     /* exclusive */
  int thread_id;

  /* results, filled in by the thread */
  gint64 open_us;
  gint64 render_us;
  int rendered;
  int failed;
} ThreadJob;

static gpointer
render_slice (gpointer data)
{
  ThreadJob *job = data;

  gint64 t0 = g_get_monotonic_time ();
  GError *error = NULL;
  PdfDoc *doc = pdfdoc_load (job->path, &error);
  gint64 t1 = g_get_monotonic_time ();
  job->open_us = t1 - t0;

  if (doc == NULL) {
    fprintf (stderr, "thread %d: failed to open '%s': %s\n",
              job->thread_id, job->path, error != NULL ? error->message : "unknown");
    g_clear_error (&error);
    return NULL;
  }

  gint64 render_start = g_get_monotonic_time ();
  for (int i = job->start_page; i < job->end_page; i++) {
    GError *page_error = NULL;
    GdkPixbuf *pixbuf = pdfdoc_render_page_thumbnail (doc, i, job->thumb_width, &page_error);
    if (pixbuf == NULL) {
      fprintf (stderr, "thread %d: page %d failed: %s\n",
                job->thread_id, i, page_error != NULL ? page_error->message : "unknown");
      g_clear_error (&page_error);
      job->failed++;
      continue;
    }
    g_object_unref (pixbuf);
    job->rendered++;
  }
  job->render_us = g_get_monotonic_time () - render_start;

  pdfdoc_free (doc);
  return NULL;
}

int
main (int argc, char **argv)
{
  if (argc < 2) {
    fprintf (stderr, "usage: %s <path.pdf> [thumb_width_px] [n_threads]\n", argv[0]);
    return 1;
  }

  const char *path = argv[1];
  int thumb_width = (argc >= 3) ? atoi (argv[2]) : DEFAULT_THUMBNAIL_WIDTH_PX;
  int n_threads = (argc >= 4) ? atoi (argv[3]) : (int) g_get_num_processors ();

  if (thumb_width <= 0) {
    fprintf (stderr, "thumb_width_px must be > 0\n");
    return 1;
  }
  if (n_threads <= 0) {
    fprintf (stderr, "n_threads must be > 0\n");
    return 1;
  }

  /* Open once up front just to get the page count and fail fast on a
   * bad path, before spinning up threads. */
  GError *error = NULL;
  PdfDoc *probe = pdfdoc_load (path, &error);
  if (probe == NULL) {
    fprintf (stderr, "failed to load '%s': %s\n",
              path, error != NULL ? error->message : "unknown error");
    g_clear_error (&error);
    return 1;
  }
  int n_pages = pdfdoc_get_page_count (probe);
  pdfdoc_free (probe);

  printf ("'%s': %d pages, thumbnail width %d px, %d threads (nproc=%u)\n",
           path, n_pages, thumb_width, n_threads, g_get_num_processors ());

  if (n_threads > n_pages)
    n_threads = n_pages > 0 ? n_pages : 1;

  ThreadJob *jobs = g_new0 (ThreadJob, n_threads);
  GThread **threads = g_new0 (GThread *, n_threads);

  int base = n_pages / n_threads;
  int remainder = n_pages % n_threads;
  int cursor = 0;

  gint64 wall_start = g_get_monotonic_time ();

  for (int t = 0; t < n_threads; t++) {
    /* Distribute the remainder one page at a time across the first
     * `remainder` threads so slices are as even as possible. */
    int slice = base + (t < remainder ? 1 : 0);

    jobs[t].path = path;
    jobs[t].thumb_width = thumb_width;
    jobs[t].start_page = cursor;
    jobs[t].end_page = cursor + slice;
    jobs[t].thread_id = t;
    cursor += slice;

    threads[t] = g_thread_new (NULL, render_slice, &jobs[t]);
  }

  for (int t = 0; t < n_threads; t++)
    g_thread_join (threads[t]);

  gint64 wall_us = g_get_monotonic_time () - wall_start;

  int total_rendered = 0, total_failed = 0;
  gint64 max_open_us = 0, max_render_us = 0, sum_render_us = 0;

  printf ("\n--- per-thread ---\n");
  for (int t = 0; t < n_threads; t++) {
    printf ("  thread %d: pages [%d, %d)  open=%.3fs render=%.3fs  (%d ok, %d failed)\n",
             jobs[t].thread_id, jobs[t].start_page, jobs[t].end_page,
             jobs[t].open_us / 1e6, jobs[t].render_us / 1e6,
             jobs[t].rendered, jobs[t].failed);
    total_rendered += jobs[t].rendered;
    total_failed += jobs[t].failed;
    sum_render_us += jobs[t].render_us;
    if (jobs[t].open_us > max_open_us) max_open_us = jobs[t].open_us;
    if (jobs[t].render_us > max_render_us) max_render_us = jobs[t].render_us;
  }

  double wall_s = wall_us / 1e6;
  double sum_render_s = sum_render_us / 1e6;
  double ideal_speedup = n_threads;
  double actual_speedup = sum_render_s / wall_s;

  printf ("\n--- summary ---\n");
  printf ("threads:                 %d\n", n_threads);
  printf ("pages rendered:          %d ok, %d failed\n", total_rendered, total_failed);
  printf ("sum of per-thread render time (= work done): %.3f s\n", sum_render_s);
  printf ("wall-clock time (this run):                  %.3f s\n", wall_s);
  printf ("effective speedup:       %.2fx  (ideal with %d threads: %.2fx)\n",
           actual_speedup, n_threads, ideal_speedup);
  printf ("efficiency:              %.0f%%  (100%% = perfect linear scaling)\n",
           100.0 * actual_speedup / ideal_speedup);
  printf ("\nCompare wall-clock time above against the single-threaded\n"
          "render_timing_harness result for the same file to see the real,\n"
          "measured benefit (or lack of one) from parallelizing.\n");

  g_free (jobs);
  g_free (threads);

  return (total_failed > 0) ? 1 : 0;
}
