/*
 * pattern.c : Support and specifications for patterns.
 *
 * Author:
 *     Jody Goldberg <jgoldberg@home.com>
 *
 *  (C) 1999, 2000 Jody Goldberg
 */
#include "config.h"
#include "pattern.h"
#include "color.h"

typedef struct {
	int const x, y;
	char const pattern [8];
} gnumeric_sheet_pattern_t;

static gnumeric_sheet_pattern_t const
gnumeric_sheet_patterns [GNUMERIC_SHEET_PATTERNS] = {
/* 1 */	{ 8, 8, /* Solid */
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
/* 2 */	{ 8, 8, /* 75% */
	  { 0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee, 0xbb, 0xee } },
/* 3 */	{ 8, 8, /* 50% */
	  { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 } },
/* 4 */	{ 8, 8, /* 25% */
	  { 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88 } },
/* 5 */	{ 8, 8, /* 12.5% */
	  { 0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00 } },
/* 6 */	{ 8, 8, /* 6.25% */
	  { 0x20, 0x00, 0x02, 0x00, 0x20, 0x00, 0x02, 0x00 } },
/* 7 */	{ 8, 8, /* Horizontal Stripe */
	  { 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff } },
/* 8 */	{ 8, 8, /* Vertical Stripe */
	  { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33 } },
/* 9 */	{ 8, 8, /* Reverse Diagonal Stripe */
	  { 0xcc, 0x66, 0x33, 0x99, 0xcc, 0x66, 0x33, 0x99 } },
/* 10*/	{ 8, 8, /* Diagonal Stripe */
	  { 0x33, 0x66, 0xcc, 0x99, 0x33, 0x66, 0xcc, 0x99 } },
/* 11*/	{ 8, 8, /* Diagonal Crosshatch */
	  { 0x99, 0x66, 0x66, 0x99, 0x99, 0x66, 0x66, 0x99 } },
/* 12*/	{ 8, 8, /* Thick Diagonal Crosshatch */
	  { 0xff, 0x66, 0xff, 0x99, 0xff, 0x66, 0xff, 0x99 } },
/* 13*/	{ 8, 8, /* Thin Horizontal Stripe */
	  { 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00 } },
/* 14*/	{ 8, 8, /* Thin Vertical Stripe */
	  { 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22 } },
/* 15*/	{ 8, 8, /* Thin Reverse Diagonal Stripe */
	  { 0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88 } },
/* 16*/	{ 8, 8, /* Thin Diagonal Stripe */
	  { 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 } },
/* 17*/	{ 8, 8, /* Thin Crosshatch */
	  { 0x22, 0x22, 0xff, 0x22, 0x22, 0x22, 0xff, 0x22 } },
/* 18*/	{ 8, 8, /* Thin Diagonal Crosshatch */
	  { 0x88, 0x55, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55 } },
/* 19*/	{ 8, 8, /* Applix small circle */
	  { 0x99, 0x55, 0x33, 0xff, 0x99, 0x55, 0x33, 0xff } },
/* 20*/	{ 8, 8, /* Applix semicircle */
	  { 0x10, 0x10, 0x28, 0xc7, 0x01, 0x01, 0x82, 0x7c } },
/* 21*/	{ 8, 8, /* Applix small thatch */
	  { 0x22, 0x74, 0xf8, 0x71, 0x22, 0x17, 0x8f, 0x47 } },
/* 22*/	{ 8, 8, /* Applix round thatch */
	  { 0xc1, 0x80, 0x1c, 0x3e, 0x3e, 0x3e, 0x1c, 0x80 } },
/* 23*/	{ 8, 8, /* Applix Brick */
	  { 0x20, 0x20, 0x20, 0xff, 0x02, 0x02, 0x02, 0xff } },
/* 24*/	{ 8, 8, /* 100% */
	  { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
/* 25*/	{ 6, 6, /* 87.5% */
	  { 0xfe, 0xef, 0xfb, 0xdf, 0xfd, 0xf7, 0x00, 0x00 } }
};

GdkPixmap *
gnumeric_pattern_get_stipple (gint index)
{
	static GdkPixmap *patterns [GNUMERIC_SHEET_PATTERNS];
	static gboolean	  need_init = TRUE;

	/* Initialize the patterns to NULL */
	if (need_init) {
		int i;
		for (i = GNUMERIC_SHEET_PATTERNS; --i >= 0 ;)
			patterns [i] = NULL;
	}

	g_return_val_if_fail (index >= 0, NULL);
	g_return_val_if_fail (index <= GNUMERIC_SHEET_PATTERNS, NULL);

	if (index == 0)
		return NULL;

	--index;
	if (patterns [index] == NULL) {
		gnumeric_sheet_pattern_t const * pat = gnumeric_sheet_patterns + index;
		patterns [index] = gdk_bitmap_create_from_data (
			NULL, pat->pattern, pat->x, pat->y);
	}

	return patterns [index];
}

/*
 * gnumeric_background_set_gc : Set up a GdkGC to paint the background
 *                              of a cell.
 * return : TRUE if there is a background to paint.
 */
gboolean
gnumeric_background_set_gc (MStyle *mstyle, GdkGC *gc,
			    GnomeCanvas *canvas,
			    gboolean const is_selected)
{
	int pattern;

	/*
	 * Draw the background if the PATTERN is non 0
	 * Draw a stipple too if the pattern is > 1
	 */
	if (!mstyle_is_element_set (mstyle, MSTYLE_PATTERN))
		return FALSE;
	pattern = mstyle_get_pattern (mstyle);
	if (pattern > 0) {
		GdkColor   *back;
		StyleColor *back_col =
			mstyle_get_color (mstyle, MSTYLE_COLOR_BACK);
		g_return_val_if_fail (back_col != NULL, FALSE);

		back = is_selected ? &back_col->selected_color : &back_col->color;

		if (pattern > 1) {
			StyleColor *pat_col =
				mstyle_get_color (mstyle, MSTYLE_COLOR_PATTERN);
			g_return_val_if_fail (pat_col != NULL, FALSE);

			gdk_gc_set_fill (gc, GDK_OPAQUE_STIPPLED);
			gdk_gc_set_foreground (gc, &pat_col->color);
			gdk_gc_set_background (gc, back);
			gdk_gc_set_stipple (gc, gnumeric_pattern_get_stipple (pattern));
			gnome_canvas_set_stipple_origin (canvas, gc);
		} else {
			gdk_gc_set_fill (gc, GDK_SOLID);
			gdk_gc_set_foreground (gc, back);
		}
		return TRUE;
	} else {
		/* Set this in case we have a spanning column */
		gdk_gc_set_fill (gc, GDK_SOLID);
		gdk_gc_set_foreground (gc, is_selected ? &gs_lavender : &gs_white);
	}
	return FALSE;
}

gboolean
gnumeric_background_set_pc (MStyle *mstyle, GnomePrintContext *context)
{
	int pattern;

	/*
	 * Draw the background if the PATTERN is non 0
	 * Draw a stipple too if the pattern is > 1
	 */
	if (!mstyle_is_element_set (mstyle, MSTYLE_PATTERN))
		return FALSE;
	pattern = mstyle_get_pattern (mstyle);
	if (pattern > 0) {
		StyleColor *back_col =
			mstyle_get_color (mstyle, MSTYLE_COLOR_BACK);

		g_return_val_if_fail (back_col != NULL, FALSE);

		gnome_print_setrgbcolor (context,
					 back_col->red   / (double) 0xffff,
					 back_col->green / (double) 0xffff,
					 back_col->blue  / (double) 0xffff);

#if 0
		/* Support grey scale patterns.  FIXME how to do the rest ? */
		if (pattern >= 1 && pattern <= 5) {
			static double const gray[] = { 1., .75, .50, .25, .125, .0625 };

			/* FIXME : why no support for setgray in gnome-print ? */
		}

		if (pattern > 1) {
			StyleColor *pat_col =
				mstyle_get_color (mstyle, MSTYLE_COLOR_PATTERN);
			g_return_val_if_fail (pat_col != NULL, FALSE);

			gdk_gc_set_fill (gc, GDK_OPAQUE_STIPPLED);
			gdk_gc_set_foreground (gc, &pat_col->color);
			gdk_gc_set_background (gc, back);
			gdk_gc_set_stipple (gc, gnumeric_pattern_get_stipple (pattern));
			gnome_canvas_set_stipple_origin (canvas, gc);
		} else {
			gdk_gc_set_fill (gc, GDK_SOLID);
			gdk_gc_set_foreground (gc, back);
		}
#endif
		return TRUE;
	}

	return FALSE;
}
