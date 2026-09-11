# Performance notes

## Investigation: "Open PDF" slowness on large books (Sept 2026)

A 2204-page PDF took ~51.8s (stopwatch avg of 3 runs) to go from
"Open PDF" click to thumbnails visible. Investigated with
`bench/render_timing_harness` (isolates PdfDoc load+render from GTK) and
targeted instrumentation of `populate_thumbnails`.

### Finding 1: the event-pump loop was O(n^2) - fixed

`populate_thumbnails` drained the GLib main loop
(`while (g_main_context_iteration (NULL, FALSE)) { }`) after every single
`gtk_flow_box_append`. That forces a full flowbox relayout on every
append, and relayout cost scales with however many children are already
in the box - so pumping once per page turned an O(n) total layout cost
into O(n^2) over the whole load.

Instrumented on the 2204-page file: the pump step alone went from ~2s
(first 200 pages) to ~30s cumulative (last 200 pages) - visibly
accelerating, not flat.

**Fix (shipped):** batch the pump to once every `PUMP_INTERVAL_PAGES`
(25) pages instead of every page. See `src/controller/app_controller.c`.

**Result:** ~51.8s -> ~27.1s stopwatch average (1.9x faster). Pump cost
dropped from ~30.5s to ~4-5s and its growth became roughly linear.

### Finding 2: rendering is embarrassingly parallel - not yet implemented

After the pump fix, rendering (poppler + cairo rasterization) became the
dominant cost at ~22s out of ~27s total. Tested with
`bench/render_timing_harness_parallel`, which renders page ranges across
N threads, each with its own independent `PdfDoc` (avoids relying on
poppler's per-document thread-safety guarantees for concurrent render).

Measured on the same 2204-page file, 4 threads (matching a 4-core VM):

| threads | wall-clock render time | speedup | efficiency |
|---|---|---|---|
| 1 | 21.68s | 1.00x | 100% |
| 4 | 12.18s | 3.82x | 96% |

Near-linear scaling - essentially no contention between threads once
each has its own document handle.

**Why this isn't wired into the app yet:** GTK widgets
(`GdkTexture`, thumbnail cards, `gtk_flow_box_append`) can only be
created/touched on the main thread. Worker threads can only produce
`GdkPixbuf`s; getting them back to the main thread safely, in page
order (workers finish out of order), requires a proper producer/consumer
setup (`GAsyncQueue` + a reorder buffer on the main thread), not just a
thread pool. That's meaningfully more engineering risk (ordering bugs,
a worker failing mid-render, queue backpressure) than the pump fix was.

**Estimated payoff if implemented:** render collapses from ~22s to
~12s of work that overlaps with the main thread's own remaining work
(widget build + append + pump, ~5s). Total load time would likely land
around ~12-14s - roughly another ~2x on top of the pump fix, or
~3.7-4x faster than the original ~51.8s baseline.

**Status:** intentionally deferred. `bench/render_timing_harness_parallel`
is kept in the repo so this can be picked up later without re-deriving
the measurement.
