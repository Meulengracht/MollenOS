/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Wm Type Definitions & Structures
 * - This header describes the base wm-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __LIBWM_TYPES_H__
#define __LIBWM_TYPES_H__

#define WM_HEADER_MAGIC 0xDEAE5A9A

typedef enum {
    // client => server
    wm_request_pong,
    wm_request_window_create,
    wm_request_window_destroy,
    wm_request_window_set_title,
    wm_request_surface,
    wm_request_surface_release,
    wm_request_surface_redraw,
    wm_request_surface_set_active,
    wm_request_surface_resize,
    
    // server => client
    wm_event_ping,
    wm_event_window_created,
    wm_event_surface_obtained,
    wm_event_surface_resized
} wm_event_type_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} wm_rect_t;

typedef enum {
    surface_8r8g8b8a,
} wm_surface_format_t;

typedef struct {
    uint8_t  type;
    uint8_t  flags;
    int16_t  rel_x;
    int16_t  rel_y;
    int16_t  rel_z;
    uint32_t buttons_set;
} wm_input_event_t; 

typedef struct {
    wm_rect_t           dimensions;
    wm_surface_format_t format;
} wm_surface_descriptor_t;

// length field contains total length including header
typedef struct {
    unsigned int    magic;
    wm_event_type_t event;
    unsigned int    length;
} wm_request_header_t;

typedef struct {
    wm_request_header_t header;
    unsigned int        sequence;
} wm_request_pong_t;

typedef struct {
    wm_request_header_t     header;
    wm_surface_descriptor_t surface_descriptor;
    unsigned                flags;
} wm_request_window_create_t;

typedef struct {
    wm_request_header_t header;
    char                title[64];
} wm_request_window_set_title_t;

typedef struct {
    wm_request_header_t     header;
    wm_surface_descriptor_t surface_descriptor;
} wm_request_surface_t;

typedef struct {
    wm_request_header_t header;
    wm_rect_t           dirty_area;
} wm_request_surface_redraw_t;

typedef struct {
    wm_request_header_t header;
    int                 handle;
} wm_request_surface_set_active_t;

typedef struct {
    wm_request_header_t header;
    int                 handle;
    wm_rect_t           new_dimensions;
} wm_request_surface_resize_t;

#endif // !__LIBWM_TYPES_H__
