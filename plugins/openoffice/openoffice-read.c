/* vim: set sw=8: */

/*
 * openoffice-read.c : import open/star calc files
 *
 * Copyright (C) 2002-2005 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2006 Luciano Miguel Wolf (luciano.wolf@indt.org.br)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <gnumeric-config.h>
#include <gnumeric.h>

#include <gnm-plugin.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-merge.h>
#include <ranges.h>
#include <cell.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <parse-util.h>
#include <style-color.h>
#include <sheet-style.h>
#include <mstyle.h>
#include <style-border.h>
#include <gnm-format.h>
#include <command-context.h>
#include <goffice/app/io-context.h>
#include <goffice/utils/go-units.h>
#include <goffice/utils/datetime.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-zip.h>
#include <gsf/gsf-opendoc-utils.h>
#include <gsf/gsf-doc-meta-data.h>
#include <glib/gi18n.h>

#include <string.h>
#include <locale.h>

#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-plot-impl.h>
#include <goffice/data/go-data-simple.h>
#include <sheet-object-graph.h>
#include <sheet-object-image.h>
#include <graph.h>

GNM_PLUGIN_MODULE_HEADER;

/* Filter Type */
#define mime_openofficeorg1	"application/vnd.sun.xml.calc"
#define mime_opendocument	"application/vnd.oasis.opendocument.spreadsheet"

typedef enum {
	OOO_VER_UNKNOW	= -1,
	OOO_VER_1	=  0,
	OOO_VER_OPENDOC	=  1
} OOVer;

#define OD_BORDER_THIN		1
#define OD_BORDER_MEDIUM	2.5
#define OD_BORDER_THICK		5

typedef enum {
	AREA,
	BAR,
	CIRCLE,
	LINE,
	RADAR,
	RADARAREA,
	RING,
	SCATTER,
	STOCK,
	SURF,
	UNKNOWN
} ODChartType;

typedef enum {
	OO_STYLE_UNKNOWN,
	OO_STYLE_CELL,
	OO_STYLE_COL,
	OO_STYLE_ROW,
	OO_STYLE_SHEET,
	OO_STYLE_GRAPHICS,
	OO_STYLE_CHART,
	OO_STYLE_PARAGRAPH,
	OO_STYLE_TEXT
} OOStyleType;

typedef struct {
	gchar *name;	/* property name */
	GValue *value;	/* property value */
} ODProps;		/* struct to hold axis properties inside a GSList, e.g.: is logarithmic? */

typedef struct {
	gboolean grid;		/* graph has grid? */
	gboolean row_src;	/* orientation of graph data: rows or columns */
	GSList *axis;		/* axis properties */
	GSList *chart;		/* chart properties */
} ODGraphProperties;

typedef struct {
	GogGraph *graph;		/* current graph */
	ODGraphProperties *cur_graph_style;
	GHashTable *graph_styles;	/* contain links to ODGraphProperties GSLists */
	GogChart *chart;		/* current chart */
	ODChartType chart_type;
	SheetObjectAnchor anchor;	/* anchor to draw the frame (images or graphs) */
	gboolean has_legend;
	GogObjectPosition legend;
	GogAxisType cur_axis;
} ODFrameProperties;

typedef struct {
	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	OOVer		 ver;		/* Its an OOo v1.0 or v2.0? */
	GsfInfile	*zip;		/* Reference to the open file, to load graphs and images*/
	GSList	*col_styles[2];		/* [0] style information, [1] the column starting with this style*/
	ODFrameProperties cur_frame;
	GnmParsePos 	pos;

	int 		 col_inc, row_inc;
	gboolean 	 simple_content;
	gboolean 	 error_content;
	GHashTable	*cell_styles;
	GHashTable	*col_row_styles;
	GHashTable	*table_styles;
	GHashTable	*formats;

	union {
		GnmStyle *cell;
		double	 *col_row;
	} cur_style;
	gboolean	 h_align_is_valid, repeat_content;
	GnmStyle 	*default_style_cell;
	OOStyleType	 cur_style_type;
	GSList		*sheet_order;
	int	 	 richtext_len;
	GString		*accum_fmt;
	char		*fmt_name;

	GnmExprConventions *exprconv;
} OOParseState;

static GsfXMLInNode const opendoc_content_dtd [];
static void clean_lists (ODGraphProperties *pointer);
static ODProps *dup_prop (ODProps *pointer);

static gboolean oo_warning (GsfXMLIn *xin, char const *fmt, ...)
	G_GNUC_PRINTF (2, 3);

static gboolean
oo_warning (GsfXMLIn *xin, char const *fmt, ...)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char *msg;
	va_list args;

	va_start (args, fmt);
	msg = g_strdup_vprintf (fmt, args);
	va_end (args);

	if (IS_SHEET (state->pos.sheet)) {
		char *tmp;
		if (state->pos.eval.col >= 0 && state->pos.eval.row >= 0)
			tmp = g_strdup_printf ("%s!%s : %s",
				state->pos.sheet->name_quoted,
				cellpos_as_string (&state->pos.eval), msg);
		else
			tmp = g_strdup_printf ("%s : %s",
				state->pos.sheet->name_quoted, msg);
		g_free (msg);
		msg = tmp;
	}

	gnm_io_warning (state->context, "%s", msg);
	g_free (msg);

	return FALSE; /* convenience */
}

static gboolean
oo_attr_bool (GsfXMLIn *xin, xmlChar const * const *attrs,
	      int ns_id, char const *name, gboolean *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;
	*res = g_ascii_strcasecmp ((gchar *)attrs[1], "false") && strcmp (attrs[1], "0");

	return TRUE;
}

static gboolean
oo_attr_int (GsfXMLIn *xin, xmlChar const * const *attrs,
	     int ns_id, char const *name, int *res)
{
	char *end;
	int tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;

	tmp = strtol ((gchar *)attrs[1], &end, 10);
	if (*end)
		return oo_warning (xin, "Invalid attribute '%s', expected integer, received '%s'",
				   name, attrs[1]);

	*res = tmp;
	return TRUE;
}

static gboolean
oo_attr_float (GsfXMLIn *xin, xmlChar const * const *attrs,
	       int ns_id, char const *name, gnm_float *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;

	tmp = gnm_strto ((gchar *)attrs[1], &end);
	if (*end)
		return oo_warning (xin, "Invalid attribute '%s', expected number, received '%s'",
				   name, attrs[1]);
	*res = tmp;
	return TRUE;
}


static GnmColor *
oo_parse_color (GsfXMLIn *xin, xmlChar const *str, char const *name)
{
	guint r, g, b;
	GnmColor *no_color;

	g_return_val_if_fail (str != NULL, NULL);

	if (3 == sscanf (str, "#%2x%2x%2x", &r, &g, &b))
		return style_color_new_i8 (r, g, b);

	if (0 == strcmp (str, "transparent")) {
		no_color = style_color_auto_back ();
		no_color->name = g_new (gchar, 1);
		no_color->name = g_strdup ("transparent");
		return no_color;
	}
	oo_warning (xin, "Invalid attribute '%s', expected color, received '%s'",
		    name, str);
	return NULL;
}
static GnmColor *
oo_attr_color (GsfXMLIn *xin, xmlChar const * const *attrs,
	       int ns_id, char const *name)
{
	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return NULL;
	return oo_parse_color (xin, attrs[1], name);
}
/* returns pts */
static char const *
oo_parse_distance (GsfXMLIn *xin, xmlChar const *str,
		  char const *name, double *pts)
{
	double num;
	char *end = NULL;

	g_return_val_if_fail (str != NULL, NULL);

	if (0 == strncmp (str, "none", 2)) {
		*pts = 0;
		return str + 4;
	}

	num = strtod (str, &end);
	if (str != (xmlChar const *)end) {
		if (0 == strncmp (end, "mm", 2)) {
			num = GO_CM_TO_PT (num/10.);
			end += 2;
		} else if (0 == strncmp (end, "m", 1)) {
			num = GO_CM_TO_PT (num*100.);
			end ++;
		} else if (0 == strncmp (end, "km", 2)) {
			num = GO_CM_TO_PT (num*100000.);
			end += 2;
		} else if (0 == strncmp (end, "cm", 2)) {
			num = GO_CM_TO_PT (num);
			end += 2;
		} else if (0 == strncmp (end, "pt", 2)) {
			end += 2;
		} else if (0 == strncmp (end, "pc", 2)) { /* pica 12pt == 1 pica */
			num /= 12.;
			end += 2;
		} else if (0 == strncmp (end, "ft", 2)) {
			num = GO_IN_TO_PT (num*12.);
			end += 2;
		} else if (0 == strncmp (end, "mi", 2)) {
			num = GO_IN_TO_PT (num*63360.);
			end += 2;
		} else if (0 == strncmp (end, "inch", 4)) {
			num = GO_IN_TO_PT (num);
			end += 4;
		} else if (0 == strncmp (end, "in", 2)) {
			num = GO_IN_TO_PT (num);
			end += 2;
		} else {
			oo_warning (xin, "Invalid attribute '%s', unknown unit '%s'",
				    name, str);
			return NULL;
		}
	} else {
		oo_warning (xin, "Invalid attribute '%s', expected distance, received '%s'",
			    name, str);
		return NULL;
	}

	*pts = num;
	return end;
}
/* returns pts */
static char const *
oo_attr_distance (GsfXMLIn *xin, xmlChar const * const *attrs,
		  int ns_id, char const *name, double *pts)
{
	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);
	g_return_val_if_fail (attrs[1] != NULL, NULL);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return NULL;
	return oo_parse_distance (xin, attrs[1], name, pts);
}

