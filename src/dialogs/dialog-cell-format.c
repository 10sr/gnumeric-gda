/**
 * dialog-cell-format.c:  Implements a dialog to format cells.
 *
 * Author:
 *  Jody Goldberg <jgoldberg@home.com>
 *
 **/

#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "sheet.h"
#include "color.h"
#include "dialogs.h"
#include "utils-dialog.h"
#include "widgets/widget-font-selector.h"
#include "widgets/gnumeric-dashed-canvas-line.h"
#include "gnumeric-util.h"
#include "selection.h"
#include "ranges.h"
#include "format.h"
#include "formats.h"
#include "pattern.h"
#include "mstyle.h"
#include "application.h"
#include "workbook.h"
#include "commands.h"

#define GLADE_FILE "cell-format.glade"

/* The order corresponds to border_preset_buttons */
typedef enum
{
	BORDER_PRESET_NONE,
	BORDER_PRESET_OUTLINE,
	BORDER_PRESET_INSIDE,

	BORDER_PRESET_MAX
} BorderPresets;

/* The available format widgets */
typedef enum
{
    F_GENERAL,		F_DECIMAL_BOX,	F_SEPARATOR, 
    F_SYMBOL_LABEL,	F_SYMBOL,	F_DELETE,
    F_ENTRY,		F_LIST_SCROLL,	F_LIST,
    F_TEXT,		F_DECIMAL_SPIN,	F_NEGATIVE_SCROLL, 
    F_NEGATIVE,         F_MAX_WIDGET
} FormatWidget;

struct _FormatState;
typedef struct
{
	struct _FormatState *state;
	int cur_index;
	GtkToggleButton *current_pattern;
	GtkToggleButton *default_button;
	void (*draw_preview) (struct _FormatState *);
} PatternPicker;

typedef struct
{
	struct _FormatState *state;
	GdkColor	 *auto_color;
	GtkToggleButton  *custom, *autob;
	GnomeColorPicker *picker;
	GtkSignalFunc	  preview_update;

	gboolean	  is_auto;
	guint		  rgba;
	guint		  r, g, b;
} ColorPicker;

typedef struct
{
	struct _FormatState *state;
	GtkToggleButton  *button;
	StyleBorderType	  pattern_index;
	gboolean	  is_selected;	/* Is it selected */
	StyleBorderLocation   index;
	guint		  rgba;
	gboolean	  is_set;	/* Has the element been changed */
} BorderPicker;

typedef struct _FormatState
{
	GladeXML	*gui;
	GnomePropertyBox*dialog;
	gint		 page_signal;

	Sheet		*sheet;
	MStyle		*style, *result;
	Value		*value;

	int	 	 selection_mask;
	gboolean	 enable_edit;

	struct
	{
		GtkLabel	*preview;
		GtkBox		*box;
		GtkWidget	*widget[F_MAX_WIDGET];

		gchar const	*spec;
		gint		 current_type;
		int		 num_decimals;
		int		 negative_format;
		int		 currency_index;
		gboolean	 use_separator;
	} format;
	struct
	{
		GtkCheckButton	*wrap;
	} align;
	struct
	{
		FontSelector	*selector;
		ColorPicker	 color;
	} font;
	struct
	{
		GnomeCanvas	*canvas;
		GtkButton 	*preset[BORDER_PRESET_MAX];
		GnomeCanvasItem	*back;
		GnomeCanvasItem *lines[20];

		BorderPicker	 edge[STYLE_BORDER_EDGE_MAX];
		ColorPicker	 color;
		PatternPicker	 pattern;
	} border;
	struct
	{
		GnomeCanvas	*canvas;
		GnomeCanvasItem	*back;
		GnomeCanvasItem	*pattern_item;

		ColorPicker	 back_color, pattern_color;
		PatternPicker	 pattern;
	} back;
} FormatState;

/*****************************************************************************/
/* Some utility routines shared by all pages */

/*
 * A utility routine to help mark the attributes as being changed
 * VERY stupid for now.
 */
static void
fmt_dialog_changed (FormatState *state)
{
	/* Catch all the pseudo-events that take place while initializing */
	if (state->enable_edit)
		gnome_property_box_changed (state->dialog);
}

/* Default to the 'Format' page but remember which page we were on between
 * invocations */
static int fmt_dialog_page = 0;

/*
 * Callback routine to help remember which format tab was selected
 * between dialog invocations.
 */
static void
cb_page_select (GtkNotebook *notebook, GtkNotebookPage *page,
		gint page_num, gpointer user_data)
{
	fmt_dialog_page = page_num;
}

static void
cb_notebook_destroy (GtkObject *obj, FormatState *state)
{
	gtk_signal_disconnect (obj, state->page_signal);
}

/*
 * Callback routine to give radio button like behaviour to the
 * set of toggle buttons used for line & background patterns.
 */
static void
cb_toggle_changed (GtkToggleButton *button, PatternPicker *picker)
{
	if (gtk_toggle_button_get_active (button) &&
	    picker->current_pattern != button) {
		gtk_toggle_button_set_active(picker->current_pattern, FALSE);
		picker->current_pattern = button;
		picker->cur_index =
				GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (button), "index"));
		if (picker->draw_preview)
			picker->draw_preview (picker->state);
	}
}

/*
 * Setup routine to associate images with toggle buttons
 * and to adjust the relief so it looks nice.
 */
static void
setup_pattern_button (GladeXML  *gui,
		      char const * const name,
		      PatternPicker *picker,
		      gboolean const flag,
		      int const index,
		      gboolean select)
{
	GtkWidget * tmp = glade_xml_get_widget (gui, name);
	if (tmp != NULL) {
		GtkButton *button = GTK_BUTTON (tmp);
		if (flag) {
			GtkWidget * image = gnumeric_load_image(name);
			if (image != NULL)
				gtk_container_add(GTK_CONTAINER (tmp), image);
		}

		if (picker->current_pattern == NULL) {
			picker->default_button = GTK_TOGGLE_BUTTON (button);
			picker->current_pattern = picker->default_button;
			picker->cur_index = index;
		}

		gtk_button_set_relief (button, GTK_RELIEF_NONE);
		gtk_signal_connect (GTK_OBJECT (button), "toggled",
				    GTK_SIGNAL_FUNC (cb_toggle_changed),
				    picker);
		gtk_object_set_data (GTK_OBJECT (button), "index", 
				     GINT_TO_POINTER (index));

		/* Set the state AFTER the signal to get things redrawn correctly */
		if (select) {
			picker->cur_index = index;
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
						      TRUE);
		}
	} else
		g_warning ("CellFormat : Unexpected missing glade widget");
}

static void
cb_custom_color_selected (GtkObject *obj, ColorPicker *state)
{
	/* The color picker was clicked.  Toggle the custom radio button */
	gtk_toggle_button_set_active (state->custom, TRUE);
}

static void
cb_auto_color_selected (GtkObject *obj, ColorPicker *state)
{
	/* TODO TODO TODO : Some day we need to properly support 'Auto' colors.
	 *                  We should calculate them on the fly rather than hard coding
	 *                  in the initialization.
	 */
	if ((state->is_auto = gtk_toggle_button_get_active (state->autob))) {
		/* The auto radio was clicked.  Reset the color in the picker */
		gnome_color_picker_set_i16 (state->picker,
					    state->auto_color->red,
					    state->auto_color->green,
					    state->auto_color->blue,
					    0xffff);

		state->preview_update (state->picker,
				       state->auto_color->red,
				       state->auto_color->green,
				       state->auto_color->blue,
				       0, state->state);
	}
}

static void
setup_color_pickers (GladeXML	 *gui,
		     char const  * const picker_name,
		     char const  * const custom_radio_name,
		     char const  * const auto_name,
		     ColorPicker *color_state,
		     FormatState *state,
		     GdkColor	 *auto_color,
		     GtkSignalFunc preview_update,
		     MStyleElementType const e,
		     MStyle	 *mstyle)
{
	StyleColor *mcolor = NULL;

	GtkWidget *tmp = glade_xml_get_widget (gui, picker_name);
	g_return_if_fail (tmp && NULL != (color_state->picker = GNOME_COLOR_PICKER (tmp)));

	tmp = glade_xml_get_widget (gui, custom_radio_name);
	g_return_if_fail (tmp && NULL != (color_state->custom = GTK_TOGGLE_BUTTON (tmp)));

	tmp = glade_xml_get_widget (gui, auto_name);
	g_return_if_fail (tmp && NULL != (color_state->autob = GTK_TOGGLE_BUTTON (tmp)));

	color_state->auto_color = auto_color;
	color_state->preview_update = preview_update;
	color_state->state = state;
	color_state->is_auto = TRUE;

	gtk_signal_connect (GTK_OBJECT (color_state->picker), "clicked",
			    GTK_SIGNAL_FUNC (cb_custom_color_selected),
			    color_state);
	gtk_signal_connect (GTK_OBJECT (color_state->autob), "clicked",
			    GTK_SIGNAL_FUNC (cb_auto_color_selected),
			    color_state);

	/* Toggle the auto button to initialize the color to Auto */
	gtk_toggle_button_set_active (color_state->autob, FALSE);
	gtk_toggle_button_set_active (color_state->autob, TRUE);

	/* Connect to the sample canvas and redraw it */
	gtk_signal_connect (GTK_OBJECT (color_state->picker), "color_set",
			    preview_update, state);


	if (e != MSTYLE_ELEMENT_UNSET &&
	    !mstyle_is_element_conflict (mstyle, e))
		mcolor = mstyle_get_color (mstyle, e);

	if (mcolor != NULL) {
		gnome_color_picker_set_i16 (color_state->picker,
					    mcolor->red, mcolor->green,
					    mcolor->blue, 0xffff);
		gtk_toggle_button_set_active (color_state->custom, TRUE);
		(*preview_update) (state,
				   mcolor->red,
				   mcolor->green,
				   mcolor->blue,
				   0xffff, state);
	}

}

