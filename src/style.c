/*
 * Style.c: Style resource management
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  (C) 1998, 1999, 2000 Miguel de Icaza
 */
#include <config.h>
#include <gnome.h>
#include <string.h>
#include "gnumeric.h"
#include "style.h"
#include "format.h"
#include "color.h"
#include "application.h"
#include "gnumeric-util.h"

#include <gal/widgets/e-colors.h>

#undef DEBUG_REF_COUNT
#undef DEBUG_FONTS

static GHashTable *style_font_hash;
static GHashTable *style_font_negative_hash;
static GHashTable *style_color_hash;

StyleFont *gnumeric_default_font;
StyleFont *gnumeric_default_bold_font;
StyleFont *gnumeric_default_italic_font;

StyleFont *
style_font_new_simple (const char *font_name, double size, double scale,
		       gboolean bold, gboolean italic)
{
	StyleFont *font;
	StyleFont key;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (size != 0, NULL);

	/* This cast does not mean we will change the name.  */
	key.font_name = (char *)font_name;
	key.size      = size;
	key.is_bold   = bold;
	key.is_italic = italic;
	key.scale     = scale;

	font = (StyleFont *) g_hash_table_lookup (style_font_hash, &key);
	if (!font){
		if (g_hash_table_lookup (style_font_negative_hash, &key))
			return NULL;

		font = g_new0 (StyleFont, 1);
		font->font_name = g_strdup (font_name);
		font->size      = size;
		font->scale     = scale;
		font->is_bold   = bold;
		font->is_italic = italic;

		font->dfont = gnome_get_display_font (
			font_name,
			bold ? GNOME_FONT_BOLD : GNOME_FONT_BOOK,
			italic,
			size, scale);

		if (!font->dfont) {
			g_hash_table_insert (style_font_negative_hash,
					     font, font);
			return NULL;
		}

		/* Worst case scenario */
		font->gdk_font = gnome_display_font_get_gdk_font (font->dfont);
		if (font->gdk_font == NULL)
			font->gdk_font = gdk_font_load ("fixed");
		else
			gdk_font_ref (font->gdk_font);

		font->font = gnome_font_new_closest (
			font_name,
			bold ? GNOME_FONT_BOLD : GNOME_FONT_BOOK,
			italic,
			size);

		g_hash_table_insert (style_font_hash, font, font);
	}

	font->ref_count++;

	/* FIXME : how does one get the width of the
	 * widest character used to display numbers.
	 * Use 4 as the max width for now.  Count the
	 * inter character spacing by measuring a
	 * string with 10 digits
	 */
	font->approx_width = font->gdk_font
		? gnome_font_get_width_string (font->font, "4444444444") / 10.
		: 1.;

#ifdef DEBUG_REF_COUNT
	fprintf (stderr, __FUNCTION__ " font=%p name=%s%s%s ref_count=%d\n",
		 font, font->font_name,
		 font->is_bold ? " bold" : "",
		 font->is_italic ? " italic" : "",
		 font->ref_count);
#endif
	return font;
}

StyleFont *
style_font_new (const char *font_name, double size, double scale,
		gboolean bold, gboolean italic)
{
	StyleFont *font;

	g_return_val_if_fail (font_name != NULL, NULL);
	g_return_val_if_fail (size != 0, NULL);

	font = style_font_new_simple (font_name, size, scale, bold, italic);
	if (!font){
		if (bold)
			font = gnumeric_default_bold_font;
		else if (italic)
			font = gnumeric_default_italic_font;
		else
			font = gnumeric_default_font;
		style_font_ref (font);
	}

	return font;
}

GdkFont *
style_font_gdk_font (StyleFont const * const sf)
{
	g_return_val_if_fail (sf != NULL, NULL);

	return sf->gdk_font;
}

int
style_font_get_height (StyleFont const * const sf)
{
	g_return_val_if_fail (sf != NULL, 0);

	return gnome_display_font_height (sf->dfont);
}

float
style_font_get_width (StyleFont const *sf)
{
	g_return_val_if_fail (sf != NULL, 0);

	return sf->approx_width;
}

void
style_font_ref (StyleFont *sf)
{
	g_return_if_fail (sf != NULL);

	sf->ref_count++;
#ifdef DEBUG_REF_COUNT
	fprintf (stderr, __FUNCTION__ " font=%p name=%s%s%s ref_count=%d\n",
		 sf, sf->font_name,
		 sf->is_bold ? " bold" : "",
		 sf->is_italic ? " italic" : "",
		 sf->ref_count);
#endif
}

