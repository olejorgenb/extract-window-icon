#include <stdio.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_util.h>
/* #include <xcb/shape.h> */
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>

/* A lot of this code is lifted/adapted from awesome-wm */

/* typedef xcb_get_property_cookie_t property_cookie; */

xcb_visualtype_t *draw_find_visual(const xcb_screen_t *s, xcb_visualid_t visual)
{
  xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(s);

  if(depth_iter.data)
    for(; depth_iter.rem; xcb_depth_next (&depth_iter))
      for(xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
          visual_iter.rem; xcb_visualtype_next (&visual_iter))
        if(visual == visual_iter.data->visual_id)
          return visual_iter.data;

  return NULL;
}

xcb_visualtype_t *draw_default_visual(const xcb_screen_t *s)
{
  return draw_find_visual(s, s->root_visual);
}


void
extract_pixmap(xcb_connection_t *xcb_connection,
               xcb_screen_t *screen,
               xcb_visualtype_t *visual,
               xcb_pixmap_t pixmap,
               xcb_pixmap_t mask,
               char *out_filename)
{
    xcb_get_geometry_cookie_t geom_icon_c, geom_mask_c;
    xcb_get_geometry_reply_t *geom_icon_r, *geom_mask_r = NULL;
    cairo_surface_t *s_icon, *result;

    geom_icon_c = xcb_get_geometry_unchecked(xcb_connection, pixmap);
    if (mask)
        geom_mask_c = xcb_get_geometry_unchecked(xcb_connection, mask);
    geom_icon_r = xcb_get_geometry_reply(xcb_connection, geom_icon_c, NULL);
    if (mask)
        geom_mask_r = xcb_get_geometry_reply(xcb_connection, geom_mask_c, NULL);

    if (!geom_icon_r || (mask && !geom_mask_r))
        goto out;

    /* if ((geom_icon_r->depth != 1 && geom_icon_r->depth != globalconf.screen->root_depth) */
    /*         || (geom_mask_r && geom_mask_r->depth != 1)) */
    /* { */
    /*     warn("Got pixmaps with depth (%d, %d) while processing pixmap, but only depth 1 and %d are allowed", */
    /*             geom_icon_r->depth, geom_mask_r ? geom_mask_r->depth : 0, globalconf.screen->root_depth); */
    /*     goto out; */
    /* } */

    if (geom_icon_r->depth == 1)
        s_icon = cairo_xcb_surface_create_for_bitmap(xcb_connection,
                screen, pixmap, geom_icon_r->width, geom_icon_r->height);
    else
        s_icon = cairo_xcb_surface_create(xcb_connection, pixmap, visual,
                geom_icon_r->width, geom_icon_r->height);

    result = s_icon;

    if (mask)
    {
        cairo_surface_t *s_mask;
        cairo_t *cr;

        result = cairo_surface_create_similar(s_icon, CAIRO_CONTENT_COLOR_ALPHA, geom_icon_r->width, geom_icon_r->height);
        s_mask = cairo_xcb_surface_create_for_bitmap(xcb_connection,
                screen, mask, geom_icon_r->width, geom_icon_r->height);
        cr = cairo_create(result);

        cairo_set_source_surface(cr, s_icon, 0, 0);
        cairo_mask_surface(cr, s_mask, 0, 0);
        cairo_surface_destroy(s_mask);
        cairo_destroy(cr);
    }

    cairo_surface_write_to_png(result, out_filename);

    cairo_surface_destroy(result);
    if (result != s_icon)
        cairo_surface_destroy(s_icon);

out:
    free(geom_icon_r);
    free(geom_mask_r);
}

int main(int argc, char *argv[]) {
  xcb_window_t xid = 0;
  sscanf(argv[1], "0x%lx", &xid);
  if (!xid)
    sscanf(argv[1], "%lu", &xid);
  if (!xid) {
    fprintf(stderr, "Invalid window id format: %s.", argv[1]);
    exit(1);
  }

  char *out_filename = argv[2];


  int screen_nr;
  xcb_connection_t *xcb_connection = xcb_connect (NULL, &screen_nr);

  xcb_screen_t *screen = xcb_aux_get_screen(xcb_connection, screen_nr);

  if ( xcb_connection_has_error ( xcb_connection ) ) {
    fprintf ( stderr, "Failed to open display: %s", "$DISPLAY" );
    return 1;
  }

  xcb_get_property_cookie_t wmh_c =
    xcb_icccm_get_wm_hints_unchecked(xcb_connection, xid);

  xcb_icccm_wm_hints_t wmh;
  if(!xcb_icccm_get_wm_hints_reply(xcb_connection,
                                   wmh_c,
                                   &wmh, NULL)) {
    return 1;
  }

  if(wmh.flags & XCB_ICCCM_WM_HINT_ICON_PIXMAP) {
      if(wmh.flags & XCB_ICCCM_WM_HINT_ICON_MASK)
        extract_pixmap(xcb_connection, screen, draw_default_visual(screen), wmh.icon_pixmap, wmh.icon_mask, out_filename);
  }
}