static StyleColor *
picker_style_color (ColorPicker const *c)
{
	return style_color_new (c->r, c->g, c->b);
}

/*
 * Utility routine to load an image and insert it into a
 * button of the same name.
 */
static GtkWidget *
init_button_image (GladeXML *gui, char const * const name)
{
	GtkWidget *tmp = glade_xml_get_widget (gui, name);
	if (tmp != NULL) {
		GtkWidget * image = gnumeric_load_image(name);
		if (image != NULL)
			gtk_container_add (GTK_CONTAINER (tmp), image);
	}

	return tmp;
}

/*****************************************************************************/

static void
draw_format_preview (FormatState *state)
{
	static char const * const zeros = "000000000000000000000000000000";
	static char const * const qmarks = "??????????????????????????????";
	FormatFamily const page = state->format.current_type;
	GString		*new_format = g_string_new ("");
	gchar		*preview;
	StyleColor	*preview_color;
	StyleFormat	*sf = NULL;

	/* Update the format based on the current selections and page */
	switch (page)
	{
	case FMT_GENERAL :
	case FMT_TEXT :
		g_string_append (new_format, cell_formats[page][0]);
		break;

	case FMT_CURRENCY :
		g_string_append (new_format,
				 currency_symbols[state->format.currency_index].symbol);

		/* Non simple currencies require a spacer */
		if (currency_symbols[state->format.currency_index].symbol[0] == '[')
			g_string_append_c (new_format, ' ');

	case FMT_NUMBER :
		if (state->format.use_separator) {
			g_string_append_c (new_format, '#');
			g_string_append (new_format, format_get_thousand ());
			g_string_append (new_format, "##0");
		} else
			g_string_append_c (new_format, '0');

		if (state->format.num_decimals > 0) {
			g_return_if_fail (state->format.num_decimals <= 30);

			g_string_append (new_format, format_get_decimal ());
			g_string_append (new_format, zeros + 30-state->format.num_decimals);
		}

		/* There are negatives */
		if (state->format.negative_format > 0) {
			GString *tmp = g_string_new ("");
			g_string_append (tmp, new_format->str);
			switch (state->format.negative_format) {
			case 1 : g_string_append (tmp, _(";[Red]"));
				 break;
			case 2 : g_string_append (tmp, _("_);("));
				 break;
			case 3 : g_string_append (tmp, _("_);[Red]("));
				 break;
			default :
				 g_assert_not_reached ();
			};

			g_string_append (tmp, new_format->str);

			if (state->format.negative_format >= 2)
				g_string_append_c (tmp, ')');
			g_string_free (new_format, TRUE);
			new_format = tmp;
		}
		break;

	case FMT_ACCOUNT :
		g_string_append (new_format, "_(");
		g_string_append (new_format,
				 currency_symbols[state->format.currency_index].symbol);
		g_string_append (new_format, "*#");
		g_string_append (new_format, format_get_thousand ());
		g_string_append (new_format, "##0");
		if (state->format.num_decimals > 0) {
			g_return_if_fail (state->format.num_decimals <= 30);

			g_string_append (new_format, format_get_decimal ());
			g_string_append (new_format, zeros + 30-state->format.num_decimals);
		}
		g_string_append (new_format, "_);_(");
		g_string_append (new_format,
				 currency_symbols[state->format.currency_index].symbol);
		g_string_append (new_format, "*(#");
		g_string_append (new_format, format_get_thousand ());
		g_string_append (new_format, "##0");
		if (state->format.num_decimals > 0) {
			g_return_if_fail (state->format.num_decimals <= 30);

			g_string_append (new_format, format_get_decimal ());
			g_string_append (new_format, zeros + 30-state->format.num_decimals);
		}
		g_string_append (new_format, ");_(");
		g_string_append (new_format,
				 currency_symbols[state->format.currency_index].symbol);
		g_string_append (new_format, "*\"-\"");
		g_string_append (new_format, qmarks + 30-state->format.num_decimals);
		g_string_append (new_format, "_);_(@_)");
		break;

	case FMT_PERCENT :
	case FMT_SCIENCE :
		g_string_append_c (new_format, '0');
		if (state->format.num_decimals > 0) {
			g_return_if_fail (state->format.num_decimals <= 30);

			g_string_append (new_format, format_get_decimal ());
			g_string_append (new_format, zeros + 30-state->format.num_decimals);
		}
		if (page == FMT_PERCENT)
			g_string_append_c (new_format, '%');
		else
			g_string_append (new_format, "E+00");

	default :
		break;
	};

	if (new_format->len > 0)
		gtk_entry_set_text (GTK_ENTRY (state->format.widget[F_ENTRY]),
				    new_format->str);
				    
	g_string_free (new_format, TRUE);

	/* Nothing to sample. */
	if (state->value == NULL)
		return;

	/* The first time through lets initialize */
	if (state->format.preview == NULL) {
		state->format.preview =
		    GTK_LABEL (glade_xml_get_widget (state->gui, "format_sample"));
	}

	g_return_if_fail (state->format.preview != NULL);

	if (mstyle_is_element_set (state->result, MSTYLE_FORMAT))
		sf = mstyle_get_format (state->result);
	else if (!mstyle_is_element_conflict (state->style, MSTYLE_FORMAT))
		sf = mstyle_get_format (state->style);

	if (sf == NULL)
		return;

	preview = format_value (sf, state->value, &preview_color);
	gtk_label_set_text (state->format.preview, preview);
	g_free (preview);
}

static void
fillin_negative_samples (FormatState *state, int const page)
{
	static char const * const decimals = "098765432109876543210987654321";
	static char const * const formats[4] = {
		"-%s%s3%s210%s%s",
		"%s%s3%s210%s%s",
		"(%s%s3%s210%s%s)",
		"(%s%s3%s210%s%s)"
	};
	char const * const sep = state->format.use_separator
		? format_get_thousand () : "";
	int const n = 30 - state->format.num_decimals;

	char const * const decimal = (state->format.num_decimals > 0)
		? format_get_decimal () : "";
	char const *space = "", *currency;

	GtkCList *cl;
	char buf[50];
	int i;

	g_return_if_fail (page == 1 || page == 2);
	g_return_if_fail (state->format.num_decimals <= 30);

	cl = GTK_CLIST (state->format.widget[F_NEGATIVE]);
	if (page == 2) {
		currency = currency_symbols[state->format.currency_index].symbol;
		/*
		 * FIXME : This should be better hidden.
		 * Ideally the render would do this for us.
		 */
		if (currency[0] == '[' && currency[1] == '$') {
			char const *end = strchr (currency+2, ']');
			currency = g_strndup (currency+2, end-currency-2);
			space = " ";
		} else
			currency = g_strdup (currency);
	} else
		currency = "";

	for (i = 4; --i >= 0 ; ) {
		sprintf (buf, formats[i], currency, space, sep, decimal, decimals + n);
		gtk_clist_set_text (cl, i, 0, buf);
	}

	/* If non empty then free the string */
	if (*currency)
		g_free ((char *)currency);
}

static void
cb_decimals_changed (GtkEditable *editable, FormatState *state)
{
	int const page = state->format.current_type;

	state->format.num_decimals =
		gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (editable));

	if (page == 1 || page == 2)
		fillin_negative_samples (state, page);

	draw_format_preview (state);
}

static void
cb_separator_toggle (GtkObject *obj, FormatState *state)
{
	state->format.use_separator = 
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (obj));
	fillin_negative_samples (state, 1);

	draw_format_preview (state);
}

static int
fmt_dialog_init_fmt_list (GtkCList *cl, char const * const *formats,
			 char const * const cur_format,
			 int select, int *count)
{
	int j;

	for (j = 0; formats [j]; ++j) {
		gchar *t [1];

		t [0] = _(formats [j]);
		gtk_clist_append (cl, t);

		/* CHECK : Do we really want to be case insensitive ? */
		if (!g_strcasecmp (formats[j], cur_format))
			select = j + *count;
	}

	*count += j;
	return select;
}

