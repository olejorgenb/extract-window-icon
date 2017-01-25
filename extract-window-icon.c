/**
 * Copyright © 2017 Ole Jørgen Brønner <olejorgenbb@yahoo.no>,
 *                  Julien Danjou <julien@danjou.info>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/**
 * Most of this code is lifted/adapted from awesome-wm
 */

#include <stdio.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_util.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>


#define MAX(a,b) ((a) < (b) ? (b) : (a))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

typedef int bool;
#define false 0
#define true  1

#define p_new(type, count)   ((type *)malloc(sizeof(type) * (count)))


static xcb_connection_t *connection = NULL;
static xcb_screen_t *screen = NULL;
static xcb_atom_t _NET_WM_ICON;


/** Send request to get NET_WM_ICON (EWMH)
 * \param w The window.
 * \return The cookie associated with the request.
 */
static xcb_get_property_cookie_t
ewmh_window_icon_get_unchecked(xcb_window_t w)
{
    return xcb_get_property_unchecked(connection, false, w,
                                      _NET_WM_ICON, XCB_ATOM_CARDINAL, 0, UINT32_MAX);
}

static cairo_user_data_key_t data_key;

static void
free_data(void *data)
{
    free(data);
}

/** Create a surface object from this image data.
 * \param width The width of the image.
 * \param height The height of the image
 * \param data The image's data in ARGB format, will be copied by this function.
 */
