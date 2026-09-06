#include "pdfdoc.h"

#include <gdk/gdk.h>
#include <poppler.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include <math.h>

#define PDFDOC_ERROR pdfdoc_error_quark ()

typedef enum {
  PDFDOC_ERROR_INVALID_RANGE,
  PDFDOC_ERROR_EXPORT_FAILED,
} PdfDocError;

struct _PdfDoc {
  PopplerDocument *document;
};

static GQuark
pdfdoc_error_quark (void)
{
  return g_quark_from_static_string ("pdfdoc-error-quark");
}

PdfDoc *
pdfdoc_load (const char *path, GError **error)
{
  g_return_val_if_fail (path != NULL, NULL);

  g_autofree char *uri = NULL;
  if (g_path_is_absolute (path)) {
    uri = g_filename_to_uri (path, NULL, error);
  } else {
    g_autofree char *abs_path = NULL;
    g_autofree char *cwd = g_get_current_dir ();
    abs_path = g_build_filename (cwd, path, NULL);
    uri = g_filename_to_uri (abs_path, NULL, error);
  }

  if (uri == NULL)
    return NULL;

  PopplerDocument *document = poppler_document_new_from_file (uri, NULL, error);
  if (document == NULL)
    return NULL;

  PdfDoc *doc = g_new0 (PdfDoc, 1);
  doc->document = document;
  return doc;
}

int
pdfdoc_get_page_count (PdfDoc *doc)
{
  g_return_val_if_fail (doc != NULL, 0);
  return poppler_document_get_n_pages (doc->document);
}

GdkPixbuf *
pdfdoc_render_page_thumbnail (PdfDoc *doc,
                               int page_index,
                               int target_width_px,
                               GError **error)
{
  g_return_val_if_fail (doc != NULL, NULL);
  g_return_val_if_fail (target_width_px > 0, NULL);

  int n_pages = poppler_document_get_n_pages (doc->document);
  if (page_index < 0 || page_index >= n_pages) {
    g_set_error (error, PDFDOC_ERROR, PDFDOC_ERROR_INVALID_RANGE,
                 "page index %d out of range (document has %d pages)",
                 page_index, n_pages);
    return NULL;
  }

  PopplerPage *page = poppler_document_get_page (doc->document, page_index);
  if (page == NULL) {
    g_set_error (error, PDFDOC_ERROR, PDFDOC_ERROR_INVALID_RANGE,
                 "failed to get page %d", page_index);
    return NULL;
  }

  double page_w, page_h;
  poppler_page_get_size (page, &page_w, &page_h);

  double scale = (double) target_width_px / page_w;
  int render_w = target_width_px;
  int render_h = (int) lround (page_h * scale);
  if (render_h < 1)
    render_h = 1;

  cairo_surface_t *surface =
      cairo_image_surface_create (CAIRO_FORMAT_ARGB32, render_w, render_h);
  cairo_t *cr = cairo_create (surface);

  /* Paint white background first: pages without an explicit background
   * would otherwise render with a transparent (black-on-composite) look. */
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_paint (cr);

  cairo_scale (cr, scale, scale);
  poppler_page_render (page, cr);

  cairo_destroy (cr);

  GdkPixbuf *pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, render_w, render_h);

  cairo_surface_destroy (surface);
  g_object_unref (page);

  return pixbuf;
}

gboolean
pdfdoc_export_range (PdfDoc *doc,
                      int first_page,
                      int last_page,
                      const char *out_path,
                      GError **error)
{
  g_return_val_if_fail (doc != NULL, FALSE);
  g_return_val_if_fail (out_path != NULL, FALSE);

  int n_pages = poppler_document_get_n_pages (doc->document);

  if (first_page < 0 || last_page < first_page || last_page >= n_pages) {
    g_set_error (error, PDFDOC_ERROR, PDFDOC_ERROR_INVALID_RANGE,
                 "invalid page range [%d, %d] for document with %d pages",
                 first_page, last_page, n_pages);
    return FALSE;
  }

  /* Cairo needs an initial size; the first page's size is fine since we
   * override it per-page below with cairo_pdf_surface_set_size. */
  PopplerPage *first = poppler_document_get_page (doc->document, first_page);
  double init_w, init_h;
  poppler_page_get_size (first, &init_w, &init_h);

  cairo_surface_t *surface = cairo_pdf_surface_create (out_path, init_w, init_h);
  cairo_status_t surf_status = cairo_surface_status (surface);
  if (surf_status != CAIRO_STATUS_SUCCESS) {
    g_set_error (error, PDFDOC_ERROR, PDFDOC_ERROR_EXPORT_FAILED,
                 "failed to create output PDF at %s: %s",
                 out_path, cairo_status_to_string (surf_status));
    cairo_surface_destroy (surface);
    g_object_unref (first);
    return FALSE;
  }

  cairo_t *cr = cairo_create (surface);
  g_object_unref (first);

  gboolean ok = TRUE;

  for (int i = first_page; i <= last_page; i++) {
    PopplerPage *page = poppler_document_get_page (doc->document, i);
    if (page == NULL) {
      g_set_error (error, PDFDOC_ERROR, PDFDOC_ERROR_EXPORT_FAILED,
                   "failed to get page %d while exporting", i);
      ok = FALSE;
      break;
    }

    double w, h;
    poppler_page_get_size (page, &w, &h);
    cairo_pdf_surface_set_size (surface, w, h);

    poppler_page_render_for_printing (page, cr);
    cairo_show_page (cr);

    g_object_unref (page);
  }

  cairo_destroy (cr);
  cairo_surface_finish (surface);

  cairo_status_t final_status = cairo_surface_status (surface);
  if (ok && final_status != CAIRO_STATUS_SUCCESS) {
    g_set_error (error, PDFDOC_ERROR, PDFDOC_ERROR_EXPORT_FAILED,
                 "error finalizing output PDF %s: %s",
                 out_path, cairo_status_to_string (final_status));
    ok = FALSE;
  }

  cairo_surface_destroy (surface);

  return ok;
}

void
pdfdoc_free (PdfDoc *doc)
{
  if (doc == NULL)
    return;
  g_clear_object (&doc->document);
  g_free (doc);
}