typedef struct {
	char const * const name;
	int val;
} OOEnum;

static gboolean
oo_attr_enum (GsfXMLIn *xin, xmlChar const * const *attrs,
	      int ns_id, char const *name, OOEnum const *enums, int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, attrs[0], ns_id, name))
		return FALSE;

	for (; enums->name != NULL ; enums++)
		if (!strcmp (enums->name, attrs[1])) {
			*res = enums->val;
			return TRUE;
		}
	return oo_warning (xin, "Invalid attribute '%s', unknown enum value '%s'",
			   name, attrs[1]);
}

#warning "FIXME: expand this later."
#define oo_expr_parse_str(str, pp, flags, err)	\
	gnm_expr_parse_str (str, pp,		\
		flags, state->exprconv, err)

/****************************************************************************/

static void
oo_date_convention (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* <table:null-date table:date-value="1904-01-01"/> */
	OOParseState *state = (OOParseState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "date-value")) {
			if (!strncmp (attrs[1], "1904", 4))
				workbook_set_1904 (state->pos.wb, TRUE);
		}
}

static void
oo_table_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* <table:table table:name="Result" table:style-name="ta1"> */
	OOParseState *state = (OOParseState *)xin->user_state;

	state->pos.eval.col = 0;
	state->pos.eval.row = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "name")) {
			state->pos.sheet = workbook_sheet_by_name (state->pos.wb, attrs[1]);
			if (NULL == state->pos.sheet) {
				state->pos.sheet = sheet_new (state->pos.wb, attrs[1]);
				workbook_sheet_attach (state->pos.wb, state->pos.sheet);
			}

			/* store a list of the sheets in the correct order */
			state->sheet_order = g_slist_prepend (state->sheet_order,
							      state->pos.sheet);
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "style-name"))  {
			/* hidden & rtl vs ltr */
		}
}

static void
oo_col_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmStyle *style = NULL;
	double   *width_pts = NULL;
	int repeat_count = 1;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "default-cell-style-name"))
		{
			style = g_hash_table_lookup (state->cell_styles, attrs[1]);
			state->col_styles[0] = g_slist_append (state->col_styles[0], g_strdup(attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "style-name"))
			width_pts = g_hash_table_lookup (state->col_row_styles, attrs[1]);
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-repeated", &repeat_count))
			;

	state->col_styles[1] = g_slist_append (state->col_styles[1], GINT_TO_POINTER (repeat_count));

	while (repeat_count-- > 0) {
		if (width_pts != NULL)
			sheet_col_set_size_pts (state->pos.sheet,
				state->pos.eval.col++, *width_pts, TRUE);
	}
}

static void
oo_row_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	int	 i, repeat_count = 1;
	double  *height_pts = NULL;

	state->pos.eval.col = 0;

	g_return_if_fail (state->pos.eval.row < SHEET_MAX_ROWS);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "style-name"))
			height_pts = g_hash_table_lookup (state->col_row_styles, attrs[1]);
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-rows-repeated", &repeat_count))
			;
	}
	if (height_pts != NULL)
		for (i = repeat_count; i-- > 0 ; )
			sheet_row_set_size_pts (state->pos.sheet,
				state->pos.eval.row + i, *height_pts, TRUE);
	state->row_inc = repeat_count;
}
static void
oo_row_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->pos.eval.row += state->row_inc;
}

static char const *
oo_cellref_parse (GnmCellRef *ref, char const *start, GnmParsePos const *pp)
{
	char const *tmp1, *tmp2, *ptr = start;
	/* sheet name cannot contain '.' */
	if (*ptr != '.') {
		char *name;
		if (*ptr == '$') /* ignore abs vs rel sheet name */
			ptr++;
		tmp1 = strchr (ptr, '.');
		if (tmp1 == NULL)
			return start;
		name = g_alloca (tmp1-ptr+1);
		strncpy (name, ptr, tmp1-ptr);
		name[tmp1-ptr] = 0;
		ptr = tmp1+1;

		/* OpenCalc does not pre-declare its sheets, but it does have a
		 * nice unambiguous format.  So if we find a name that has not
		 * been added yet add it.  Reorder below.
		 */
		ref->sheet = workbook_sheet_by_name (pp->wb, name);
		if (ref->sheet == NULL) {
			ref->sheet = sheet_new (pp->wb, name);
			workbook_sheet_attach (pp->wb, ref->sheet);
		}
	} else {
		ptr++; /* local ref */
		ref->sheet = NULL;
	}

	tmp1 = col_parse (ptr, &ref->col, &ref->col_relative);
	if (!tmp1)
		return start;
	tmp2 = row_parse (tmp1, &ref->row, &ref->row_relative);
	if (!tmp2)
		return start;

	if (ref->col_relative)
		ref->col -= pp->eval.col;
	if (ref->row_relative)
		ref->row -= pp->eval.row;
	return tmp2;
}

static char const *
oo_rangeref_parse (GnmRangeRef *ref, char const *start, GnmParsePos const *pp,
		   GnmExprConventions const *convention)
{
	char const *ptr;

	g_return_val_if_fail (start != NULL, start);
	g_return_val_if_fail (pp != NULL, start);

	if (*start != '[')
		return start;
	ptr = oo_cellref_parse (&ref->a, start+1, pp);
	if (*ptr == ':')
		ptr = oo_cellref_parse (&ref->b, ptr+1, pp);
	else
		ref->b = ref->a;

	if (*ptr == ']')
		return ptr + 1;
	return start;
}

static void
oo_cell_content_span_start (GsfXMLIn *xin, xmlChar const **attrs)
{
}

static void
oo_cell_content_span_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
}

static void
oo_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmExpr const	*expr = NULL;
	GnmValue		*val = NULL;
	gboolean	 bool_val;
	gnm_float	 float_val = 0;
	int array_cols = -1, array_rows = -1;
	int merge_cols = -1, merge_rows = -1;
	GnmStyle *style = NULL;
	char const *expr_string;
	GSList *search_str = NULL;
	gint cont_col, sum_cols, cont, cur_col=0;
	GnmRange tmp;

	state->col_inc = 1;
	state->error_content = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-repeated", &state->col_inc))
			;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "formula")) {
			if (attrs[1] == NULL) {
				oo_warning (xin, _("Missing expression"));
				continue;
			}

			expr_string = attrs[1];
			if (state->ver == OOO_VER_OPENDOC) {
				if (strncmp (expr_string, "oooc:", 5)) {
					oo_warning (xin, _("Missing expression namespace"));
					continue;
				}
				expr_string += 5;
			}

			expr_string = gnm_expr_char_start_p (expr_string);
			if (expr_string == NULL)
				oo_warning (xin, _("Expression '%s' does not start with a recognized character"), attrs[1]);
			else if (*expr_string == '\0')
				/* Ick.  They seem to store error cells as
				 * having value date with expr : '=' and the
				 * message in the content.
				 */
				state->error_content = TRUE;
			else {
				GnmParseError  perr;
				parse_error_init (&perr);
				expr = oo_expr_parse_str (expr_string,
					&state->pos, GNM_EXPR_PARSE_DEFAULT, &perr);
				if (expr == NULL) {
					g_print ("s=[%s]\n", expr_string);
					g_print ("e=[%s]\n", perr.err->message);
					oo_warning (xin, _("Unable to parse\n\t'%s'\nbecause '%s'"),
						    attrs[1], perr.err->message);
					parse_error_free (&perr);
				}
			}
		} else if (oo_attr_bool (xin, attrs,
					 (state->ver == OOO_VER_OPENDOC) ? OO_NS_OFFICE : OO_NS_TABLE,
					 "boolean-value", &bool_val))
			val = value_new_bool (bool_val);
		else if (gsf_xml_in_namecmp (xin, attrs[0],
			(state->ver == OOO_VER_OPENDOC) ? OO_NS_OFFICE : OO_NS_TABLE,
			"date-value")) {
			unsigned y, m, d, h, mi;
			float s;
			unsigned n = sscanf (attrs[1], "%u-%u-%uT%u:%u:%g",
					     &y, &m, &d, &h, &mi, &s);

			if (n >= 3) {
				GDate date;
				g_date_set_dmy (&date, d, m, y);
				if (g_date_valid (&date)) {
					unsigned d_serial = datetime_g_to_serial (&date,
						workbook_date_conv (state->pos.wb));
					if (n >= 6) {
						double time_frac = h + ((double)mi / 60.) + ((double)s / 3600.);
						val = value_new_float (d_serial + time_frac / 24.);
					} else
						val = value_new_int (d_serial);
				}
			}
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "time-value")) {
			unsigned h, m, s;
			if (3 == sscanf (attrs[1], "PT%uH%uM%uS", &h, &m, &s))
				val = value_new_float (h + ((double)m / 60.) + ((double)s / 3600.));
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "string-value"))
			val = value_new_string (attrs[1]);
		else if (oo_attr_float (xin, attrs,
			(state->ver == OOO_VER_OPENDOC) ? OO_NS_OFFICE : OO_NS_TABLE,
			"value", &float_val))
			val = value_new_float (float_val);
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-matrix-columns-spanned", &array_cols))
			;
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-matrix-rows-spanned", &array_rows))
			;
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-spanned", &merge_cols))
			;
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-rows-spanned", &merge_rows))
			;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "style-name")) {
			style = g_hash_table_lookup (state->cell_styles, attrs[1]);
		}
	}

	if (style != NULL) {
		gnm_style_ref (style);
		if (state->col_inc > 1 || state->row_inc > 1) {
			range_init (&tmp,
				state->pos.eval.col, state->pos.eval.row,
				state->pos.eval.col + state->col_inc - 1,
				state->pos.eval.row + state->row_inc - 1);
			sheet_style_set_range (state->pos.sheet, &tmp, style);
		} else if (merge_cols > 0 || merge_rows > 0) {
			GnmRange tmp;
			range_init (&tmp,
				state->pos.eval.col, state->pos.eval.row,
				state->pos.eval.col + merge_cols - 1,
				state->pos.eval.row + merge_rows - 1);
			sheet_style_set_range (state->pos.sheet, &tmp, style);
		} else
			sheet_style_set_pos (state->pos.sheet,
				state->pos.eval.col, state->pos.eval.row,
				style);
	} else if (g_slist_length (state->col_styles[1]) > 0) {
		for (cont_col = 0; cont_col < state->col_inc; cont_col++) {
			cur_col = (state->pos.eval.col) + cont_col;
			sum_cols = -1;
			cont = 0;
			while (sum_cols < cur_col){
				sum_cols += GPOINTER_TO_INT(g_slist_nth_data(state->col_styles[1], cont));
				cont ++;
			}

			search_str = g_slist_nth (state->col_styles[0], cont - 1);

			if (NULL != search_str){
				style = g_hash_table_lookup (state->cell_styles, search_str->data);
				gnm_style_ref (style);
				if (state->row_inc > 1){
					GnmRange style_range;
					range_init (&style_range,
						state->pos.eval.col+cont_col, state->pos.eval.row,
						state->pos.eval.col+cont_col,
						state->pos.eval.row + state->row_inc - 1);
					sheet_style_set_range (state->pos.sheet, &style_range, style);
				} else
					sheet_style_set_pos (state->pos.sheet,
						state->pos.eval.col+cont_col, state->pos.eval.row,
						style);
			}
		}
	}

	state->simple_content = FALSE;
	if (expr != NULL) {
		GnmCell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		if (array_cols > 0 || array_rows > 0) {
			if (array_cols < 0) {
				array_cols = 1;
				oo_warning (xin, _("Invalid array expression does not specify number of columns."));
			} else if (array_rows < 0) {
				array_rows = 1;
				oo_warning (xin, _("Invalid array expression does not specify number of rows."));
			}
			cell_set_array_formula (state->pos.sheet,
				state->pos.eval.col, state->pos.eval.row,
				state->pos.eval.col + array_cols-1,
				state->pos.eval.row + array_rows-1,
				expr);
			if (val != NULL)
				cell_assign_value (cell, val);
		} else {
			if (val != NULL)
				cell_set_expr_and_value (cell, expr, val, TRUE);
			else
				cell_set_expr (cell, expr);
			gnm_expr_unref (expr);
		}
	} else if (val != NULL) {
		GnmCell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		/* has cell previously been initialized as part of an array */
		if (cell_is_nonsingleton_array (cell))
			cell_assign_value (cell, val);
		else
			cell_set_value (cell, val);
	} else if (!state->error_content)
		/* store the content as a string */
		state->simple_content = TRUE;

	if (merge_cols > 0 && merge_rows > 0) {
		GnmRange r;
		range_init (&r,
			state->pos.eval.col, state->pos.eval.row,
			state->pos.eval.col + merge_cols - 1,
			state->pos.eval.row + merge_rows - 1);
		sheet_merge_add (state->pos.sheet, &r, FALSE,
				 NULL);
	}
}