static cairo_surface_t *
draw_surface_from_data(int width, int height, uint32_t *data)
{
    unsigned long int len = width * height;
    unsigned long int i;
    uint32_t *buffer = p_new(uint32_t, len);
    cairo_surface_t *surface;

    /* Cairo wants premultiplied alpha, meh :( */
    for(i = 0; i < len; i++)
    {
        uint8_t a = (data[i] >> 24) & 0xff;
        double alpha = a / 255.0;
        uint8_t r = ((data[i] >> 16) & 0xff) * alpha;
        uint8_t g = ((data[i] >>  8) & 0xff) * alpha;
        uint8_t b = ((data[i] >>  0) & 0xff) * alpha;
        buffer[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }

    surface =
        cairo_image_surface_create_for_data((unsigned char *) buffer,
                                            CAIRO_FORMAT_ARGB32,
                                            width,
                                            height,
                                            width*4);
    /* This makes sure that buffer will be freed */
    cairo_surface_set_user_data(surface, &data_key, buffer, &free_data);

    return surface;
}

static cairo_surface_t *
ewmh_window_icon_from_reply(xcb_get_property_reply_t *r, uint32_t preferred_size)
{
    uint32_t *data, *end, *found_data = 0;
    uint32_t found_size = 0;

    if(!r || r->type != XCB_ATOM_CARDINAL || r->format != 32 || r->length < 2)
        return 0;

    data = (uint32_t *) xcb_get_property_value(r);
    if (!data) return 0;

    end = data + r->length;

    /* Goes over the icon data and picks the icon that best matches the size preference.
     * In case the size match is not exact, picks the closest bigger size if present,
     * closest smaller size otherwise.
     */
    while (data + 1 < end) {
        /* check whether the data size specified by width and height fits into the array we got */
        uint64_t data_size = (uint64_t) data[0] * data[1];
        if (data_size > (uint64_t) (end - data - 2)) break;

        /* use the greater of the two dimensions to match against the preferred size */
        uint32_t size = MAX(data[0], data[1]);

        /* pick the icon if it's a better match than the one we already have */
        bool found_icon_too_small = found_size < preferred_size;
        bool found_icon_too_large = found_size > preferred_size;
        bool icon_empty = data[0] == 0 || data[1] == 0;
        bool better_because_bigger =  found_icon_too_small && size > found_size;
        bool better_because_smaller = found_icon_too_large &&
            size >= preferred_size && size < found_size;
        if (!icon_empty && (better_because_bigger || better_because_smaller || found_size == 0))
        {
            found_data = data;
            found_size = size;
        }

        data += data_size + 2;
    }

    if (!found_data) return 0;

    return draw_surface_from_data(found_data[0], found_data[1], found_data + 2);
}

/** Get NET_WM_ICON. */
static cairo_surface_t *
get_net_wm_icon(xcb_window_t xid, uint32_t preferred_size)
{
    xcb_get_property_cookie_t cookie =
        xcb_get_property_unchecked(connection, false, xid,
                                   _NET_WM_ICON, XCB_ATOM_CARDINAL, 0, UINT32_MAX);
    xcb_get_property_reply_t *r = xcb_get_property_reply(connection, cookie, NULL);
    cairo_surface_t *surface = ewmh_window_icon_from_reply(r, preferred_size);
    free(r);
    return surface;
}

static xcb_visualtype_t *
draw_find_visual(const xcb_screen_t *s, xcb_visualid_t visual)
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

static xcb_visualtype_t *
draw_default_visual(const xcb_screen_t *s)
{
    return draw_find_visual(s, s->root_visual);
}


static cairo_surface_t *
cairo_surface_from_pixmap(xcb_visualtype_t *visual,
                          xcb_pixmap_t pixmap,
                          xcb_pixmap_t mask)
{
    xcb_get_geometry_cookie_t geom_icon_c, geom_mask_c;
    xcb_get_geometry_reply_t *geom_icon_r, *geom_mask_r = NULL;
    cairo_surface_t *s_icon, *result;

    geom_icon_c = xcb_get_geometry_unchecked(connection, pixmap);
    if (mask)
        geom_mask_c = xcb_get_geometry_unchecked(connection, mask);
    geom_icon_r = xcb_get_geometry_reply(connection, geom_icon_c, NULL);
    if (mask)
        geom_mask_r = xcb_get_geometry_reply(connection, geom_mask_c, NULL);

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
        s_icon = cairo_xcb_surface_create_for_bitmap(connection,
                screen, pixmap, geom_icon_r->width, geom_icon_r->height);
    else
        s_icon = cairo_xcb_surface_create(connection, pixmap, visual,
                geom_icon_r->width, geom_icon_r->height);

    result = s_icon;

    if (mask)
    {
        cairo_surface_t *s_mask;
        cairo_t *cr;

        result = cairo_surface_create_similar(s_icon, CAIRO_CONTENT_COLOR_ALPHA, geom_icon_r->width, geom_icon_r->height);
        s_mask = cairo_xcb_surface_create_for_bitmap(connection,
                screen, mask, geom_icon_r->width, geom_icon_r->height);
        cr = cairo_create(result);

        cairo_set_source_surface(cr, s_icon, 0, 0);
        cairo_mask_surface(cr, s_mask, 0, 0);
        cairo_surface_destroy(s_mask);
        cairo_destroy(cr);
    }


    if (result != s_icon)
        cairo_surface_destroy(s_icon);

out:
    free(geom_icon_r);
    free(geom_mask_r);

    return result;
}


/* Cool kids use macros */
#define INIT_ATOM(name) init_atom(#name, &name)

static void
init_atom(const char *name, xcb_atom_t *dest)
{
    xcb_intern_atom_cookie_t c =
        xcb_intern_atom_unchecked(connection, false, strlen(name), name);
    xcb_intern_atom_reply_t *r =
    xcb_intern_atom_reply(connection, c, NULL);
    *dest = r->atom;
    free(r);
}

static void
init_atoms()
{
    INIT_ATOM(_NET_WM_ICON);
}

static cairo_surface_t *
get_wm_hints_icon(xcb_window_t xid) {
    xcb_get_property_cookie_t wmh_c =
        xcb_icccm_get_wm_hints_unchecked(connection, xid);

    xcb_icccm_wm_hints_t wmh;
    if(!xcb_icccm_get_wm_hints_reply(connection,
                                     wmh_c,
                                     &wmh, NULL)) {
        return NULL;
    }

    cairo_surface_t *result = NULL;
    if(wmh.flags & XCB_ICCCM_WM_HINT_ICON_PIXMAP) {
        if(wmh.flags & XCB_ICCCM_WM_HINT_ICON_MASK)
            result = cairo_surface_from_pixmap(draw_default_visual(screen),
                                               wmh.icon_pixmap,
                                               wmh.icon_mask);
    }
    return result;
}

static bool
parse_xid(const char *id_str, xcb_window_t *ret)
{
    xcb_window_t xid = 0;
    sscanf(id_str, "0x%x", &xid);
    if (!xid)
        sscanf(id_str, "%u", &xid);
    if (!xid) {
        return false;
    }
    *ret = xid;
    return true;
}

int
main(int argc, char *argv[])
{
    xcb_window_t xid = 0;
    int screen_nr = -1;

    if(argc <= 1) {
        fprintf(stderr, "Expected an X window identifier\n");
        exit(1);
    }

    if(!parse_xid(argv[1], &xid)) {
        fprintf(stderr, "Invalid window id format: %s\n", argv[1]);
        exit(1);
    }

    connection = xcb_connect(NULL, &screen_nr);
    screen = xcb_aux_get_screen(connection, screen_nr);

    if (xcb_connection_has_error(connection)) {
        fprintf(stderr, "Failed to open display: %s\n", "$DISPLAY");
        return 1;
    }

    init_atoms();

    cairo_surface_t *hint_icon = get_wm_hints_icon(xid);
    cairo_surface_t *net_wm_icon = get_net_wm_icon(xid, 64);

    bool found_some_icon = false;

    if(hint_icon != NULL) {
        char *filename = "wm_hints-icon.png";
        cairo_surface_write_to_png(hint_icon, filename);
        cairo_surface_destroy(hint_icon);
        fprintf(stderr, "Found WM_HINTS icon (%s)\n", filename);
        found_some_icon = true;
    }

    if(net_wm_icon != NULL) {
        char *filename = "net_wm_hints-icon.png";
        cairo_surface_write_to_png(net_wm_icon, filename);
        cairo_surface_destroy(net_wm_icon);
        fprintf(stderr, "Found _NET_WM_ICON icon (%s)\n", filename);
        found_some_icon = true;
    }

    if(!found_some_icon)
        exit(2);
}