static void
fmt_dialog_enable_widgets (FormatState *state, int page)
{
	static FormatWidget contents[12][7] =
	{
		/* General */
		{ F_GENERAL, F_MAX_WIDGET },
		/* Number */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_SEPARATOR, 
		  F_NEGATIVE_SCROLL, F_NEGATIVE, F_MAX_WIDGET },
		/* Currency */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_SYMBOL_LABEL, F_SYMBOL, 
		  F_NEGATIVE_SCROLL, F_NEGATIVE, F_MAX_WIDGET },
		/* Accounting */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_SYMBOL_LABEL, F_SYMBOL, F_MAX_WIDGET },
		/* Date */
		{ F_LIST_SCROLL, F_LIST, F_MAX_WIDGET },
		/* Time */
		{ F_LIST_SCROLL, F_LIST, F_MAX_WIDGET },
		/* Percentage */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_MAX_WIDGET },
		/* Fraction */
		{ F_LIST_SCROLL, F_LIST, F_MAX_WIDGET },
		/* Scientific */
		{ F_DECIMAL_BOX, F_DECIMAL_SPIN, F_MAX_WIDGET },
		/* Text */
		{ F_TEXT, F_MAX_WIDGET },
		/* Special */
		{ F_MAX_WIDGET },
		/* Custom */
		{ F_ENTRY, F_LIST_SCROLL, F_LIST, F_DELETE, F_MAX_WIDGET },
	};

	int const old_page = state->format.current_type;
	int i, count = 0;
	FormatWidget tmp;

	/* Hide widgets from old page */
	if (old_page >= 0)
		for (i = 0; (tmp = contents[old_page][i]) != F_MAX_WIDGET ; ++i)
			gtk_widget_hide (state->format.widget[tmp]);

	/* Set the default format if appropriate */
	if (page == FMT_GENERAL || page == FMT_ACCOUNT || page == FMT_FRACTION || page == FMT_TEXT) {
		FormatCharacteristics info;
		int list_elem = 0;
		if (page == cell_format_classify (state->format.spec, &info))
			list_elem = info.list_element;

		gtk_entry_set_text (GTK_ENTRY (state->format.widget[F_ENTRY]),
				    cell_formats[page][list_elem]);
	}

	state->format.current_type = page;
	for (i = 0; (tmp = contents[page][i]) != F_MAX_WIDGET ; ++i) {
		GtkWidget *w = state->format.widget[tmp];
		gtk_widget_show (w);

		/* The sample is always the 1st widget */
		gtk_box_reorder_child (state->format.box, w, i+1);

		if (tmp == F_LIST) {
			GtkCList *cl = GTK_CLIST (w);
			int select = -1, start = 0, end = -1;

			switch (page) {
			case 4: case 5: case 7:
				start = end = page;
				break;

			case 11:
				start = 0; end = 8;
				break;

			default :
				g_assert_not_reached ();
			};

			gtk_clist_freeze (cl);
			gtk_clist_clear (cl);
			gtk_clist_set_auto_sort (cl, FALSE);

			for (; start <= end ; ++start)
				select = fmt_dialog_init_fmt_list (cl,
						cell_formats [start],
						state->format.spec,
						select, &count);
			gtk_clist_thaw (cl);

			/* If this is the custom page and the format has
			 * not been found append it */
			/* TODO We should add the list of other custom formats created.
			 *      It should be easy.  All that is needed is a way to differentiate
			 *      the std formats and the custom formats in the StyleFormat hash.
			 */
			if  (page == 11 && select == -1)
				gtk_entry_set_text (GTK_ENTRY (state->format.widget[F_ENTRY]),
						    (gchar *)state->format.spec);
			else if (select < 0)
				select = 0;

			if (select >= 0)
				gtk_clist_select_row (cl, select, 0);
		} else if (tmp == F_NEGATIVE)
			fillin_negative_samples (state, page);
	}

	draw_format_preview (state);
}

/*
 * Callback routine to manage the relationship between the number
 * formating radio buttons and the widgets required for each mode.
 */
static void
cb_format_changed (GtkObject *obj, FormatState *state)
{
	GtkToggleButton *button = GTK_TOGGLE_BUTTON (obj);
	if (gtk_toggle_button_get_active (button))
		fmt_dialog_enable_widgets ( state,
			GPOINTER_TO_INT (gtk_object_get_data (obj, "index")));
}

static void
cb_format_entry (GtkEditable *w, FormatState *state)
{
	gchar const *tmp = gtk_entry_get_text (GTK_ENTRY (w));

	/* If the format didn't change don't react */
	if (!g_strcasecmp (state->format.spec, tmp))
		return;

	g_free ((char *)state->format.spec);
	state->format.spec = g_strdup (tmp);
	mstyle_set_format (state->result, state->format.spec);
	fmt_dialog_changed (state);
	draw_format_preview (state);
}

static void
cb_format_list_select (GtkCList *clist, gint row, gint column,
		       GdkEventButton *event, FormatState *state)
{
	gchar *text;
	gtk_clist_get_text (clist, row, column, &text);
	gtk_entry_set_text (GTK_ENTRY (state->format.widget[F_ENTRY]), text);
}

static void
cb_format_currency_select (GtkEditable *w, FormatState *state)
{
	gchar const *tmp = gtk_entry_get_text (GTK_ENTRY (w));

	/* There must be a better way than this */
	int i;
	for (i = 0; currency_symbols[i].symbol != NULL ; ++i)
		if (!strcmp (_(currency_symbols [i].description), tmp)) {
			state->format.currency_index = i;
			break;
		}

	fillin_negative_samples (state, state->format.current_type);
	draw_format_preview (state);
}

/*
 * Callback routine to make the selected line visible once our geometry is
 * known
 */
static void
cb_format_list_size_allocate (GtkCList *clist, GtkAllocation *allocation,
		       FormatState *state)
{
	gint r, rmin, rmax;

	if (!clist->selection)
		return;

	r = (gint) clist->selection->data;

	/* GTK visbility calculation sometimes is too optimistic */
	rmin = (r - 1 > 0) ? r - 1 : 0;
	rmax = (r + 1 < clist->rows - 1) ? r + 1 : clist->rows - 1;
	if (! (gtk_clist_row_is_visible (clist, rmin) &&
	       gtk_clist_row_is_visible (clist, rmax)))
		gtk_clist_moveto (clist, r, 0, 0.5, 0.);
}

static void
cb_format_negative_form_selected (GtkCList *clist, gint row, gint column,
				  GdkEventButton *event, FormatState *state)
{
	state->format.negative_format = row;
	draw_format_preview (state);
}