static void
oo_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->col_inc > 1) {
		GnmCell *cell = sheet_cell_get (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		if (!cell_is_empty (cell)) {
			int i = 1;
			GnmCell *next;
			for (; i < state->col_inc ; i++) {
				next = sheet_cell_fetch (state->pos.sheet,
					state->pos.eval.col + i, state->pos.eval.row);
				cell_set_value (next, value_dup (cell->value));
			}
		}
	}

	state->pos.eval.col += state->col_inc;
}

static void
oo_covered_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->col_inc = 1;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-repeated", &state->col_inc))
			;
#if 0
		/* why bother it is covered ? */
		else if (!strcmp (attrs[0], OO_NS_TABLE, "style-name"))
			style = g_hash_table_lookup (state->cell_styles, attrs[1]);

	if (style != NULL) {
		gnm_style_ref (style);
		sheet_style_set_pos (state->pos.sheet,
		     state->pos.eval.col, state->pos.eval.row,
		     style);
	}
#endif
}

static void
oo_covered_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->pos.eval.col += state->col_inc;
}

static void
oo_cell_content_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->simple_content || state->error_content) {
		GnmValue *v;
		GnmCell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		if (state->simple_content)
			v = value_new_string (xin->content->str);
		else
			v = value_new_error (NULL, xin->content->str);
		cell_set_value (cell, v);
	}
}

static void
oo_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const style_types [] = {
		{ "table-cell",	  OO_STYLE_CELL },
		{ "table-row",	  OO_STYLE_ROW },
		{ "table-column", OO_STYLE_COL },
		{ "table",	  OO_STYLE_SHEET },
		{ "graphics",	  OO_STYLE_GRAPHICS },
		{ "paragraph",	  OO_STYLE_PARAGRAPH },
		{ "text",	  OO_STYLE_TEXT },
		{ "chart",	  OO_STYLE_CHART },
		{ "graphic",	  OO_STYLE_GRAPHICS },
		{ NULL,	0 },
	};

	OOParseState *state = (OOParseState *)xin->user_state;
	xmlChar const *name = NULL;
	xmlChar const *parent_name = NULL;
	GnmStyle *style;
	GOFormat *fmt = NULL;
	int tmp;
	ODGraphProperties *cur_style;

	g_return_if_fail (state->cur_style_type == OO_STYLE_UNKNOWN);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "family", style_types, &tmp))
			state->cur_style_type = tmp;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "name"))
			name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "parent-style-name"))
			parent_name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "data-style-name")) {
			GOFormat *tmp = g_hash_table_lookup (state->formats, attrs[1]);
			if (tmp != NULL)
				fmt = tmp;
		}

	switch (state->cur_style_type) {
	case OO_STYLE_CELL:
		style = (parent_name != NULL)
			? g_hash_table_lookup (state->cell_styles, parent_name)
			: NULL;
		state->cur_style.cell = (style != NULL)
			? gnm_style_dup (style) : gnm_style_new_default ();
		state->h_align_is_valid = state->repeat_content = FALSE;

		if (fmt != NULL)
			gnm_style_set_format (state->cur_style.cell, fmt);

		if (name != NULL)
			g_hash_table_replace (state->cell_styles,
					      g_strdup (name),
					      state->cur_style.cell);
		else if (0 == strcmp (xin->node->id, "DEFAULT_STYLE")) {
			 if (state->default_style_cell)
				 gnm_style_unref (state->default_style_cell);
			 state->default_style_cell = state->cur_style.cell;
		}
		break;

	case OO_STYLE_COL:
	case OO_STYLE_ROW:
		state->cur_style.col_row = g_new0 (double, 1);
		if (name)
			g_hash_table_replace (state->col_row_styles,
					      g_strdup (name),
					      state->cur_style.col_row);
		break;

	case OO_STYLE_CHART:
		if (name != NULL){
			cur_style = g_new0(ODGraphProperties, 1);
			cur_style->axis = NULL;
			cur_style->chart = NULL;
			state->cur_frame.cur_graph_style = cur_style;
			state->cur_frame.chart_type = UNKNOWN;
			g_hash_table_replace (state->cur_frame.graph_styles,
					      g_strdup (name),
					      state->cur_frame.cur_graph_style);
		}
		break;
	default:
		break;
	}
}

static void
oo_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	switch (state->cur_style_type) {
	case OO_STYLE_CELL : state->cur_style.cell = NULL;
		break;
	case OO_STYLE_COL :
	case OO_STYLE_ROW : state->cur_style.col_row = NULL;
		break;
	case OO_STYLE_CHART : state->cur_frame.cur_graph_style = NULL;
		break;

	default :
		break;
	}
	state->cur_style_type = OO_STYLE_UNKNOWN;
}

static void
oo_date_day (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");

	g_string_append (state->accum_fmt, is_short ? "d" : "dd");
}
static void
oo_date_month (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean as_text = FALSE;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");
		else if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "textual", &as_text))
			;
	g_string_append (state->accum_fmt, as_text
			 ? (is_short ? "mmm" : "mmmm")
			 : (is_short ? "m" : "mm"));
}
static void
oo_date_year (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");
	g_string_append (state->accum_fmt, is_short ? "yy" : "yyyy");
}
static void
oo_date_era (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_day_of_week (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");
	g_string_append (state->accum_fmt, is_short ? "ddd" : "dddd");
}
static void
oo_date_week_of_year (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_quarter (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_hours (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");
	g_string_append (state->accum_fmt, is_short ? "h" : "hh");
}
static void
oo_date_minutes (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");
	g_string_append (state->accum_fmt, is_short ? "m" : "mm");
}
static void
oo_date_seconds (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->accum_fmt == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_NUMBER, "style"))
			is_short = 0 == strcmp (attrs[1], "short");
	g_string_append (state->accum_fmt, is_short ? "s" : "ss");
}
static void
oo_date_am_pm (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	if (state->accum_fmt != NULL)
		g_string_append (state->accum_fmt, "AM/PM");

}
static void
oo_date_text_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->accum_fmt == NULL)
		return;

	g_string_append (state->accum_fmt, xin->content->str);
}

static void
oo_date_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "name"))
			name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "family") &&
			 0 != strcmp (attrs[1], "data-style"))
			return;

	g_return_if_fail (state->accum_fmt == NULL);
	g_return_if_fail (name != NULL);

	state->accum_fmt = g_string_new (NULL);
	state->fmt_name = g_strdup (name);
}

