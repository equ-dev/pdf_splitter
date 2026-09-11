# PDF Chapter Splitter

A small GTK4 desktop app for splitting a PDF textbook into per-chapter PDF
files. Load a book, page through real rendered thumbnails, select page
ranges, and export each chapter as its own PDF.

![Screenshot](docs/screenshot.png)

## Features

- **Open PDF** — pick any local PDF; every page renders as a real thumbnail
  in a scrollable grid.
- **Go to page** — jump straight to a page number instead of scrolling
  through a long book by hand.
- **Range selection** — click a page, then Shift+click another to select
  everything in between. Click a selected page again to clear the selection.
- **Chapters** — click **Add Chapter** to turn the current selection into a
  chapter entry in the sidebar. Each chapter gets its own color, shown both
  as a stripe on every thumbnail it covers and as a tint on its sidebar
  card, so it's obvious at a glance which pages belong to which chapter.
- **Rename inline** — click a chapter's title in the sidebar to edit it in
  place; no dialog required.
- **Remove** — each chapter card has its own Remove button.
- **Unassigned pages** — the sidebar lists any page ranges not yet claimed
  by a chapter, so gaps don't go unnoticed before export.
- **Export All** — writes each chapter to its own `Chapter_01.pdf`,
  `Chapter_02.pdf`, ... file.
- A loading dialog with a spinner and progress bar covers the (synchronous)
  load-and-render step so the app never looks frozen on a long book.

## Building

Requires GTK4, poppler-glib, and Meson/Ninja.

```bash
sudo apt-get install -y meson ninja-build libgtk-4-dev libpoppler-glib-dev check
```

```bash
meson setup build
meson compile -C build
```

## Running

```bash
./build/src/pdf-chapter-splitter            # opens with an empty grid; use Open PDF...
./build/src/pdf-chapter-splitter book.pdf   # loads book.pdf immediately
```

## Testing

The PDF-handling core (`src/model`) has an automated unit test suite built
on Check, covering loading, page counts, thumbnail rendering, and page-range
export (including error cases like invalid ranges).

```bash
meson test -C build -v
```

## Architecture

The app is split into three layers:

- **`src/model`** — `PdfDoc`: wraps a loaded PDF via poppler-glib. No GTK
  dependency beyond `GdkPixbuf`; fully unit-testable in isolation.
- **`src/view`** — `MainWindow`: builds and lays out all widgets (thumbnail
  grid, chapter sidebar, loading dialog). No PDF-handling logic.
- **`src/controller`** — `AppController`: wires the two together — button
  clicks, page-range selection, chapter bookkeeping, and calling into the
  model to load and export.

```
src/
├── main.c
├── model/       PdfDoc: load, render thumbnails, export page ranges
├── view/        MainWindow: widget layout only
└── controller/  AppController: wires model <-> view, owns app state
```