static void
fmt_dialog_init_format_page (FormatState *state)
{
	static char const * const format_buttons[] = {
	    "format_general",	"format_number",
	    "format_currency",	"format_accounting",
	    "format_date",	"format_time",
	    "format_percentage","format_fraction",
	    "format_scientific","format_text",
	    "format_special",	"format_custom",
	    NULL
	};

	/* The various format widgets */
	static char const * const widget_names[] =
	{
		"format_general_label",	"format_decimal_box",
		"format_separator",	"format_symbol_label",
		"format_symbol_select",	"format_delete",
		"format_entry",		"format_list_scroll",
		"format_list",		"format_text_label",
		"format_number_decimals", "format_negative_scroll",
		"format_negatives",     NULL
	};

	GtkWidget *tmp;
	GtkCList *cl;
	GtkCombo *combo;
	char const * name;
	int i, j, page;
	FormatCharacteristics info;

	/* Get the current format */
	char const * format = cell_formats [0][0];
	if (!mstyle_is_element_conflict (state->style, MSTYLE_FORMAT))
		format = mstyle_get_format (state->style)->format;

	state->format.preview = NULL;
	state->format.spec = g_strdup (format);

	/* The handlers will set the format family later.  -1 flags that
	 * all widgets are already hidden. */
	state->format.current_type = -1;

	/* Attempt to extract general parameters from the current format */
	if ((page = cell_format_classify (state->format.spec, &info)) < 0)
		page = 11; /* Default to custom */

	/* Even if the format was not recognized it has set intelligent defaults */
	state->format.use_separator = info.thousands_sep;
	state->format.num_decimals = info.num_decimals;
	state->format.negative_format = info.negative_fmt;
	state->format.currency_index = info.currency_symbol_index;

	state->format.box = GTK_BOX (glade_xml_get_widget (state->gui, "format_box"));

	/* Collect all the required format widgets and hide them */
	for (i = 0; (name = widget_names[i]) != NULL; ++i) {
		tmp = glade_xml_get_widget (state->gui, name);

		g_return_if_fail (tmp != NULL);

		gtk_widget_hide (tmp);
		state->format.widget[i] = tmp;
	}

	/* setup the red elements of the negative list box */
	cl = GTK_CLIST (state->format.widget[F_NEGATIVE]);
	if (cl != NULL) {
		gchar *dummy[1] = { "321" };
		GtkStyle *style;

		/* stick in some place holders */
		for (j = 4; --j >= 0 ;)
		    gtk_clist_append  (cl, dummy);

		/* Make the 2nd and 4th elements red */
		gtk_widget_ensure_style (GTK_WIDGET (cl));
		style = gtk_widget_get_style (GTK_WIDGET (cl));
		style = gtk_style_copy (style);
		style->fg[GTK_STATE_NORMAL] = gs_red;
		style->fg[GTK_STATE_ACTIVE] = gs_red;
		style->fg[GTK_STATE_PRELIGHT] = gs_red;
		gtk_clist_set_cell_style (cl, 1, 0, style);
		gtk_clist_set_cell_style (cl, 3, 0, style);
		gtk_style_unref (style);

		gtk_clist_select_row (cl, state->format.negative_format, 0);
		gtk_signal_connect (GTK_OBJECT (cl),
				    "select-row",
				    GTK_SIGNAL_FUNC (cb_format_negative_form_selected),
				    state);
		gtk_clist_column_titles_passive (cl);
	}

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->format.widget[F_DECIMAL_SPIN]),
				   state->format.num_decimals);

	/* Catch changes to the spin box */
	(void) gtk_signal_connect (
		GTK_OBJECT (state->format.widget[F_DECIMAL_SPIN]),
		"changed", GTK_SIGNAL_FUNC (cb_decimals_changed),
		state);

	/* Catch <return> in the spin box */
	gnome_dialog_editable_enters (
		GNOME_DIALOG (state->dialog),
		GTK_EDITABLE (state->format.widget[F_DECIMAL_SPIN]));

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->format.widget[F_SEPARATOR]),
				      state->format.use_separator);

	/* Setup special handlers for : Numbers */
	gtk_signal_connect (GTK_OBJECT (state->format.widget[F_SEPARATOR]),
			    "toggled",
			    GTK_SIGNAL_FUNC (cb_separator_toggle),
			    state);

	gtk_signal_connect (GTK_OBJECT (state->format.widget[F_LIST]),
			    "select-row",
			    GTK_SIGNAL_FUNC (cb_format_list_select),
			    state);
      
	gtk_signal_connect (GTK_OBJECT (state->format.widget[F_LIST]),
			    "size-allocate",
			    GTK_SIGNAL_FUNC (cb_format_list_size_allocate),
			    state);

	/* Setup handler Currency & Accounting currency symbols */
	combo = GTK_COMBO (state->format.widget[F_SYMBOL]);
	if (combo != NULL) {
		GList *l = NULL;
		gtk_combo_set_value_in_list (combo, TRUE, FALSE);
		gtk_combo_set_case_sensitive (combo, FALSE);
		gtk_entry_set_editable (GTK_ENTRY (combo->entry), FALSE);

		for (i = 0; currency_symbols[i].symbol != NULL ; ++i) {
			gchar *descr = _(currency_symbols [i].description);
			l = g_list_append (l, descr);
		}

		gtk_combo_set_popdown_strings (combo, l);
		gtk_entry_set_text (GTK_ENTRY (combo->entry), 
				    _(currency_symbols [state->format.currency_index].description));

		gtk_signal_connect (GTK_OBJECT (combo->entry),
				    "changed", GTK_SIGNAL_FUNC (cb_format_currency_select),
				    state);
	}

	/* Setup special handler for Custom */
	gtk_signal_connect (GTK_OBJECT (state->format.widget[F_ENTRY]),
			    "changed", GTK_SIGNAL_FUNC(cb_format_entry),
			    state);
	gnome_dialog_editable_enters (GNOME_DIALOG (state->dialog),
				      GTK_EDITABLE (state->format.widget[F_ENTRY]));
	
	/* Setup format buttons to toggle between the format pages */
	for (i = 0; (name = format_buttons[i]) != NULL; ++i) {
		tmp = glade_xml_get_widget (state->gui, name);
		if (tmp == NULL)
			continue;

		gtk_object_set_data (GTK_OBJECT (tmp), "index", 
				     GINT_TO_POINTER (i));
		gtk_signal_connect (GTK_OBJECT (tmp), "toggled",
				    GTK_SIGNAL_FUNC (cb_format_changed),
				    state);

		if (i == page) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tmp), TRUE);
			if (i == 0)
				/* We have to invoke callback ourselves */
				cb_format_changed (GTK_OBJECT (tmp), state);
		}
	}

	draw_format_preview (state);
}

/*****************************************************************************/

static void
cb_align_h_toggle (GtkToggleButton *button, FormatState *state)
{
	if (!gtk_toggle_button_get_active (button))
		return;

	mstyle_set_align_h (
		state->result,
		GPOINTER_TO_INT (gtk_object_get_data (
		GTK_OBJECT (button), "align")));
	fmt_dialog_changed (state);
}

static void
cb_align_v_toggle (GtkToggleButton *button, FormatState *state)
{
	if (!gtk_toggle_button_get_active (button))
		return;

	mstyle_set_align_v (
		state->result,
		GPOINTER_TO_INT (gtk_object_get_data (
		GTK_OBJECT (button), "align")));
	fmt_dialog_changed (state);
}

static void
cb_align_wrap_toggle (GtkToggleButton *button, FormatState *state)
{
	mstyle_set_fit_in_cell (state->result,
				gtk_toggle_button_get_active (button));
	fmt_dialog_changed (state);
}

static void
fmt_dialog_init_align_radio (char const * const name,
			     int const val, int const target,
			     FormatState *state,
			     GtkSignalFunc handler)
{
	GtkWidget *tmp = glade_xml_get_widget (state->gui, name);
	if (tmp != NULL) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tmp),
					      val == target);
		gtk_object_set_data (GTK_OBJECT (tmp), "align", 
				     GINT_TO_POINTER (val));
		gtk_signal_connect (GTK_OBJECT (tmp),
				    "toggled", handler,
				    state);
	}
}

static void
fmt_dialog_init_align_page (FormatState *state)
{
	static struct
	{
		char const * const	name;
		StyleHAlignFlags	align;
	} const h_buttons[] =
	{
	    { "halign_left",	HALIGN_LEFT },
	    { "halign_center",	HALIGN_CENTER },
	    { "halign_right",	HALIGN_RIGHT },
	    { "halign_general",	HALIGN_GENERAL },
	    { "halign_justify",	HALIGN_JUSTIFY },
	    { "halign_fill",	HALIGN_FILL },
	    { NULL }
	};
	static struct
	{
		char const * const	name;
		StyleVAlignFlags	align;
	} const v_buttons[] =
	{
	    { "valign_top", VALIGN_TOP },
	    { "valign_center", VALIGN_CENTER },
	    { "valign_bottom", VALIGN_BOTTOM },
	    { "valign_justify", VALIGN_JUSTIFY },
	    { NULL }
	};

	gboolean wrap = FALSE;
	StyleHAlignFlags    h = HALIGN_GENERAL;
	StyleVAlignFlags    v = VALIGN_CENTER;
	char const *name;
	int i;

	if (!mstyle_is_element_conflict (state->style, MSTYLE_ALIGN_H))
		h = mstyle_get_align_h (state->style);
	if (!mstyle_is_element_conflict (state->style, MSTYLE_ALIGN_V))
		v = mstyle_get_align_v (state->style);

	/* Setup the horizontal buttons */
	for (i = 0; (name = h_buttons[i].name) != NULL; ++i)
		fmt_dialog_init_align_radio (name, h_buttons[i].align,
					     h, state,
					     GTK_SIGNAL_FUNC (cb_align_h_toggle));

	/* Setup the vertical buttons */
	for (i = 0; (name = v_buttons[i].name) != NULL; ++i)
		fmt_dialog_init_align_radio (name, v_buttons[i].align,
					     v, state,
					     GTK_SIGNAL_FUNC (cb_align_v_toggle));

	/* Setup the wrap button, and assign the current value */
	if (!mstyle_is_element_conflict (state->style, MSTYLE_FIT_IN_CELL))
		wrap = mstyle_get_fit_in_cell (state->style);

	state->align.wrap =
	    GTK_CHECK_BUTTON (glade_xml_get_widget (state->gui, "align_wrap"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->align.wrap),
				      wrap);
	gtk_signal_connect (GTK_OBJECT (state->align.wrap), "toggled",
			    GTK_SIGNAL_FUNC (cb_align_wrap_toggle),
			    state);
}

/*****************************************************************************/

/*
 * A callback to set the font color.
 * It is called whenever the color picker changes value.
 */
static void
cb_font_preview_color (GtkObject *obj, guint r, guint g, guint b, guint a,
		       FormatState *state)
{
	GtkStyle *style;
	GdkColor col;
	state->font.color.r = col.red   = r;
	state->font.color.g = col.green = g;
	state->font.color.b = col.blue  = b;

	style = gtk_style_copy (state->font.selector->font_preview->style);
	style->fg[GTK_STATE_NORMAL] = col;
	style->fg[GTK_STATE_ACTIVE] = col;
	style->fg[GTK_STATE_PRELIGHT] = col;
	style->fg[GTK_STATE_SELECTED] = col;
	gtk_widget_set_style (state->font.selector->font_preview, style);
	gtk_style_unref (style);

	mstyle_set_color (state->result, MSTYLE_COLOR_FORE,
			  picker_style_color (&state->font.color));
	fmt_dialog_changed (state);
}

static void
cb_font_changed (GtkWidget *widget, GtkStyle *previous_style, FormatState *state)
{
	FontSelector *font_sel;
	GnomeDisplayFont *gnome_display_font;
	GnomeFont *gnome_font;
	char *family_name;
	double height;

	g_return_if_fail (state != NULL);
	font_sel = state->font.selector;
	g_return_if_fail (font_sel != NULL);
	gnome_display_font = font_sel->display_font;

	if (!gnome_display_font)
		return;

	gnome_font = gnome_display_font->gnome_font;
	family_name = gnome_font->fontmap_entry->familyname;
	height = gnome_display_font->gnome_font->size;

	if (state->enable_edit) {
		mstyle_set_font_name   (state->result, family_name);
		mstyle_set_font_size   (state->result, gnome_font->size);
		mstyle_set_font_bold   (state->result,
					gnome_font->fontmap_entry->weight_code >=
					GNOME_FONT_BOLD);
		mstyle_set_font_italic (state->result, gnome_font->fontmap_entry->italic);

		fmt_dialog_changed (state);
	}
}