static void
oo_date_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	g_return_if_fail (state->accum_fmt != NULL);

	g_hash_table_insert (state->formats, state->fmt_name,
		go_format_new_from_XL (state->accum_fmt->str, FALSE));
	g_string_free (state->accum_fmt, TRUE);
	state->accum_fmt = NULL;
	state->fmt_name = NULL;
}

static void
oo_parse_border (GsfXMLIn *xin, GnmStyle *style,
		 char const *str, GnmStyleElement location)
{
	double pts;
	char const *end = oo_parse_distance (xin, str, "border", &pts);
	GnmBorder *border = NULL;
	GnmColor *color = NULL;
	char *border_color = NULL;
	char *border_type = NULL;
	size_t pos = 0;
	StyleBorderType border_style;

	if (end == NULL || end == str)
		return;
	if (*end == ' ')
		end++;
/* "0.035cm solid #000000" */
	border_color = strchr (end, '#');
	if (border_color) {
		pos = strlen (end) - strlen (border_color);
		border_type = (char *)malloc(pos);
		memset (border_type, '\0', pos);
		strncpy (border_type, end, pos-1);
		color = oo_parse_color (xin, border_color, "color");

		if (!strcmp("solid", border_type)) {
			if (pts <= OD_BORDER_THIN)
				border_style = STYLE_BORDER_THIN;
			else if (pts <= OD_BORDER_MEDIUM)
				border_style = STYLE_BORDER_MEDIUM;
			else
				border_style = STYLE_BORDER_THICK;
		} else
			border_style = STYLE_BORDER_DOUBLE;

		border = style_border_fetch (border_style, color,
			style_border_get_orientation (location - MSTYLE_BORDER_TOP));
		border->width = pts;
		gnm_style_set_border (style, location, border);
		free (border_type);
	}
}

static void
oo_style_prop_cell (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const h_alignments [] = {
		{ "start",	HALIGN_LEFT },
		{ "center",	HALIGN_CENTER },
		{ "end", 	HALIGN_RIGHT },
		{ "justify",	HALIGN_JUSTIFY },
		{ "automatic",	HALIGN_GENERAL },
		{ NULL,	0 },
	};
	static OOEnum const v_alignments [] = {
		{ "bottom", 	VALIGN_BOTTOM },
		{ "top",	VALIGN_TOP },
		{ "middle",	VALIGN_CENTER },
		{ "automatic",	VALIGN_TOP },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmColor *color;
	GnmStyle *style = state->cur_style.cell;
	gboolean  btmp;
	int	  tmp;

	g_return_if_fail (style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "background-color"))) {
			gnm_style_set_back_color (style, color);
			if (color->name != NULL){
				if (!strcmp (color->name, "transparent")) {
					gnm_style_set_pattern (style, 0);
					g_free (color->name);
				}
			} else
				gnm_style_set_pattern (style, 1);
		} else if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "color")))
			gnm_style_set_font_color (style, color);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "cell-protect"))
			gnm_style_set_content_locked (style, !strcmp (attrs[1], "protected"));
		else if (oo_attr_enum (xin, attrs,
				       (state->ver >= OOO_VER_OPENDOC) ? OO_NS_FO : OO_NS_STYLE,
				       "text-align", h_alignments, &tmp))
			gnm_style_set_align_h (style, state->h_align_is_valid
					       ? (state->repeat_content ? HALIGN_FILL : tmp)
					       : HALIGN_GENERAL);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "text-align-source"))
			state->h_align_is_valid = !strcmp (attrs[1], "fix");
		else if (oo_attr_bool (xin, attrs, OO_NS_STYLE, "repeat-content", &btmp))
			state->repeat_content = btmp;
		else if (oo_attr_enum (xin, attrs,
				       (state->ver >= OOO_VER_OPENDOC) ? OO_NS_STYLE : OO_NS_FO,
				       "vertical-align", v_alignments, &tmp))
			gnm_style_set_align_v (style, tmp);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "wrap-option"))
			gnm_style_set_wrap_text (style, !strcmp (attrs[1], "wrap"));
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "border-bottom"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_BOTTOM);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "border-left"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_LEFT);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "border-right"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_RIGHT);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "border-top"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_TOP);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "border")) {
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_BOTTOM);
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_LEFT);
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_RIGHT);
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_TOP);
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "diagonal-tr-bl"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_DIAGONAL);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "diagonal-tl-br"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_REV_DIAGONAL);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "font-name"))
			gnm_style_set_font_name (style, attrs[1]);
		else if (oo_attr_bool (xin, attrs, OO_NS_STYLE, "shrink-to-fit", &btmp))
			gnm_style_set_shrink_to_fit (style, btmp);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "cell-protect"))
			gnm_style_set_content_locked (style, !strcmp (attrs[1], "protected"));
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "direction"))
			gnm_style_set_text_dir (style, strcmp (attrs[1], "rtl") ? GNM_TEXT_DIR_LTR : GNM_TEXT_DIR_RTL);
		else if (oo_attr_int (xin, attrs, OO_NS_STYLE, "rotation-angle", &tmp))
			gnm_style_set_rotation	(style, tmp);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "font-size")) {
			float size;
			if (1 == sscanf (attrs[1], "%fpt", &size))
				gnm_style_set_font_size (style, size);
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "text-underline-style"))
			/* cheesy simple support for now */
			gnm_style_set_font_uline (style, strcmp (attrs[1], "none") ? UNDERLINE_SINGLE : UNDERLINE_NONE);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "font-style"))
			gnm_style_set_font_italic (style, 0 == strcmp (attrs[1], "italic"));
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_FO, "font-weight"))
			gnm_style_set_font_bold (style, 0 == strcmp (attrs[1], "bold"));
#if 0
		else if (!strcmp (attrs[0], OO_NS_FO, "font-weight")) {
				gnm_style_set_font_bold (style, TRUE);
				gnm_style_set_font_uline (style, TRUE);
			="normal"
		} else if (!strcmp (attrs[0], OO_NS_STYLE, "text-underline" )) {
			="italic"
				gnm_style_set_font_italic (style, TRUE);
		}
#endif
}

static void
oo_style_prop_row (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	double pts;

	g_return_if_fail (state->cur_style.col_row != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (NULL != oo_attr_distance (xin, attrs, OO_NS_STYLE, "row-height", &pts))
			*(state->cur_style.col_row) = pts;
}
static void
oo_style_prop_col (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	double pts;

	g_return_if_fail (state->cur_style.col_row != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (NULL != oo_attr_distance (xin, attrs, OO_NS_STYLE, "column-width", &pts))
			*(state->cur_style.col_row) = pts;
}

static void
oo_style_map (GsfXMLIn *xin, xmlChar const **attrs)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "condition")) /* "cell-content()=1" */
			;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "apply-style-name"))
			;
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_STYLE, "base-cell-address"))
			;
}

static ODProps*
dup_prop(ODProps *pointer) {
	ODProps *prop_copy = g_new0(ODProps, 1);

	prop_copy->name = strdup (pointer->name);
	prop_copy->value = g_new0 (GValue, 1);
	prop_copy->value = g_value_init (prop_copy->value, G_VALUE_TYPE(pointer->value));
	g_value_copy (pointer->value, prop_copy->value);

	return prop_copy;
}

static void
od_style_prop_chart (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	ODGraphProperties *style = state->cur_frame.cur_graph_style;
	ODProps *prop = NULL;

	g_return_if_fail (style != NULL);

	style->grid = FALSE;
	style->row_src = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		prop = g_new0 (ODProps, 1);
		prop->value = g_new0 (GValue, 1);
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "logarithmic")) {
			if (!strcmp(attrs[1], "true")){
				prop->name = strdup ("map-name");
				prop->value = g_value_init (prop->value, G_TYPE_STRING);
				g_value_set_string (prop->value, "Log");
				style->axis = g_slist_append (style->axis, dup_prop(prop));
			}
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "vertical")) {
			if (!strcmp(attrs[1], "true")){
				prop->name = strdup ("horizontal");
				prop->value = g_value_init (prop->value, G_TYPE_BOOLEAN);
				g_value_set_boolean (prop->value, TRUE);
				style->chart = g_slist_append (style->chart, dup_prop(prop));
			}
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "stacked")) {
			if (!strcmp(attrs[1], "true")){
				prop->name = strdup ("type");
				prop->value = g_value_init (prop->value, G_TYPE_STRING);
				g_value_set_string (prop->value, strdup("stacked"));
				style->chart = g_slist_append (style->chart, dup_prop(prop));
			}
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "percentage")) {
			if (!strcmp(attrs[1], "true")){
				prop->name = strdup ("type");
				prop->value = g_value_init (prop->value, G_TYPE_STRING);
				g_value_set_string (prop->value, strdup("as_percentage"));
				style->chart = g_slist_append (style->chart, dup_prop(prop));
			}
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "overlap")) {
				prop->name = strdup ("overlap-percentage");
				prop->value = g_value_init (prop->value, G_TYPE_INT);
				g_value_set_int (prop->value, g_strtod(attrs[1], NULL));
				style->chart = g_slist_append (style->chart, dup_prop(prop));
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "gap-width")) {
				prop->name = strdup ("gap-percentage");
				prop->value = g_value_init (prop->value, G_TYPE_INT);
				g_value_set_int (prop->value, g_strtod(attrs[1], NULL));
				style->chart = g_slist_append (style->chart, dup_prop(prop));
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "series-source")) {
			if (!strcmp(attrs[1], "rows"))
				style->row_src = TRUE;
		}
		if (G_IS_VALUE (prop->value)) {
			g_value_unset (prop->value);
			g_free (prop->name);
		} else
			g_free (prop->value);
		g_free (prop);
	}
}

