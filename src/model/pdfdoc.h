#ifndef PDFDOC_H
#define PDFDOC_H

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/*
 * PdfDoc wraps a loaded PDF document and exposes the three operations the
 * rest of the app needs: page count, thumbnail rendering, and exporting a
 * contiguous page range to a standalone PDF file.
 */
typedef struct _PdfDoc PdfDoc;

/* Load a PDF from a local file path. Returns NULL and sets *error on
 * failure (missing file, corrupt/encrypted PDF, etc). */
PdfDoc *pdfdoc_load (const char *path, GError **error);

/* Number of pages in the document (>= 0). */
int pdfdoc_get_page_count (PdfDoc *doc);

/* Render page (0-indexed) to a GdkPixbuf scaled so its width is
 * target_width_px, preserving aspect ratio. Returns a new-reference
 * GdkPixbuf the caller must g_object_unref(), or NULL with *error set if
 * page_index is out of range. */
GdkPixbuf *pdfdoc_render_page_thumbnail (PdfDoc *doc,
                                          int page_index,
                                          int target_width_px,
                                          GError **error);

/* Export pages [first_page, last_page] (0-indexed, inclusive) to a new
 * single PDF file at out_path. Returns TRUE on success, FALSE with
 * *error set on failure (invalid range, unwritable path, etc). */
gboolean pdfdoc_export_range (PdfDoc *doc,
                               int first_page,
                               int last_page,
                               const char *out_path,
                               GError **error);

void pdfdoc_free (PdfDoc *doc);

#endif /* PDFDOC_H */
