#include <gdk/gdk.h>

#if 0
#ifndef __GNUC__
#define __FUNCTION__ __FILE__
#endif
#define gnm_simple_canvas_grab(a,b,c,d)	do {		\
	g_printerr ("%s %d: grab "GNUMERIC_ITEM" %p\n",	\
		 __FUNCTION__, __LINE__, a);		\
	gnm_simple_canvas_grab (a, b, c,d);		\
} while (0)
#define gnm_simple_canvas_ungrab(a,b) do {			\
	g_printerr ("%s %d: ungrab "GNUMERIC_ITEM" %p\n",	\
		 __FUNCTION__, __LINE__, a);			\
	gnm_simple_canvas_ungrab (a, b);			\
} while (0)
#endif

void item_debug_cross (GdkDrawable *drawable, GdkGC *gc, int x1, int y1, int x2, int y2);