static void
oo_style_prop (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	switch (state->cur_style_type) {
	case OO_STYLE_CELL : oo_style_prop_cell (xin, attrs); break;
	case OO_STYLE_COL :  oo_style_prop_col (xin, attrs); break;
	case OO_STYLE_ROW :  oo_style_prop_row (xin, attrs); break;
	case OO_STYLE_CHART :  od_style_prop_chart (xin, attrs); break;

	default :
		break;
	}
}

static void
oo_named_expr (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	xmlChar const *name     = NULL;
	xmlChar const *base_str  = NULL;
	xmlChar const *expr_str = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "name"))
			name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "base-cell-address"))
			base_str = attrs[1];
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "expression"))
			expr_str = attrs[1];

	if (name != NULL && base_str != NULL && expr_str != NULL) {
		GnmParseError perr;
		GnmParsePos   pp;
		GnmExpr const *expr;
		char *tmp = g_strconcat ("[", base_str, "]", NULL);

		parse_error_init (&perr);
		parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);

		expr = oo_expr_parse_str (tmp, &pp,
			GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
			&perr);
		g_free (tmp);

		if (expr == NULL || GNM_EXPR_GET_OPER (expr) != GNM_EXPR_OP_CELLREF) {
			oo_warning (xin, _("Unable to parse position for expression '%s' @ '%s' because '%s'"),
				    name, base_str, perr.err->message);
			parse_error_free (&perr);
			if (expr != NULL)
				gnm_expr_unref (expr);
		} else {
			GnmCellRef const *ref = &expr->cellref.ref;
			parse_pos_init (&pp, state->pos.wb, ref->sheet,
					ref->col, ref->row);

			gnm_expr_unref (expr);
			expr = oo_expr_parse_str (expr_str, &pp, 0, &perr);
			if (expr == NULL) {
				oo_warning (xin, _("Unable to parse position for expression '%s' with value '%s' because '%s'"),
					    name, expr_str, perr.err->message);
				parse_error_free (&perr);
			} else {
				pp.sheet = NULL;
				expr_name_add (&pp, name, expr, NULL, TRUE, NULL);
			}
		}
	}
}

static void
oo_named_range (GsfXMLIn *xin, xmlChar const **attrs)
{
	g_warning ("Unimplemented");
}

static void
od_draw_frame (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmRange cell_base;
	gfloat frame_offset [4];
	gchar const *aux = NULL;
	gdouble height, width, x, y;
	ColRowInfo const *col, *row;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2){
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_SVG, "width"))
			aux = oo_parse_distance (xin, attrs[1], "width", &width);
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_SVG, "height"))
			aux = oo_parse_distance (xin, attrs[1], "height", &height);
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_SVG, "x"))
			aux = oo_parse_distance (xin, attrs[1], "x", &x);
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "y"))
			aux = oo_parse_distance (xin, attrs[1], "y", &y);
	}
	cell_base.start.col = cell_base.end.col = state->pos.eval.col;
	cell_base.start.row = cell_base.end.row = state->pos.eval.row;

	col = sheet_col_get_info (state->pos.sheet, state->pos.eval.col);
	row = sheet_row_get_info (state->pos.sheet, state->pos.eval.row);

	frame_offset[0] = (x/col->size_pts);
	frame_offset[1] = (y/row->size_pts);
	frame_offset[2] = (width/col->size_pts);
	frame_offset[3] = (height/row->size_pts);

	sheet_object_anchor_init (&state->cur_frame.anchor, &cell_base, frame_offset, NULL, SO_DIR_DOWN_RIGHT);
}

static void
od_draw_object (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *name = NULL;
	GsfInput	*content = NULL;
	SheetObject *sog = sheet_object_graph_new (NULL);

	state->cur_frame.graph = sheet_object_graph_get_gog (sog);
	sheet_object_set_anchor (sog, &state->cur_frame.anchor);
	sheet_object_set_sheet (sog, state->pos.sheet);
	g_object_unref (sog);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_XLINK, "href") &&
		    strncmp (attrs[1], "./", 2) == 0) {
			name = attrs[1] + 2;
			break;
		}

	if (!name)
		return;

	content = gsf_infile_child_by_vname (state->zip, name, "content.xml", NULL);

	if (content != NULL) {
		GsfXMLInDoc *doc =
			gsf_xml_in_doc_new (opendoc_content_dtd, gsf_ooo_ns);
		gsf_xml_in_doc_parse (doc, content, state);
		gsf_xml_in_doc_free (doc);
		g_object_unref (content);
	}

	g_hash_table_destroy (state->cur_frame.graph_styles);
	state->cur_frame.graph_styles = g_hash_table_new_full (g_str_hash, g_str_equal,
															(GDestroyNotify) g_free,
															(GDestroyNotify) clean_lists);
	state->cur_frame.has_legend = FALSE;
}

static void
od_draw_image (GsfXMLIn *xin, xmlChar const **attrs)
{
	GsfInput *input;

	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *file = NULL;

	SheetObjectImage *soi;
	SheetObject *so;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_XLINK, "href") &&
		    strncmp (attrs[1], "Pictures/", 9) == 0) {
			file = attrs[1] + 9;
			break;
		}

	if (!file)
		return;

	input = gsf_infile_child_by_vname (state->zip, "Pictures", file, NULL);

	if (input != NULL) {
		gsf_off_t len = gsf_input_size (input);
		guint8 const *data = gsf_input_read (input, len, NULL);
		soi = g_object_new (SHEET_OBJECT_IMAGE_TYPE, NULL);
		sheet_object_image_set_image (soi, "", (void *)data, len, TRUE);

		so = SHEET_OBJECT (soi);
		sheet_object_set_anchor (so, &state->cur_frame.anchor);
		sheet_object_set_sheet (so, state->pos.sheet);
		g_object_unref (input);
	}
}

static void
od_chart_title (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	GogObject *label = NULL;
	OOParseState *state = (OOParseState *)xin->user_state;

	label = gog_object_add_by_name ((GogObject *)state->cur_frame.chart, "Title", NULL);
	gog_dataset_set_dim (GOG_DATASET (label), 0,
			     go_data_scalar_str_new (g_strdup (xin->content->str), TRUE),
			     NULL);
}

static void
od_chart_axis (GsfXMLIn *xin, xmlChar const **attrs)
{
	gchar const *name = NULL;
	OOParseState *state = (OOParseState *)xin->user_state;
	ODGraphProperties *style = NULL;
	GSList *axis;
	guint cont;
	ODProps *axis_props;
	GSList *plots;
	GogPlot *plot;

	state->cur_frame.cur_axis = GOG_AXIS_UNKNOWN;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "style-name"))
			name = attrs[1];
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "dimension")) {
			if (!strcmp(attrs[1],"x"))
				state->cur_frame.cur_axis = GOG_AXIS_X;
			else
				state->cur_frame.cur_axis = GOG_AXIS_Y;
		}
	}
	style = g_hash_table_lookup (state->cur_frame.graph_styles, name);
	axis = gog_chart_get_axes (state->cur_frame.chart, state->cur_frame.cur_axis);
	for (cont = 0; cont < g_slist_length(style->axis); cont ++) {
		axis_props = g_slist_nth_data(style->axis, cont);
		g_object_set (axis->data,
				axis_props->name,
				g_value_get_string(axis_props->value),
				NULL);
	}
	plots = gog_chart_get_plots (state->cur_frame.chart);
	plot = g_slist_nth_data (plots, 0);
	for (cont = 0; cont < g_slist_length(style->chart); cont ++) {
		axis_props = g_slist_nth_data(style->chart, cont);
		switch (G_VALUE_TYPE(axis_props->value)) {
			case G_TYPE_BOOLEAN:
				g_object_set (plot,
						strdup(axis_props->name),
						g_value_get_boolean (axis_props->value),
						NULL);
				break;
			case G_TYPE_STRING:
				g_object_set (plot,
						strdup(axis_props->name),
						strdup(g_value_get_string (axis_props->value)),
						NULL);
				break;
			case G_TYPE_INT:
				g_object_set (plot,
						axis_props->name,
						g_value_get_int (axis_props->value),
						NULL);
				break;
			default: break;
		}
	}
}