void
style_font_unref (StyleFont *sf)
{
	g_return_if_fail (sf != NULL);
	g_return_if_fail (sf->ref_count > 0);

	sf->ref_count--;
#ifdef DEBUG_REF_COUNT
	fprintf (stderr, __FUNCTION__ " font=%p name=%s%s%s ref_count=%d\n",
		 sf, sf->font_name,
		 sf->is_bold ? " bold" : "",
		 sf->is_italic ? " italic" : "",
		 sf->ref_count);
#endif
	if (sf->ref_count != 0)
		return;

	gtk_object_unref (GTK_OBJECT (sf->font));
	gdk_font_unref (sf->gdk_font);

	g_hash_table_remove (style_font_hash, sf);
	g_free (sf->font_name);
	g_free (sf);
}

StyleColor *
style_color_new (gushort red, gushort green, gushort blue)
{
	StyleColor *sc;
	StyleColor key;

	key.red   = red;
	key.green = green;
	key.blue  = blue;

	sc = g_hash_table_lookup (style_color_hash, &key);
	if (!sc) {
		sc = g_new (StyleColor, 1);

		key.color.red = red;
		key.color.green = green;
		key.color.blue = blue;
		sc->color = key.color;
		sc->red = red;
		sc->green = green;
		sc->blue = blue;
		sc->name = NULL;
		sc->color.pixel = e_color_alloc (red, green, blue);

		/* Make a contrasting selection color with an alpha of .5 */
		red   += (gs_lavender.red   - red)/2;
		green += (gs_lavender.green - green)/2;
		blue  += (gs_lavender.blue  - blue)/2;
		sc->selected_color.red = red;
		sc->selected_color.green = green;
		sc->selected_color.blue = blue;
		sc->selected_color.pixel = e_color_alloc (red, green, blue);

		g_hash_table_insert (style_color_hash, sc, sc);
		sc->ref_count = 0;
	}
	sc->ref_count++;

	return sc;
}

StyleColor *
style_color_black (void)
{
	static StyleColor *color = NULL;

	if (!color)
		color = style_color_new (0, 0, 0);
	return style_color_ref (color);
}

StyleColor *
style_color_white (void)
{
	static StyleColor *color = NULL;

	if (!color)
		color = style_color_new (0xffff, 0xffff, 0xffff);
	return style_color_ref (color);
}

StyleColor *
style_color_ref (StyleColor *sc)
{
	g_return_val_if_fail (sc != NULL, NULL);

	sc->ref_count++;

	return sc;
}

void
style_color_unref (StyleColor *sc)
{
	g_return_if_fail (sc != NULL);
	g_return_if_fail (sc->ref_count > 0);

	sc->ref_count--;
	if (sc->ref_count != 0)
		return;

	/*
	 * There is no need to deallocate colors, as they come from
	 * the GDK Color Context
	 */
	g_hash_table_remove (style_color_hash, sc);
	g_free (sc);
}

/*
 * The routines used to hash and compare the different styles
 */
gint
style_font_equal (gconstpointer v, gconstpointer v2)
{
	const StyleFont *k1 = (const StyleFont *) v;
	const StyleFont *k2 = (const StyleFont *) v2;

	if (k1->size != k2->size)
		return 0;

	if (k1->is_bold != k2->is_bold)
		return 0;
	if (k1->is_italic != k2->is_italic)
		return 0;
	if (k1->scale != k2->scale)
		return 0;

	return !strcmp (k1->font_name, k2->font_name);
}

guint
style_font_hash_func (gconstpointer v)
{
	const StyleFont *k = (const StyleFont *) v;

	return k->size + g_str_hash (k->font_name);
}

static gint
color_equal (gconstpointer v, gconstpointer v2)
{
	const StyleColor *k1 = (const StyleColor *) v;
	const StyleColor *k2 = (const StyleColor *) v2;

	if (k1->red   == k2->red &&
	    k1->green == k2->green &&
	    k1->blue  == k2->blue)
		return 1;

	return 0;
}

static guint
color_hash (gconstpointer v)
{
	const StyleColor *k = (const StyleColor *)v;

	return (k->red << 16) | (k->green << 8) | (k->blue);
}

