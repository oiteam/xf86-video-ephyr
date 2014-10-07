/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@o-hand.com>
 * 
 * Copyright © 2004 Nokia 
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Nokia not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. Nokia makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * NOKIA DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL NOKIA BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _EPHYR_H_
#define _EPHYR_H_

#include <xcb/xcb_image.h>

typedef struct _ephyrScrPriv {
    /* Host X window info */
    xcb_window_t win;
    xcb_window_t win_pre_existing; /* Set via -parent option like xnest */
    xcb_window_t peer_win;         /* Used for GL; should be at most one */
    xcb_image_t *ximg;
    Bool win_explicit_position;
    int win_x, win_y;
    int win_width, win_height;
    int server_depth;
    const char *output;            /* Set via -output option */
    unsigned char *fb_data;        /* only used when host bpp != server bpp */
    xcb_shm_segment_info_t shminfo;

    KdScreenInfo *screen;
    int mynum;                     /* Screen number */

    /**
     * Per-screen Xlib-using state for glamor (private to
     * ephyr_glamor_glx.c)
     */
    struct ephyr_glamor *glamor;
} EphyrScrPriv;

void
ephyrPoll(void);

/* hostx.c glamor support */
Bool
ephyr_glamor_init(ScreenPtr pScreen);

Bool
ephyr_glamor_create_screen_resources(ScreenPtr pScreen);

void
ephyr_glamor_enable(ScreenPtr pScreen);

void
ephyr_glamor_disable(ScreenPtr pScreen);

void
ephyr_glamor_fini(ScreenPtr pScreen);

void
ephyr_glamor_host_paint_rect(ScreenPtr pScreen);

/* ephyrvideo.c */
Bool
ephyrInitVideo(ScreenPtr pScreen);

/* ephyr_glamor_xv.c */
#ifdef GLAMOR
void
ephyr_glamor_xv_init(ScreenPtr screen);
#else /* !GLAMOR */

static inline void
ephyr_glamor_xv_init(ScreenPtr screen)
{
}
#endif /* !GLAMOR */

#endif