static void
od_plot_area(GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar *graph_range = NULL;
	GnmValue *actual_range;
	GogSeries *series;
	GnmRange cell_base;
	GogPlot   *plot;
	GnmParseError  perr;
	GnmExpr const	*expr = NULL;
	gint cur_col, dim, flag=0;
	gint MAX_DIM = 2;
	gint v_offset = 0, h_offset = 0;

	gchar const *name = NULL;
	ODGraphProperties *style = NULL;
	guint cont;
	ODProps *chart_props;
	GogObject *legend;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "style-name")) {
			name = attrs[1];
			style = g_hash_table_lookup (state->cur_frame.graph_styles, name);
		} else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_TABLE, "cell-range-address"))
			graph_range = g_strdup_printf("[%s]",attrs[1]);
		else if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "data-source-has-labels")) {
			if (!strcmp(attrs[1], "both"))
				v_offset = h_offset = 1;
			else if (!strcmp(attrs[1], "column"))
				h_offset = 1;
			else if (!strcmp(attrs[1], "row"))
				v_offset = 1;
		}
	flag = !(v_offset || h_offset);

	switch (state->cur_frame.chart_type){
		case AREA:
				plot = gog_plot_new_by_name ("GogAreaPlot");
				break;
		case BAR:
				plot = gog_plot_new_by_name ("GogBarColPlot");
				break;
		case CIRCLE:
				plot = gog_plot_new_by_name ("GogPiePlot");
				break;
		case LINE:
				plot = gog_plot_new_by_name ("GogLinePlot");
				break;
		case RADAR:
				plot = gog_plot_new_by_name ("GogRadarPlot");
				flag = 1;
				break;
		case RADARAREA:
				plot = gog_plot_new_by_name ("GogRadarAreaPlot");
				break;
		case RING:
				plot = gog_plot_new_by_name ("GogRingPlot");
				break;
		case SCATTER:
				plot = gog_plot_new_by_name ("GogXYPlot");
				flag = 0;
				break;
		case STOCK:
				plot = gog_plot_new_by_name ("GogMinMaxPlot");
				MAX_DIM = 3;
				break;
		case SURF:
				plot = gog_plot_new_by_name ("GogContourPlot");
				break;
		default: return;
	}
	for (cont = 0; cont < g_slist_length(style->chart); cont ++){
		chart_props = g_slist_nth_data(style->chart, cont);
		switch (G_VALUE_TYPE(chart_props->value)) {
			case G_TYPE_BOOLEAN:
				g_object_set (plot,
						strdup(chart_props->name),
						g_value_get_boolean (chart_props->value),
						NULL);
				break;
			case G_TYPE_STRING:
				g_object_set (plot,
						strdup(chart_props->name),
						strdup(g_value_get_string (chart_props->value)),
						NULL);
				break;
			case G_TYPE_INT:
				g_object_set (plot,
						strdup(chart_props->name),
						g_value_get_int (chart_props->value),
						NULL);
				break;
			default: break;
		}
	}

	gog_object_add_by_name (GOG_OBJECT(state->cur_frame.chart), "Plot", GOG_OBJECT (plot));
	parse_error_init (&perr);
	expr = oo_expr_parse_str (graph_range, &state->pos, 0, &perr);
	if (expr == NULL) {
		oo_warning (xin, _("Unable to parse '%s' because '%s'"),
				    attrs[1], perr.err->message);
		parse_error_free (&perr);
	}
	actual_range = gnm_expr_get_range (expr);
	cur_col = actual_range->v_range.cell.a.col;

	cell_base.start.row = actual_range->v_range.cell.a.row + v_offset;
	cell_base.end.row = actual_range->v_range.cell.b.row;
	do {
		series = gog_plot_new_series (plot);
		dim = flag;

		if (flag) {
			cell_base.start.col = cur_col;
			cell_base.end.col = cur_col;
		} else {
			cell_base.start.col = actual_range->v_range.cell.a.col;
			cell_base.end.col = actual_range->v_range.cell.a.col;
		}
		while (dim < MAX_DIM){
			gog_series_set_dim (series, dim,
							gnm_go_data_vector_new_expr (state->pos.sheet,
							gnm_expr_new_constant (
							value_new_cellrange_r (state->pos.sheet, &cell_base))), NULL);
			cell_base.start.col = cell_base.end.col = cur_col + 1;
			dim++;
		}
		cur_col++;
	} while (cur_col < actual_range->v_range.cell.b.col+flag);

	if (state->cur_frame.has_legend){
		legend = gog_object_add_by_name ((GogObject *)state->cur_frame.chart, "Legend", NULL);
		gog_object_set_position_flags (legend, state->cur_frame.legend, GOG_POSITION_COMPASS | GOG_POSITION_ALIGNMENT);
	}
	value_release (actual_range);
	g_free (graph_range);
	gnm_expr_unref (expr);
}

static void
od_chart (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *chart_type = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "class"))
			chart_type = attrs[1]+6; /* removes the "chart:" */

	if (!strcmp(chart_type, "area"))
		state->cur_frame.chart_type = AREA;
	else if (!strcmp(chart_type, "bar"))
		state->cur_frame.chart_type = BAR;
	else if (!strcmp(chart_type, "circle"))
		state->cur_frame.chart_type = CIRCLE;
	else if (!strcmp(chart_type, "line"))
		state->cur_frame.chart_type = LINE;
	else if (!strcmp(chart_type, "radar"))
		state->cur_frame.chart_type = RADAR;
	else if (!strcmp(chart_type, "ring"))
		state->cur_frame.chart_type = RING;
	else if (!strcmp(chart_type, "scatter"))
		state->cur_frame.chart_type = SCATTER;
	else if (!strcmp(chart_type, "stock"))
		state->cur_frame.chart_type = STOCK;

	state->cur_frame.chart = GOG_CHART (gog_object_add_by_name (GOG_OBJECT(state->cur_frame.graph), "Chart", NULL));
}

static void
od_legend (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->cur_frame.has_legend = TRUE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "legend-position")) {
			if (!strcmp (attrs[1], "top"))
				state->cur_frame.legend = GOG_POSITION_N | GOG_POSITION_ALIGN_CENTER;
			else if (!strcmp (attrs[1], "bottom"))
				state->cur_frame.legend = GOG_POSITION_S | GOG_POSITION_ALIGN_CENTER;
			else if (!strcmp (attrs[1], "end"))
				state->cur_frame.legend = GOG_POSITION_E | GOG_POSITION_ALIGN_CENTER;
			else
				state->cur_frame.legend = GOG_POSITION_W | GOG_POSITION_ALIGN_CENTER;
		}
}

static void
od_chart_grid (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GSList *axis = NULL;

	if ((state->cur_frame.cur_axis != UNKNOWN) &&
	 	(state->cur_frame.chart_type != RING) &&
	 	(state->cur_frame.chart_type != CIRCLE) &&
	 	(state->cur_frame.chart_type != RADAR)) {

		axis = gog_chart_get_axes (state->cur_frame.chart, state->cur_frame.cur_axis);
		for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
			if (gsf_xml_in_namecmp (xin, attrs[0], OO_NS_CHART, "class"))
			{
				if (!strcmp(attrs[1], "major"))
					gog_object_add_by_name (GOG_OBJECT(axis->data), "MajorGrid", NULL);
				else if (!strcmp(attrs[1], "minor"))
					gog_object_add_by_name (GOG_OBJECT(axis->data), "MinorGrid", NULL);
			}
	}
}

static void
od_chart_wall (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	gog_object_add_by_name (GOG_OBJECT(state->cur_frame.chart), "Grid", NULL);
}

static void
clean_lists(ODGraphProperties *pointer)
{
	guint cont;
	ODProps *pointer_props = NULL;

	if (g_slist_length (pointer->axis) > 0) {
		for (cont = 0; cont < g_slist_length(pointer->axis); cont ++){
			pointer_props = (ODProps *)g_slist_nth_data (pointer->axis, cont);
			g_free (pointer_props->name);
			if (G_IS_VALUE (pointer_props->value))
				g_value_unset(pointer_props->value);
			else
				g_free (pointer_props->value);
		}
		g_slist_free (pointer->axis);
	}
	if (g_slist_length(pointer->chart) > 0) {
		for (cont = 0; cont < g_slist_length (pointer->chart); cont ++){
			pointer_props = (ODProps *)g_slist_nth_data (pointer->chart, cont);
			g_free(pointer_props->name);
			if (G_IS_VALUE (pointer_props->value))
				g_value_unset (pointer_props->value);
			else
				g_free (pointer_props->value);
		}
		g_slist_free (pointer->chart);
	}
}

static GsfXMLInNode const styles_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),