static void
font_init (void)
{
	double const scale  = application_dpi_to_pixels ();
	gnumeric_default_font = style_font_new_simple (DEFAULT_FONT, DEFAULT_SIZE,
						       scale, FALSE, FALSE);

	if (!gnumeric_default_font) {
		char *lc_all = getenv ("LC_ALL");
		char *lang = getenv ("LANG");
		char *msg;
		char *fontmap_fn = gnome_datadir_file ("fonts/fontmap");
		gboolean exists = (fontmap_fn != NULL);

		if (!exists)
			fontmap_fn = gnome_unconditional_datadir_file ("fonts/fontmap");

		if (lc_all == NULL)
			lc_all = _("<Has not been set>");
		if (lang == NULL)
			lang = _("<Has not been set>");

		msg = g_strdup_printf (
			_("Gnumeric failed to find a suitable default font.\n"
			"Please verify your gnome-print installation.\n"
			"Your fontmap file %s.\n"
			"\n"
			"If you still have no luck, please file a proper bug report (see\n"
			"http://bugs.gnome.org) including the following extra items:\n"
			"\n"
			"1) The content of your fontmap file, if the file exists.\n"
			"\t (typically located in %s)\n"
			"2) The value of the LC_ALL environment variable\n"
			"\tLC_ALL=%s\n"
			"3) The value of the LANG environment variable\n"
			"\tLANG=%s\n"
			"4) What version of libxml gnumeric is running with.\n"
			"   You may be able to use the 'ldd' command to get that information.\n"
			"\n"
			"Thanks -- the Gnumeric Team\n"), exists
			? _("does not have a valid entry for Helvetica")
			: _("could not be found in the expected location"),
			fontmap_fn, lc_all, lang);
		gnumeric_notice (NULL, GNOME_MESSAGE_BOX_ERROR, msg);
		exit (1);
	}

	/*
	 * Load bold font
	 */
	gnumeric_default_bold_font = style_font_new_simple (
		DEFAULT_FONT, DEFAULT_SIZE, scale, TRUE, FALSE);
	if (gnumeric_default_bold_font == NULL){
	    gnumeric_default_bold_font = gnumeric_default_font;
	    style_font_ref (gnumeric_default_bold_font);
	}

	/*
	 * Load italic font
	 */
	gnumeric_default_italic_font = style_font_new_simple (
		DEFAULT_FONT, DEFAULT_SIZE, scale, FALSE, TRUE);
	if (gnumeric_default_italic_font == NULL){
		gnumeric_default_italic_font = gnumeric_default_font;
		style_font_ref (gnumeric_default_italic_font);
	}
}

void
style_init (void)
{
	number_format_init ();
	style_font_hash   = g_hash_table_new (style_font_hash_func,
					      style_font_equal);
	style_color_hash  = g_hash_table_new (color_hash, color_equal);

	style_font_negative_hash = g_hash_table_new (style_font_hash_func,
						     style_font_equal);

	font_init ();
}

static void
delete_neg_font (gpointer key, gpointer value, gpointer user_data)
{
	StyleFont *font = key;

	g_free (font->font_name);
	g_free (font);
}

/*
 * Release all resources allocated by style_init.
 */
void
style_shutdown (void)
{
	if (gnumeric_default_font != gnumeric_default_bold_font &&
	    gnumeric_default_bold_font->ref_count != 1) {
		g_warning ("Default bold font has %d references.  It should have only one.",
			   gnumeric_default_bold_font->ref_count);
	}
	style_font_unref (gnumeric_default_bold_font);
	gnumeric_default_bold_font = NULL;

	if (gnumeric_default_font != gnumeric_default_italic_font &&
	    gnumeric_default_italic_font->ref_count != 1) {
		g_warning ("Default italic font has %d references.  It should have only one.",
			   gnumeric_default_italic_font->ref_count);
	}
	style_font_unref (gnumeric_default_italic_font);
	gnumeric_default_italic_font = NULL;

	/* At this point, even if we had bold == normal (etc), we should
	   have exactly one reference to default.  */
	if (gnumeric_default_font->ref_count != 1) {
		g_warning ("Default font has %d references.  It should have only one.",
			   gnumeric_default_font->ref_count);
	}
	style_font_unref (gnumeric_default_font);
	gnumeric_default_font = NULL;

	number_format_shutdown ();
	g_hash_table_destroy (style_font_hash);
	style_font_hash = NULL;
	g_hash_table_destroy (style_color_hash);
	style_color_hash = NULL;

	g_hash_table_foreach (style_font_negative_hash, delete_neg_font, NULL);
	g_hash_table_destroy (style_font_negative_hash);
	style_font_negative_hash = NULL;
}