/* Manually insert the font selector, and setup signals */
static void
fmt_dialog_init_font_page (FormatState *state)
{
	GtkWidget *tmp = font_selector_new ();
	FontSelector *font_widget = FONT_SELECTOR (tmp);
	GtkWidget *container = glade_xml_get_widget (state->gui, "font_box");

	g_return_if_fail (container != NULL);

	/* TODO : How to insert the font box in the right place initially */
	gtk_widget_show (tmp);
	gtk_box_pack_start (GTK_BOX (container), tmp, TRUE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (container), tmp, 0);

	gnome_dialog_editable_enters (GNOME_DIALOG (state->dialog),
				      GTK_EDITABLE (font_widget->font_name_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (state->dialog),
				      GTK_EDITABLE (font_widget->font_style_entry));
	gnome_dialog_editable_enters (GNOME_DIALOG (state->dialog),
				      GTK_EDITABLE (font_widget->font_size_entry));

	/* When the font preview changes flag we know the style has changed.
	 * This catches color and font changes
	 */
	gtk_signal_connect (GTK_OBJECT (font_widget->font_preview),
			    "style_set",
			    GTK_SIGNAL_FUNC (cb_font_changed), state);

	state->font.selector = FONT_SELECTOR (font_widget);

	if (!mstyle_is_element_conflict (state->style, MSTYLE_FONT_NAME))
		font_selector_set_name (state->font.selector,
					mstyle_get_font_name (state->style));

	if (!mstyle_is_element_conflict (state->style, MSTYLE_FONT_BOLD) &&
	    !mstyle_is_element_conflict (state->style, MSTYLE_FONT_ITALIC))
		font_selector_set_style (state->font.selector,
					 mstyle_get_font_bold (state->style),
					 mstyle_get_font_italic (state->style));
	if (!mstyle_is_element_conflict (state->style, MSTYLE_FONT_SIZE))
		font_selector_set_points (state->font.selector,
					  mstyle_get_font_size (state->style));

	/* Set the resolution scaling factor */
	font_selector_set_screen_res (state->font.selector,
				      application_display_dpi_get (TRUE),
				      application_display_dpi_get (FALSE));
}

/*****************************************************************************/

static void
draw_pattern_preview (FormatState *state)
{
	/* The first time through lets initialize */
	if (state->back.canvas == NULL) {
		state->back.canvas =
		    GNOME_CANVAS (glade_xml_get_widget (state->gui, "back_sample"));
	}

	fmt_dialog_changed (state);

	/* If background is auto (none) : then remove any patterns or backgrounds */
	if (state->back.back_color.is_auto) {
		if (state->back.back != NULL) {
			gtk_object_destroy (GTK_OBJECT (state->back.back));
			state->back.back = NULL;
		}
		if (state->back.pattern_item != NULL) {
			gtk_object_destroy (GTK_OBJECT (state->back.pattern_item));
			state->back.pattern_item = NULL;

			/* This will recursively call draw_pattern_preview */
			gtk_toggle_button_set_active (state->back.pattern.default_button,
						      TRUE);
			/* This will recursively call draw_pattern_preview */
			gtk_toggle_button_set_active (state->back.pattern_color.autob,
						      TRUE);
			return;
		}

		if (state->enable_edit) {
			/* We can clear the background by specifying a pattern of 0 */
			mstyle_set_pattern (state->result, 0);

			/* Clear the colours just in case (The actual colours are irrelevant */
			mstyle_set_color (state->result, MSTYLE_COLOR_BACK,
					  style_color_new (0xffff, 0xffff, 0xffff));

			mstyle_set_color (state->result, MSTYLE_COLOR_PATTERN,
					  style_color_new (0x0, 0x0, 0x0));
		}

	/* BE careful just in case the initialization failed */
	} else if (state->back.canvas != NULL) {
		GnomeCanvasGroup *group =
			GNOME_CANVAS_GROUP (gnome_canvas_root (state->back.canvas));

		/* Create the background if necessary */
		if (state->back.back == NULL) {
			state->back.back = GNOME_CANVAS_ITEM (
				gnome_canvas_item_new (
					group,
					gnome_canvas_rect_get_type (),
					"x1", 0.,	"y1", 0.,
					"x2", 90.,	"y2", 50.,
					"width_pixels", (int) 5,
				       "fill_color_rgba",
				       state->back.back_color.rgba,
					NULL));
		} else
			gnome_canvas_item_set (
				GNOME_CANVAS_ITEM (state->back.back),
				"fill_color_rgba", state->back.back_color.rgba,
				NULL);

		if (state->enable_edit) {
			mstyle_set_pattern (state->result, state->back.pattern.cur_index);
			mstyle_set_color (state->result, MSTYLE_COLOR_BACK,
					  picker_style_color (&state->back.back_color));

			if (state->back.pattern.cur_index > 1)
				mstyle_set_color (state->result, MSTYLE_COLOR_PATTERN,
						  picker_style_color (&state->back.pattern_color));
		}

		/* If there is no pattern don't draw the overlay */
		if (state->back.pattern.cur_index == 0) {
			if (state->back.pattern_item != NULL) {
				gtk_object_destroy (GTK_OBJECT (state->back.pattern_item));
				state->back.pattern_item = NULL;
			}
			return;
		}

		/* Create the pattern if necessary */
		if (state->back.pattern_item == NULL) {
			state->back.pattern_item = GNOME_CANVAS_ITEM (
				gnome_canvas_item_new (
					group,
					gnome_canvas_rect_get_type (),
					"x1", 0.,	"y1", 0.,
					"x2", 90.,	"y2", 50.,
					"width_pixels", (int) 5,
					"fill_color_rgba",
					state->back.pattern_color.rgba,
					NULL));
		} else
			gnome_canvas_item_set (
				GNOME_CANVAS_ITEM (state->back.pattern_item),
				"fill_color_rgba", state->back.pattern_color.rgba,
				NULL);

		gnome_canvas_item_set (
			GNOME_CANVAS_ITEM (state->back.pattern_item),
			"fill_stipple",
			gnumeric_pattern_get_stipple (state->back.pattern.cur_index),
			NULL);
	}
}

static void
cb_back_preview_color (GtkObject *obj, guint r, guint g, guint b, guint a,
		       FormatState *state)
{
	state->back.back_color.r = r;
	state->back.back_color.g = g;
	state->back.back_color.b = b;
	state->back.back_color.rgba =
		GNOME_CANVAS_COLOR_A (r>>8, g>>8, b>>8, 0x00);
	draw_pattern_preview (state);
}

static void
cb_pattern_preview_color (GtkObject *obj, guint r, guint g, guint b, guint a,
			  FormatState *state)
{
	state->back.pattern_color.r = r;
	state->back.pattern_color.g = g;
	state->back.pattern_color.b = b;
	state->back.pattern_color.rgba =
		GNOME_CANVAS_COLOR_A (r>>8, g>>8, b>>8, 0x00);
	draw_pattern_preview (state);
}

static void
cb_custom_back_selected (GtkObject *obj, FormatState *state)
{
	draw_pattern_preview (state);
}

static void
draw_pattern_selected (FormatState *state)
{
	/* If a pattern was selected switch to custom color.
	 * The color is already set to the default, but we need to
	 * differentiate, default and none
	 */
	if (state->back.pattern.cur_index > 0)
		gtk_toggle_button_set_active (state->back.back_color.custom, TRUE);
	draw_pattern_preview (state);
}

/*****************************************************************************/

#define L 10.	/* Left */
#define R 140.	/* Right */
#define T 10.	/* Top */
#define B 90.	/* Bottom */
#define H 50.	/* Horizontal Middle */
#define V 75.	/* Vertical Middle */

static struct
{
	double const			points[4];
	int const			states;
	StyleBorderLocation	const	location;
} const line_info[] =
{
	/*
	state 1 = single cell;
	state 2 = multi vert, single horiz (A1:A2); 
	state 3 = single vert, multi horiz (A1:B1);
	state 4 = multi vertical & multi horizontal
	*/

	/* 1, 2, 3, 4 */
	{ { L, T, R, T }, 0xf, STYLE_BORDER_TOP },
	{ { L, B, R, B }, 0xf, STYLE_BORDER_BOTTOM },
	{ { L, T, L, B }, 0xf, STYLE_BORDER_LEFT },
	{ { R, T, R, B }, 0xf, STYLE_BORDER_RIGHT },

	/* Only for state 2 & 4 */
	{ { L, H, R, H }, 0xa, STYLE_BORDER_HORIZ },

	/* Only for state 3 & 4 */
	{ { V, T, V, B }, 0xc, STYLE_BORDER_VERT },

	/* Only for state 1 & 4 */
	{ { L, T, R, B }, 0x9, STYLE_BORDER_REV_DIAG },
	{ { L, B, R, T }, 0x9, STYLE_BORDER_DIAG},

	/* Only for state 2 */
	{ { L, T, R, H }, 0x2, STYLE_BORDER_REV_DIAG },
	{ { L, H, R, B }, 0x2, STYLE_BORDER_REV_DIAG },
	{ { L, H, R, T }, 0x2, STYLE_BORDER_DIAG },
	{ { L, B, R, H }, 0x2, STYLE_BORDER_DIAG },

	/* Only for state 3 */
	{ { L, T, V, B }, 0x4, STYLE_BORDER_REV_DIAG },
	{ { V, T, R, B }, 0x4, STYLE_BORDER_REV_DIAG },
	{ { L, B, V, T }, 0x4, STYLE_BORDER_DIAG },
	{ { V, B, R, T }, 0x4, STYLE_BORDER_DIAG },

	/* Only for state 4 */
	{ { L, H, V, B }, 0x8, STYLE_BORDER_REV_DIAG },
	{ { V, T, R, H }, 0x8, STYLE_BORDER_REV_DIAG },
	{ { L, H, V, T }, 0x8, STYLE_BORDER_DIAG },
	{ { V, B, R, H }, 0x8, STYLE_BORDER_DIAG },

	{ { 0., 0., 0., 0. }, 0, 0 }
};

static MStyleBorder *
border_get_mstyle (FormatState const *state, StyleBorderLocation const loc)
{
	BorderPicker const * edge = & state->border.edge[loc];
	int const r = (edge->rgba >> 24) & 0xff;
	int const g = (edge->rgba >> 16) & 0xff;
	int const b = (edge->rgba >>  8) & 0xff;
	StyleColor *color =
	    style_color_new ((r << 8)|r, (g << 8)|g, (b << 8)|b);
		
	/* Don't set borders that have not been changed */
	if (!edge->is_set)
		return NULL;

	if (!edge->is_selected)
		return style_border_ref (style_border_none ());

	return style_border_fetch (state->border.edge[loc].pattern_index, color,
				   style_border_get_orientation (loc + MSTYLE_BORDER_TOP));
}

/* See if either the color or pattern for any segment has changed and
 * apply the change to all of the lines that make up the segment.
 */
static gboolean
border_format_has_changed (FormatState *state, BorderPicker *edge)
{
	int i;
	gboolean changed = FALSE;

	edge->is_set = TRUE;
	if (edge->rgba != state->border.color.rgba) {
		edge->rgba = state->border.color.rgba;

		for (i = 0; line_info[i].states != 0 ; ++i ) {
			if (line_info[i].location == edge->index &&
			    state->border.lines[i] != NULL)
				gnome_canvas_item_set (
					GNOME_CANVAS_ITEM (state->border.lines[i]),
					"fill_color_rgba", edge->rgba,
					NULL);
		}
		changed = TRUE;
	}
	if (edge->pattern_index != state->border.pattern.cur_index) {
		edge->pattern_index = state->border.pattern.cur_index;
		for (i = 0; line_info[i].states != 0 ; ++i ) {
			if (line_info[i].location == edge->index &&
			    state->border.lines[i] != NULL) {
				gnumeric_dashed_canvas_line_set_dash_index (
					GNUMERIC_DASHED_CANVAS_LINE (state->border.lines[i]),
					edge->pattern_index);
			}
		}
		changed = TRUE;
	}

	return changed;
}

/*
 * Map canvas x.y coords to a border type
 * Handle all of the various permutations of lines
 */
static gboolean
border_event (GtkWidget *widget, GdkEventButton *event, FormatState *state)
{
	double x = event->x;
	double y = event->y;
	BorderPicker		*edge;

	/* Crap!  This variable is always initialized.
	 * However, the compiler is confused and thinks it is not
	 * so we are forced to pick a random irrelevant value.
	 */
	StyleBorderLocation	 which = STYLE_BORDER_LEFT;

	if (event->button != 1)
		return FALSE;

	/* If we receive a double or triple translate them into single clicks */
	if (event->type == GDK_2BUTTON_PRESS || event->type == GDK_3BUTTON_PRESS)
	{
		GdkEventType type = event->type;
		event->type = GDK_BUTTON_PRESS;
		border_event (widget, event, state);
		if (event->type == GDK_3BUTTON_PRESS)
			border_event (widget, event, state);
		event->type = type;
	}

	/* The edges are always there */
	if (x <= L+5.)		which = STYLE_BORDER_LEFT;
	else if (y <= T+5.)	which = STYLE_BORDER_TOP;
	else if (y >= B-5.)	which = STYLE_BORDER_BOTTOM;
	else if (x >= R-5.)	which = STYLE_BORDER_RIGHT;
	else switch (state->selection_mask) {
	case 1 :
		if ((x < V) == (y < H))
			which = STYLE_BORDER_REV_DIAG;
		else
			which = STYLE_BORDER_DIAG;
		break;
	case 2 :
		if (H-5. < y  && y < H+5.)
			which = STYLE_BORDER_HORIZ;
		else {
			/* Map everything back to the top */
			if (y > H) y -= H-10.;

			if ((x < V) == (y < H/2.))
				which = STYLE_BORDER_REV_DIAG;
			else
				which = STYLE_BORDER_DIAG;
		}
		break;
	case 4 :
		if (V-5. < x  && x < V+5.)
			which = STYLE_BORDER_VERT;
		else {
			/* Map everything back to the left */
			if (x > V) x -= V-10.;

			if ((x < V/2.) == (y < H))
				which = STYLE_BORDER_REV_DIAG;
			else
				which = STYLE_BORDER_DIAG;
		}
		break;
	case 8 :
		if (V-5. < x  && x < V+5.)
			which = STYLE_BORDER_VERT;
		else if (H-5. < y  && y < H+5.)
			which = STYLE_BORDER_HORIZ;
		else {
			/* Map everything back to the 1st quadrant */
			if (x > V) x -= V-10.;
			if (y > H) y -= H-10.;

			if ((x < V/2.) == (y < H/2.))
				which = STYLE_BORDER_REV_DIAG;
			else
				which = STYLE_BORDER_DIAG;
		}
		break;

	default :
		g_assert_not_reached ();
	}

	edge = &state->border.edge[which];
	if (!border_format_has_changed (state, edge) || !edge->is_selected)
		gtk_toggle_button_set_active (edge->button,
					      !edge->is_selected);

	return TRUE;
}

static void
draw_border_preview (FormatState *state)
{
	static double const corners[12][6] = 
	{
	    { T-5., T, L, T, L, T-5. },
	    { R+5., T, R, T, R, T-5 },
	    { T-5., B, L, B, L, B+5. },
	    { R+5., B, R, B, R, B+5. },

	    { V-5., T-1., V, T-1., V, T-5. },
	    { V+5., T-1., V, T-1., V, T-5. },

	    { V-5., B+1., V, B+1., V, B+5. },
	    { V+5., B+1., V, B+1., V, B+5. },

	    { L-1., H-5., L-1., H, L-5., H },
	    { L-1., H+5., L-1., H, L-5., H },

	    { R+1., H-5., R+1., H, R+5., H },
	    { R+1., H+5., R+1., H, R+5., H }
	};
	int i, j;

	/* The first time through lets initialize */
	if (state->border.canvas == NULL) {
		GnomeCanvasGroup  *group;
		GnomeCanvasPoints *points;

		state->border.canvas =
			GNOME_CANVAS (glade_xml_get_widget (state->gui, "border_sample"));
		group = GNOME_CANVAS_GROUP (gnome_canvas_root (state->border.canvas));

		gtk_signal_connect (GTK_OBJECT (state->border.canvas),
				    "button-press-event", GTK_SIGNAL_FUNC (border_event),
				    state);

		state->border.back = GNOME_CANVAS_ITEM (
			gnome_canvas_item_new ( group,
						gnome_canvas_rect_get_type (),
						"x1", L-10.,	"y1", T-10.,
						"x2", R+10.,	"y2", B+10.,
						"width_pixels", (int) 0,
						"fill_color",	"white",
						NULL));

		/* Draw the corners */
		points = gnome_canvas_points_new (3);

		for (i = 0; i < 12 ; ++i) {
			if (i >= 8) {
				if (!(state->selection_mask & 0xa))
					continue;
			} else if (i >= 4) {
				if (!(state->selection_mask & 0xc))
					continue;
			}

			for (j = 6 ; --j >= 0 ;)
				points->coords [j] = corners[i][j];

			gnome_canvas_item_new (group,
					       gnome_canvas_line_get_type (),
					       "width_pixels",	(int) 0,
					       "fill_color",	"gray63",
					       "points",	points,
					       NULL);
		}
		gnome_canvas_points_free (points);

		points = gnome_canvas_points_new (2);
		for (i = 0; line_info[i].states != 0 ; ++i ) {
			for (j = 4; --j >= 0 ; )
				points->coords [j] = line_info[i].points[j];

			if (line_info[i].states & state->selection_mask) {
				BorderPicker const * p =
				    & state->border.edge[line_info[i].location];
				state->border.lines[i] =
					gnome_canvas_item_new (group,
							       gnumeric_dashed_canvas_line_get_type (),
							       "fill_color_rgba", p->rgba,
							       "points",	  points,
							       NULL);
				gnumeric_dashed_canvas_line_set_dash_index (
					GNUMERIC_DASHED_CANVAS_LINE (state->border.lines[i]),
					p->pattern_index);
			} else
				state->border.lines[i] = NULL;
		    }
		gnome_canvas_points_free (points);
	}

	for (i = 0; i < STYLE_BORDER_EDGE_MAX; ++i) {
		BorderPicker *border = &state->border.edge[i];
		void (*func)(GnomeCanvasItem *item) = border->is_selected
			? &gnome_canvas_item_show : &gnome_canvas_item_hide;

		for (j = 0; line_info[j].states != 0 ; ++j) {
			if (line_info[j].location == i &&
			    state->border.lines[j] != NULL)
				(*func) (state->border.lines[j]);
		}
	}

	fmt_dialog_changed (state);
}

static void
cb_border_preset_clicked (GtkButton *btn, FormatState *state)
{
	gboolean target_state;
	StyleBorderLocation i, last;

	if (state->border.preset[BORDER_PRESET_NONE] == btn) {
		i = STYLE_BORDER_TOP;
		last = STYLE_BORDER_VERT;
		target_state = FALSE;
	} else if (state->border.preset[BORDER_PRESET_OUTLINE] == btn) {
		i = STYLE_BORDER_TOP;
		last = STYLE_BORDER_RIGHT;
		target_state = TRUE;
	} else if (state->border.preset[BORDER_PRESET_INSIDE] == btn) {
		i = STYLE_BORDER_HORIZ;
		last = STYLE_BORDER_VERT;
		target_state = TRUE;
	} else {
		g_warning ("Unknown border preset button");
		return;
	}

	/* If we are turning things on, TOGGLE the states to
	 * capture the current pattern and color */
	for (; i <= last; ++i) {
		gtk_toggle_button_set_active (
			state->border.edge[i].button,
			FALSE);

		if (target_state)
			gtk_toggle_button_set_active (
				state->border.edge[i].button,
				TRUE);
		else if (gtk_toggle_button_get_active (
				state->border.edge[i].button))
			/* Turn off damn it !
			 * we really want things off not just to pick up
			 * the new colours.
			 */
			gtk_toggle_button_set_active (
				state->border.edge[i].button,
				FALSE);
	}
}

/*
 * Callback routine to update the border preview when a button is clicked
 */
static void
cb_border_toggle (GtkToggleButton *button, BorderPicker *picker)
{
	picker->is_selected = gtk_toggle_button_get_active (button);

	/* If the format has changed and we were just toggled off,
	 * turn ourselves back on.
	 */
	if (border_format_has_changed (picker->state, picker) &&
	    !picker->is_selected)
		gtk_toggle_button_set_active (button, TRUE);
	else 
		/* Update the preview lines and enable/disable them */
		draw_border_preview (picker->state);
}

static void
cb_border_color (GtkObject *obj, guint r, guint g, guint b, guint a,
		 FormatState *state)
{
	state->border.color.rgba = GNOME_CANVAS_COLOR_A (r>>8, g>>8, b>>8, 0x00);
}

#undef L
#undef R
#undef T
#undef B
#undef H
#undef V

typedef struct
{
	StyleBorderLocation     t;
	MStyleBorder const *res;
} check_border_closure_t;

/*
 * Initialize the fields of a BorderPicker, connect signals and
 * hide if needed.
 */
static void
init_border_button (FormatState *state, StyleBorderLocation const i,
		    GtkWidget *button,
		    MStyleBorder const * const border)
{
	if (border == NULL) {
		state->border.edge[i].rgba = 0;
		state->border.edge[i].pattern_index = STYLE_BORDER_INCONSISTENT;
		state->border.edge[i].is_selected = TRUE;
	} else {
		StyleColor const * c = border->color;
		state->border.edge[i].rgba =
		    GNOME_CANVAS_COLOR_A (c->red>>8, c->green>>8, c->blue>>8, 0x00);
		state->border.edge[i].pattern_index = border->line_type;
		state->border.edge[i].is_selected = (border->line_type != STYLE_BORDER_NONE);
	}

	state->border.edge[i].state = state;
	state->border.edge[i].index = i;
	state->border.edge[i].button = GTK_TOGGLE_BUTTON (button);
	state->border.edge[i].is_set = FALSE;

	g_return_if_fail (button != NULL);

	gtk_toggle_button_set_active (state->border.edge[i].button,
				      state->border.edge[i].is_selected);

	gtk_signal_connect (GTK_OBJECT (button), "toggled",
			    GTK_SIGNAL_FUNC (cb_border_toggle),
			    &state->border.edge[i]);

	if ((i == STYLE_BORDER_HORIZ && !(state->selection_mask & 0xa)) ||
	    (i == STYLE_BORDER_VERT  && !(state->selection_mask & 0xc)))
		gtk_widget_hide (button);
}

/*****************************************************************************/

/* Handler for the apply button */
static void
cb_fmt_dialog_dialog_apply (GtkObject *w, int page, FormatState *state)
{
	MStyleBorder *borders[STYLE_BORDER_EDGE_MAX];
	int i;

	if (page != -1)
		return;

	cell_freeze_redraws ();
	
	mstyle_ref (state->result);

	for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
		borders [i] = border_get_mstyle (state, i);

	cmd_format (workbook_command_context_gui (state->sheet->workbook),
		    state->sheet, state->result, borders);

	if (mstyle_is_element_set (state->result, MSTYLE_FONT_SIZE))        
		sheet_selection_height_update (state->sheet);     

	cell_thaw_redraws ();
	mstyle_unref (state->result);

	/* Get a fresh style to accumulate results in */
	state->result = mstyle_new ();
}

/* Handler for destroy */
static gboolean
cb_fmt_dialog_dialog_destroy (GtkObject *w, FormatState *state)
{
	g_free ((char *)state->format.spec);
	mstyle_unref (state->style);
	mstyle_unref (state->result);
	gtk_object_unref (GTK_OBJECT (state->gui));
	g_free (state);
	return FALSE;
}

/* Set initial focus */
static void set_initial_focus (FormatState *state)
{
	GtkWidget *focus_widget = NULL, *pagew;
	gchar *name;
	
	pagew = gtk_notebook_get_nth_page 
		(GTK_NOTEBOOK (state->dialog->notebook), fmt_dialog_page);
	name = gtk_widget_get_name (pagew);

	if (strcmp (name, "number_box") == 0) {
		focus_widget
			= glade_xml_get_widget (state->gui, "format_general");
	} else if (strcmp (name, "alignment_box") == 0) {
		focus_widget
			= glade_xml_get_widget (state->gui, "halign_left");
	} else if (strcmp (name, "font_box") == 0) {
		focus_widget 
			= GTK_WIDGET (state->font.selector->font_size_entry);
	} else if (strcmp (name, "border_box") == 0) {
		focus_widget
			= glade_xml_get_widget (state->gui, "outline_border");
	} else if (strcmp (name, "background_box") == 0) {
		focus_widget
			= glade_xml_get_widget (state->gui, "back_color_auto");
	} else if (strcmp (name, "protection_box") == 0) {
		focus_widget = glade_xml_get_widget (state->gui, 
						     "protected_button");
	} else {
		focus_widget = NULL;
	}

	if (focus_widget 
	    && GTK_WIDGET_CAN_FOCUS (focus_widget) 
	    && GTK_WIDGET_IS_SENSITIVE (focus_widget))
		gtk_widget_grab_focus (focus_widget);
}

static void
fmt_dialog_impl (FormatState *state, MStyleBorder **borders)
{
	static GnomeHelpMenuEntry help_ref = { "gnumeric", "formatting.html" };
	static struct
	{
		char const * const name;
		StyleBorderType const pattern;
	} const line_pattern_buttons[] = {
	    { "line_pattern_thin", STYLE_BORDER_THIN },

	    { "line_pattern_none", STYLE_BORDER_NONE },
	    { "line_pattern_medium_dash_dot_dot", STYLE_BORDER_MEDIUM_DASH_DOT_DOT },

	    { "line_pattern_hair", STYLE_BORDER_HAIR },
	    { "line_pattern_slant", STYLE_BORDER_SLANTED_DASH_DOT },

	    { "line_pattern_dotted", STYLE_BORDER_DOTTED },
	    { "line_pattern_medium_dash_dot", STYLE_BORDER_MEDIUM_DASH_DOT },

	    { "line_pattern_dash_dot_dot", STYLE_BORDER_DASH_DOT_DOT },
	    { "line_pattern_medium_dash", STYLE_BORDER_MEDIUM_DASH },

	    { "line_pattern_dash_dot", STYLE_BORDER_DASH_DOT },
	    { "line_pattern_medium", STYLE_BORDER_MEDIUM },

	    { "line_pattern_dashed", STYLE_BORDER_DASHED },
	    { "line_pattern_thick", STYLE_BORDER_THICK },

	    /* Thin will display here, but we need to put it first to make it
	     * the default */
	    { "line_pattern_double", STYLE_BORDER_DOUBLE },

	    { NULL },
	};
	static char const * const pattern_buttons[] = {
	    "gp_solid", "gp_75grey", "gp_50grey",
	    "gp_25grey", "gp_125grey", "gp_625grey",
	    "gp_horiz",
	    "gp_vert",
	    "gp_diag",
	    "gp_rev_diag",
	    "gp_diag_cross",
	    "gp_thick_diag_cross",
	    "gp_thin_horiz",
	    "gp_thin_vert",
	    "gp_thin_rev_diag",
	    "gp_thin_diag",
	    "gp_thin_horiz_cross",
	    "gp_thin_diag_cross",
	    NULL
	};

	/* The order corresponds to the BorderLocation enum */
	static char const * const border_buttons[] = {
	    "top_border",	"bottom_border",
	    "left_border",	"right_border",
	    "rev_diag_border",	"diag_border",
	    "inside_horiz_border", "inside_vert_border",
	    NULL
	};

	/* The order corresponds to BorderPresets */
	static char const * const border_preset_buttons[] = {
	    "no_border", "outline_border", "inside_border",
	    NULL
	};

	int i, selected;
	char const *name;
	gboolean has_back;

	GtkWidget *dialog = glade_xml_get_widget (state->gui, "CellFormat");
	g_return_if_fail (dialog != NULL);

	gtk_window_set_title (GTK_WINDOW (dialog), _("Format Cells"));

	/* Initialize */
	state->dialog			= GNOME_PROPERTY_BOX (dialog);

	state->enable_edit		= FALSE;  /* Enable below */

	state->border.canvas	= NULL;
	state->border.pattern.cur_index	= 0;

	state->back.canvas	= NULL;
	state->back.back		= NULL;
	state->back.pattern_item	= NULL;
	state->back.pattern.cur_index	= 0;

	/* Select the same page the last invocation used */
	gtk_notebook_set_page (
		GTK_NOTEBOOK (GNOME_PROPERTY_BOX (dialog)->notebook),
		fmt_dialog_page);
	state->page_signal = gtk_signal_connect (
		GTK_OBJECT (GNOME_PROPERTY_BOX (dialog)->notebook),
		"switch_page", GTK_SIGNAL_FUNC (cb_page_select),
		NULL);
	gtk_signal_connect (
		GTK_OBJECT (GNOME_PROPERTY_BOX (dialog)->notebook),
		"destroy", GTK_SIGNAL_FUNC (cb_notebook_destroy),
		state);

	fmt_dialog_init_format_page (state);
	fmt_dialog_init_align_page (state);
	fmt_dialog_init_font_page (state);

	/* Setup border line pattern buttons & select the 1st button */
	state->border.pattern.draw_preview = NULL;
	state->border.pattern.current_pattern = NULL;
	state->border.pattern.state = state;
	for (i = 0; (name = line_pattern_buttons[i].name) != NULL; ++i)
		setup_pattern_button (state->gui, name, &state->border.pattern,
				      i != 1, /* No image for None */
				      line_pattern_buttons[i].pattern,
				      FALSE); /* don't select */

	/* Set the default line pattern to THIN (the 1st element of line_pattern_buttons).
	 * This can not come from the style.  It is a UI element not a display item */
	gtk_toggle_button_set_active (state->border.pattern.default_button, TRUE);

#define COLOR_SUPPORT(v, n, style_element, auto_color, func) \
	setup_color_pickers (state->gui, #n "_picker", #n "_custom", #n "_auto",\
			     &state->v, state, auto_color, GTK_SIGNAL_FUNC (func),\
			     style_element, state->style)

	COLOR_SUPPORT (font.color, font_color, MSTYLE_COLOR_FORE,
		       &gs_black, cb_font_preview_color);

	/* FIXME : If all the border colors are the same return that color */
	COLOR_SUPPORT (border.color, border_color, MSTYLE_ELEMENT_UNSET,
		       &gs_black, cb_border_color);

	COLOR_SUPPORT (back.back_color, back_color, MSTYLE_COLOR_BACK,
		       &gs_white, cb_back_preview_color);
	COLOR_SUPPORT (back.pattern_color, pattern_color, MSTYLE_COLOR_PATTERN,
		       &gs_black, cb_pattern_preview_color);

	/* The background color selector is special.  There is a difference
	 * between auto (None) and the default custom which is white.
	 */
	gtk_signal_connect (GTK_OBJECT (state->back.back_color.custom), "clicked",
			    GTK_SIGNAL_FUNC (cb_custom_back_selected),
			    state);

	/* Setup the border images */
	for (i = 0; (name = border_buttons[i]) != NULL; ++i) {
		GtkWidget * tmp = init_button_image (state->gui, name);
		if (tmp != NULL) {
			init_border_button (state, i, tmp,
					    borders [i]);
			style_border_unref (borders [i]);
		}
	}

	/* Get the current background
	 * A pattern of 0 is has no background.
	 * A pattern of 1 is a solid background
	 * All others have 2 colours and a stipple
	 */
	has_back = FALSE;
	selected = 1;
	if (!mstyle_is_element_conflict (state->style, MSTYLE_PATTERN)) {
		selected = mstyle_get_pattern (state->style);
		has_back = (selected != 0);
	}

	/* Setup pattern buttons & select the current pattern (or the 1st
	 * if none is selected)
	 * NOTE : This must be done AFTER the colour has been setup to
	 * avoid having it erased by initialization.
	 */
	state->back.pattern.draw_preview = &draw_pattern_selected;
	state->back.pattern.current_pattern = NULL;
	state->back.pattern.state = state;
	for (i = 0; (name = pattern_buttons[i]) != NULL; ++i)
		setup_pattern_button (state->gui, name,
				      &state->back.pattern, TRUE,
				      i+1, /* Pattern #s start at 1 */
				      i+1 == selected);

	/* If the pattern is 0 indicating no background colour
	 * Set background to No colour.  This will set states correctly.
	 */
	if (!has_back)
		gtk_toggle_button_set_active (state->back.back_color.autob,
					      TRUE);

	/* Setup the images in the border presets */
	for (i = 0; (name = border_preset_buttons[i]) != NULL; ++i) {
		GtkWidget * tmp = init_button_image (state->gui, name);
		if (tmp != NULL) {
			state->border.preset[i] = GTK_BUTTON (tmp);
			gtk_signal_connect (GTK_OBJECT (tmp), "clicked",
					    GTK_SIGNAL_FUNC (cb_border_preset_clicked),
					    state);
			if (i == BORDER_PRESET_INSIDE && state->selection_mask != 0x8) 
				gtk_widget_hide (tmp);
		}
	}

	/* Draw the border preview */
	draw_border_preview (state);

	/* Setup help */
	gtk_signal_connect (GTK_OBJECT (dialog), "help",
			    GTK_SIGNAL_FUNC (gnome_help_pbox_goto), &help_ref);

	/* Handle apply */
	gtk_signal_connect (GTK_OBJECT (dialog), "apply",
			    GTK_SIGNAL_FUNC (cb_fmt_dialog_dialog_apply), state);

	/* Handle destroy */
	gtk_signal_connect(GTK_OBJECT(dialog), "destroy",
			   GTK_SIGNAL_FUNC(cb_fmt_dialog_dialog_destroy),
			   state);

	/* Set initial focus */
	set_initial_focus (state);

	/* Ok, edit events from now on are real */
	state->enable_edit = TRUE;

	/* We could now make it modeless, and arguably should do so. We must
	 * then track the selection: styles should be applied to the current
	 * selection.
	 * There are some UI issues to discuss before we do this, though. Most
	 * important: 
	 * - will users be confused?
	 * And on a different level:
	 * - should the preselected style in the dialog change when another
	 *   cell is selected? May be, but then we can't first make a style,
	 *   then move around and apply it to different cells.
	 */
	
	/* Make it modal */
	gtk_window_set_modal (GTK_WINDOW(dialog), TRUE);

	
	/* Bring up the dialog */
	gnumeric_dialog_show (state->sheet->workbook->toplevel,
			      GNOME_DIALOG (dialog), FALSE, TRUE);
}

static gboolean
fmt_dialog_selection_type (Sheet *sheet,
			   Range const *range,
			   gpointer user_data)
{
	if (range->start.row != range->end.row)
		*(int *)user_data |= 1;
	if (range->start.col != range->end.col)
		*(int *)user_data |= 2;

	return TRUE;
}

void
dialog_cell_format (Workbook *wb, Sheet *sheet)
{
	GladeXML     *gui;
	MStyle       *mstyle;
	Value	     *sample_val;
	Cell	     *first_upper_left;
	Range const  *selection;
	MStyleBorder *borders[STYLE_BORDER_EDGE_MAX];
	FormatState  *state = g_new (FormatState, 1);

	g_return_if_fail (wb != NULL);
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/" GLADE_FILE , NULL);
	if (!gui) {
		g_warning ("Could not find " GLADE_FILE "\n");
		return;
	}

	selection = selection_first_range (sheet, TRUE);
	first_upper_left = sheet_cell_get (sheet, selection->start.col,
					   selection->start.row);

	sample_val = (first_upper_left) ? first_upper_left->value : NULL;
	mstyle = sheet_selection_get_unique_style (sheet, borders);

	/* Initialize */
	state->gui		= gui;
	state->sheet		= sheet;
	state->value		= sample_val;
	state->style		= mstyle;
	state->result		= mstyle_new ();
	state->selection_mask	= 0;

	(void) selection_foreach_range (sheet,
					&fmt_dialog_selection_type,
					&state->selection_mask);
	state->selection_mask	= 1 << state->selection_mask;

	fmt_dialog_impl (state, borders);
}

/*
 * TODO 
 *
 * Borders
 * 	- Double lines for borders
 * 	- Add the 'text' elements in the preview
 *
 * Wishlist
 * 	- Some undo capabilities in the dialog.
 * 	- How to distinguish between auto & custom colors on extraction from styles.
 */