/* ooo-1.x */
GSF_XML_IN_NODE (START, OFFICE_FONTS, OO_NS_OFFICE, "font-decls", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-decl", GSF_XML_NO_CONTENT, NULL, NULL),

/* ooo-2.x */
GSF_XML_IN_NODE (START, OFFICE_FONTS, OO_NS_OFFICE, "font-face-decls", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-face", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE (START, OFFICE_STYLES, OO_NS_OFFICE, "styles", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
    GSF_XML_IN_NODE (STYLE, TABLE_CELL_PROPS, OO_NS_STYLE,	"table-cell-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (STYLE, TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE, PARAGRAPH_PROPS, OO_NS_STYLE,	"paragraph-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (PARAGRAPH_PROPS, PARA_TABS, OO_NS_STYLE,  "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE, STYLE_PROP, OO_NS_STYLE,		"properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, DEFAULT_STYLE, OO_NS_STYLE, "default-style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TABLE_CELL_PROPS, OO_NS_STYLE, "table-cell-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TEXT_PROP, OO_NS_STYLE,	   "text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_GRAPHIC_PROPS, OO_NS_STYLE,	   "graphic-properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_PARAGRAPH_PROPS, OO_NS_STYLE,  "paragraph-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (DEFAULT_PARAGRAPH_PROPS, DEFAULT_PARA_TABS, OO_NS_STYLE,  "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_STYLE_PROP, OO_NS_STYLE,	   "properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (DEFAULT_STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	"text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, OO_NS_STYLE,	"properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER,		"day", GSF_XML_NO_CONTENT,	&oo_date_day, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER,		"month", GSF_XML_NO_CONTENT,	&oo_date_month, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER,		"year", GSF_XML_NO_CONTENT,	&oo_date_year, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_ERA, OO_NS_NUMBER,		"era", GSF_XML_NO_CONTENT,	&oo_date_era, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER,	"day-of-week", GSF_XML_NO_CONTENT, &oo_date_day_of_week, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER,	"week-of-year", GSF_XML_NO_CONTENT, &oo_date_week_of_year, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER,		"quarter", GSF_XML_NO_CONTENT, &oo_date_quarter, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,			"map", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER, 		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,			"map", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER, "boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER, "boolean", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER,		"currency-style", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE,	"properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER,	"currency-symbol", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER,	"text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER, "text-style", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,		"text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

  { NULL }
};

static GsfXMLInNode const ooo1_content_dtd [] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-content", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, SCRIPT, OO_NS_OFFICE, "script", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_FONTS, OO_NS_OFFICE, "font-decls", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-decl", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_STYLES, OO_NS_OFFICE, "automatic-styles", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
      GSF_XML_IN_NODE (STYLE, STYLE_PROP, OO_NS_STYLE, "properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
        GSF_XML_IN_NODE (STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	  "number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	  "text", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, OO_NS_STYLE,	  "properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		  "map", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER,		"day", GSF_XML_NO_CONTENT,	&oo_date_day, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER,		"month", GSF_XML_NO_CONTENT,	&oo_date_month, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER,		"year", GSF_XML_NO_CONTENT,	&oo_date_year, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_ERA, OO_NS_NUMBER,		"era", GSF_XML_NO_CONTENT,	&oo_date_era, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER,	"day-of-week", GSF_XML_NO_CONTENT, &oo_date_day_of_week, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER,	"week-of-year", GSF_XML_NO_CONTENT, &oo_date_week_of_year, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER,		"quarter", GSF_XML_NO_CONTENT, &oo_date_quarter, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER, 		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER, "boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER, "boolean", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER, "currency-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER, "number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE, "properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE, "map", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER, "currency-symbol", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER, "text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER, "number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER, "text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER, "text-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,		"text", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE, OFFICE_BODY, OO_NS_OFFICE, "body", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, TABLE_CALC_SETTINGS, OO_NS_TABLE, "calculation-settings", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TABLE_CALC_SETTINGS, DATE_CONVENTION, OO_NS_TABLE, "null-date", GSF_XML_NO_CONTENT, oo_date_convention, NULL),

    GSF_XML_IN_NODE (OFFICE_BODY, TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, &oo_table_start, NULL),
      GSF_XML_IN_NODE (TABLE, FORMS,	 OO_NS_OFFICE, "forms", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, &oo_col_start, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, &oo_row_start, &oo_row_end),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, &oo_cell_start, &oo_cell_end),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", GSF_XML_NO_CONTENT, &oo_covered_cell_start, &oo_covered_cell_end),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, &oo_cell_content_end),
	    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, &oo_cell_content_span_start, &oo_cell_content_span_end),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_OBJECT, OO_NS_DRAW, "object", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
	    GSF_XML_IN_NODE (CELL_GRAPHIC, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd def */
	    GSF_XML_IN_NODE (CELL_GRAPHIC, DRAW_POLYLINE, OO_NS_DRAW, "polyline", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd def */
      GSF_XML_IN_NODE (TABLE, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
      GSF_XML_IN_NODE (TABLE, TABLE_ROW_GROUP,	      OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW_GROUP, OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW,	    OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
    GSF_XML_IN_NODE (OFFICE_BODY, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_EXPR, OO_NS_TABLE, "named-expression", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, DB_RANGES, OO_NS_TABLE, "database-ranges", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (DB_RANGES, DB_RANGE, OO_NS_TABLE, "database-range", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DB_RANGE, TABLE_SORT, OO_NS_TABLE, "sort", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (TABLE_SORT, SORT_BY, OO_NS_TABLE, "sort-by", GSF_XML_NO_CONTENT, NULL, NULL),
  { NULL }
};

/****************************************************************************/

typedef GValue		OOConfigItem;
typedef GHashTable	OOConfigItemSet;
typedef GHashTable	OOConfigItemMapNamed;
typedef GPtrArray	OOConfigItemMapIndexed;

#if 0
static GHashTable *
oo_config_item_set ()
{
	return NULL;
}
#endif

static GsfXMLInNode const opencalc_settings_dtd [] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-settings", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, SETTINGS, OO_NS_OFFICE, "settings", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SETTINGS, CONFIG_ITEM_SET, OO_NS_CONFIG, "config-item-set", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE_FULL (CONFIG_ITEM_SET, CONFIG_ITEM,		OO_NS_CONFIG, "config-item",		GSF_XML_NO_CONTENT,  TRUE, FALSE, NULL, NULL, 0),
      GSF_XML_IN_NODE_FULL (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_INDEXED,	OO_NS_CONFIG, "config-item-map-indexed", GSF_XML_NO_CONTENT, TRUE, FALSE, NULL, NULL, 1),
      GSF_XML_IN_NODE_FULL (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_ENTRY,	OO_NS_CONFIG, "config-item-map-entry",	GSF_XML_NO_CONTENT,  TRUE, FALSE, NULL, NULL, 2),
      GSF_XML_IN_NODE_FULL (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_NAMED,	OO_NS_CONFIG, "config-item-map-named",	GSF_XML_NO_CONTENT,  TRUE, FALSE, NULL, NULL, 3),
  { NULL }
};

/****************************************************************************/
/* Generated based on:
 * http://www.oasis-open.org/committees/download.php/12572/OpenDocument-v1.0-os.pdf */
static GsfXMLInNode const opendoc_content_dtd [] = {
	GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
	GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-content", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE, SCRIPT, OO_NS_OFFICE, "scripts", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE, OFFICE_FONTS, OO_NS_OFFICE, "font-face-decls", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_FONTS, FONT_FACE, OO_NS_STYLE, "font-face", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE, OFFICE_STYLES, OO_NS_OFFICE, "automatic-styles", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
	      GSF_XML_IN_NODE (STYLE, TABLE_CELL_PROPS, OO_NS_STYLE, "table-cell-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TABLE_COL_PROPS, OO_NS_STYLE, "table-column-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TABLE_ROW_PROPS, OO_NS_STYLE, "table-row-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, CHART_PROPS, OO_NS_STYLE, "chart-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TEXT_PROPS, OO_NS_STYLE, "text-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TABLE_PROPS, OO_NS_STYLE, "table-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, PARAGRAPH_PROPS, OO_NS_STYLE, "paragraph-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, GRAPHIC_PROPS, OO_NS_STYLE, "graphic-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, STYLE_MAP, OO_NS_STYLE, "map", GSF_XML_NO_CONTENT, &oo_style_map, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	  "number", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	  "text", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, OO_NS_STYLE,	  "properties", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		  "map", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER,		"day", GSF_XML_NO_CONTENT,	&oo_date_day, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER,		"month", GSF_XML_NO_CONTENT,	&oo_date_month, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER,		"year", GSF_XML_NO_CONTENT,	&oo_date_year, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_ERA, OO_NS_NUMBER,		"era", GSF_XML_NO_CONTENT,	&oo_date_era, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER,	"day-of-week", GSF_XML_NO_CONTENT, &oo_date_day_of_week, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER,	"week-of-year", GSF_XML_NO_CONTENT, &oo_date_week_of_year, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER,		"quarter", GSF_XML_NO_CONTENT, &oo_date_quarter, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER, 	"time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,	"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,	"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,	"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,	"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER, 	"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,	"map", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER,	"boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER,	"boolean", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER,	 "currency-style", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER,	 "number", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE, "properties", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE,	 "map", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER,	 "currency-symbol", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER,	 "text", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER,		"percentage-style", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER,		"text-style", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,	"text", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (OFFICE, OFFICE_BODY, OO_NS_OFFICE, "body", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE_BODY, TABLE_SETTINGS, OO_NS_OFFICE, "spreadsheet", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (TABLE_SETTINGS, CALC_SETTINGS, OO_NS_TABLE, "calculation-settings", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_SETTINGS, CHART, OO_NS_CHART, "chart", FALSE, NULL, NULL),

		GSF_XML_IN_NODE (OFFICE_BODY, OFFICE_CHART, OO_NS_OFFICE, "chart", FALSE, NULL, NULL),
		GSF_XML_IN_NODE (OFFICE_CHART, CHART_CHART, OO_NS_CHART, "chart", FALSE, &od_chart, NULL),
		GSF_XML_IN_NODE (CHART_CHART, CHART_TITLE, OO_NS_CHART, "title", FALSE, NULL, NULL),
		GSF_XML_IN_NODE (CHART_TITLE, TITLE_TEXT, OO_NS_TEXT, "p", TRUE, NULL, &od_chart_title),
		GSF_XML_IN_NODE (CHART_CHART, CHART_LEGEND, OO_NS_CHART, "legend", TRUE, &od_legend, NULL),
		GSF_XML_IN_NODE (CHART_CHART, CHART_PLOT_AREA, OO_NS_CHART, "plot-area", TRUE, &od_plot_area, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_AXIS, OO_NS_CHART, "axis", TRUE, &od_chart_axis, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_SERIES, OO_NS_CHART, "series", TRUE, NULL, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_WALL, OO_NS_CHART, "wall", TRUE, &od_chart_wall, NULL),
		GSF_XML_IN_NODE (CHART_AXIS, CHART_GRID, OO_NS_CHART, "grid", TRUE, &od_chart_grid, NULL),

	    GSF_XML_IN_NODE (TABLE_SETTINGS, TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, &oo_table_start, NULL),
	      GSF_XML_IN_NODE (TABLE, FORMS, OO_NS_OFFICE, "forms", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, &oo_col_start, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, &oo_row_start, &oo_row_end),
	      GSF_XML_IN_NODE (TABLE, TABLE_ROWS, OO_NS_TABLE, "table-rows", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_ROW, TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, &oo_cell_start, &oo_cell_end),
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_FRAME, OO_NS_DRAW, "frame", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_FRAME, CELL_IMAGE, OO_NS_DRAW, "image", GSF_XML_NO_CONTENT, NULL, NULL),
		      GSF_XML_IN_NODE (CELL_IMAGE, IMAGE_TEXT, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", GSF_XML_NO_CONTENT, &oo_covered_cell_start, &oo_covered_cell_end),
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, &oo_cell_content_end),

			GSF_XML_IN_NODE (TABLE_CELL, DRAW_FRAME, OO_NS_DRAW, "frame", FALSE, &od_draw_frame, NULL),
			GSF_XML_IN_NODE (DRAW_FRAME, DRAW_OBJECT, OO_NS_DRAW, "object", TRUE, &od_draw_object, NULL),
			GSF_XML_IN_NODE (DRAW_FRAME, DRAW_IMAGE, OO_NS_DRAW, "image", TRUE, &od_draw_image, NULL),

		    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_ADDR, OO_NS_TEXT, "a", GSF_XML_SHARED_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, &oo_cell_content_span_start, &oo_cell_content_span_end),
		      GSF_XML_IN_NODE (CELL_TEXT_SPAN, CELL_TEXT_SPAN_ADDR, OO_NS_TEXT, "a", GSF_XML_SHARED_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_OBJECT, OO_NS_DRAW, "object", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
		    GSF_XML_IN_NODE (CELL_GRAPHIC, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd def */
		    GSF_XML_IN_NODE (CELL_GRAPHIC, DRAW_POLYLINE, OO_NS_DRAW, "polyline", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd def */
	      GSF_XML_IN_NODE (TABLE, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
	      GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW_GROUP, OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (TABLE, TABLE_ROW_GROUP,	      OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW,	    OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
	  GSF_XML_IN_NODE (TABLE_SETTINGS, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_EXPR, OO_NS_TABLE, "named-expression", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),
	    GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_RANGE, OO_NS_TABLE, "named-range", GSF_XML_NO_CONTENT, &oo_named_range, NULL),
	GSF_XML_IN_NODE (OFFICE_BODY, DB_RANGES, OO_NS_TABLE, "database-ranges", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (DB_RANGES, DB_RANGE, OO_NS_TABLE, "database-range", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (DB_RANGE, TABLE_SORT, OO_NS_TABLE, "sort", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_SORT, SORT_BY, OO_NS_TABLE, "sort-by", GSF_XML_NO_CONTENT, NULL, NULL),
	{ NULL }
};

/****************************************************************************/

static GnmExpr const *
errortype_renamer (char const *name, GnmExprList *args, GnmExprConventions *convs)
{
	GnmFunc *f = gnm_func_lookup ("ERROR.TYPE", NULL);
	if (f != NULL)
		return gnm_expr_new_funcall (f, args);
	return gnm_func_placeholder_factory (name, args, convs);
}

static GnmExpr const *
oo_unknown_hander (char const *name,
		   GnmExprList *args,
		   GnmExprConventions const *convs)
{
	if (0 == strncmp ("com.sun.star.sheet.addin.Analysis.get", name, 37)) {
		GnmFunc *f = gnm_func_lookup (name + 37, NULL);
		if (f != NULL)
			return gnm_expr_new_funcall (f, args);
		g_warning ("unknown function");
	}
	return gnm_func_placeholder_factory (name, args, convs);
}

static GnmExprConventions *
oo_conventions (void)
{
	GnmExprConventions *res = gnm_expr_conventions_new ();

	res->decode_ampersands = TRUE;
	res->intersection_char = '!';
	res->decimal_sep_dot = TRUE;
	res->argument_sep_semicolon = TRUE;
	res->array_col_sep_comma = TRUE;
	res->range_sep_colon = TRUE;
	res->output_argument_sep = ";";
	res->output_array_col_sep = ",";
	res->unknown_function_handler = oo_unknown_hander;
	res->ref_parser = oo_rangeref_parse;

#warning "TODO : OO adds a 'mode' parm to floor/ceiling"
#warning "TODO : OO missing 'A1' parm for address"
	res->function_rewriter_hash =
		g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (res->function_rewriter_hash,
		(gchar *)"ERRORTYPE", errortype_renamer);
	return res;
}

void
openoffice_file_open (GOFileOpener const *fo, IOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input);
void
openoffice_file_open (GOFileOpener const *fo, IOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input)
{
	GsfXMLInDoc	*doc;
	GsfInput	*content = NULL;
	GsfInput	*styles = NULL;
	GsfInput	*mimetype = NULL;
	GsfDocMetaData	*meta_data;
	GsfInfile	*zip;
	OOParseState	 state;
	GError		*err = NULL;
	char *old_num_locale, *old_monetary_locale;
	int i;

	zip = gsf_infile_zip_new (input, &err);
	if (zip == NULL) {
		g_return_if_fail (err != NULL);
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			err->message);
		g_error_free (err);
		return;
	}

	mimetype = gsf_infile_child_by_name (zip, "mimetype");
	if (mimetype == NULL) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			_("No stream named mimetype found."));
		g_object_unref (zip);
		return;
	} else {
		gsf_off_t size = gsf_input_size (mimetype);
		guint8 const *header = gsf_input_read (mimetype, size, NULL);
		if (!strncmp (mime_openofficeorg1, header, size))
			state.ver = OOO_VER_1;
		else if (!strncmp (mime_opendocument, header, size))
			state.ver = OOO_VER_OPENDOC;
		else {
			go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
						     _("Unknown mimetype for openoffice file."));
			g_object_unref (mimetype);
			g_object_unref (zip);
			return;
		}
		g_object_unref (mimetype);
	}

	content = gsf_infile_child_by_name (zip, "content.xml");
	if (content == NULL) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			 _("No stream named content.xml found."));
		g_object_unref (zip);
		return;
	}

	styles = gsf_infile_child_by_name (zip, "styles.xml");
	if (styles == NULL) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			 _("No stream named styles.xml found."));
		g_object_unref (content);
		g_object_unref (zip);
		return;
	}

	old_num_locale = g_strdup (go_setlocale (LC_NUMERIC, NULL));
	go_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (go_setlocale (LC_MONETARY, NULL));
	go_setlocale (LC_MONETARY, "C");
	go_set_untranslated_bools ();

	/* init */
	state.context = io_context;
	state.wb_view = wb_view;
	state.pos.wb	= wb_view_workbook (wb_view);
	state.zip = zip;
	state.pos.sheet = NULL;
	state.col_styles[0] = NULL;
	state.col_styles[1] = NULL;
	state.pos.eval.col	= -1;
	state.pos.eval.row	= -1;
	state.col_row_styles = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.cell_styles = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gnm_style_unref);
	state.formats = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) go_format_unref);
	state.cur_frame.graph_styles = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) clean_lists);
	state.cur_style.cell   = NULL;
	state.default_style_cell = NULL;
	state.cur_style_type   = OO_STYLE_UNKNOWN;
	state.sheet_order = NULL;
	state.exprconv = oo_conventions ();
	state.accum_fmt = NULL;
	state.cur_frame.has_legend = FALSE;

	if (state.ver == OOO_VER_OPENDOC) {
		GsfInput *meta_file =
			meta_file = gsf_infile_child_by_name (zip, "meta.xml");
		if (meta_file != NULL) {
			meta_data = gsf_doc_meta_data_new ();
			err = gsf_opendoc_metadata_read (meta_file, meta_data);
			g_object_set_data (G_OBJECT(state.pos.wb), "GsfDocMetaData", meta_data);
			g_object_unref (meta_file);
		}
	}

	if (styles != NULL) {
		GsfXMLInDoc *doc = gsf_xml_in_doc_new (styles_dtd, gsf_ooo_ns);
		gsf_xml_in_doc_parse (doc, styles, &state);
		gsf_xml_in_doc_free (doc);
		g_object_unref (styles);
	}

	doc  = gsf_xml_in_doc_new (
		(state.ver == OOO_VER_1) ? ooo1_content_dtd : opendoc_content_dtd,
		gsf_ooo_ns);
	if (gsf_xml_in_doc_parse (doc, content, &state)) {
		GsfInput *settings;

		/* get the sheet in the right order (in case something was
		 * created out of order implictly) */
		state.sheet_order = g_slist_reverse (state.sheet_order);
		workbook_sheet_reorder (state.pos.wb, state.sheet_order);
		g_slist_free (state.sheet_order);

		/* look for the view settings */
		if (state.ver == OOO_VER_1) {
			settings = gsf_infile_child_by_name (zip, "settings.xml");
			if (settings != NULL) {
				GsfXMLInDoc *sdoc = gsf_xml_in_doc_new (opencalc_settings_dtd, gsf_ooo_ns);
				gsf_xml_in_doc_parse (sdoc, settings, &state);
				gsf_xml_in_doc_free (sdoc);
				g_object_unref (settings);
			}
		}
	} else
		gnumeric_io_error_string (io_context, _("XML document not well formed!"));
	gsf_xml_in_doc_free (doc);

	if (state.default_style_cell)
		gnm_style_unref (state.default_style_cell);
	g_hash_table_destroy (state.col_row_styles);
	g_hash_table_destroy (state.cell_styles);
	g_hash_table_destroy (state.cur_frame.graph_styles);
	g_hash_table_destroy (state.formats);
	g_slist_free (state.col_styles[0]);
	g_slist_free (state.col_styles[1]);
	g_object_unref (content);

	g_object_unref (zip);

	i = workbook_sheet_count (state.pos.wb);
	while (i-- > 0)
		sheet_flag_recompute_spans (workbook_sheet_by_index (state.pos.wb, i));

	gnm_expr_conventions_free (state.exprconv);

	/* go_setlocale restores bools to locale translation */
	go_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	go_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);
}
