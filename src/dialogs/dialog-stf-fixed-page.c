/*
 * dialog-stf-fixed-page.c : Controls the widgets on the fixed page of the dialog (fixed-width page that is)
 *
 * Copyright 2001 Almer S. Tigelaar <almer@gnome.org>
 * Copyright 2003 Morten Welinder <terra@gnome.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#undef GTK_DISABLE_DEPRECATED
#warning "This file uses GTK_DISABLE_DEPRECATED"
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialog-stf.h"
#include <gui-util.h>

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

/**
 * fixed_page_autodiscover:
 * @pagedata: a mother struct
 *
 * Use the STF's autodiscovery function and put the
 * result in the fixed_collist
 **/
static void
fixed_page_autodiscover (DruidPageData_t *pagedata)
{
	FixedInfo_t *info = pagedata->fixed_info;
	guint i = 1;
	char *tset[2];

	stf_parse_options_fixed_autodiscover (info->fixed_run_parseoptions, pagedata->importlines, (char *) pagedata->cur);

	gtk_clist_clear (info->fixed_collist);

	while (i < info->fixed_run_parseoptions->splitpositions->len) {

		tset[0] = g_strdup_printf ("%d", i - 1);
		tset[1] = g_strdup_printf ("%d", g_array_index (info->fixed_run_parseoptions->splitpositions,
								int,
								i));
		gtk_clist_append (info->fixed_collist, tset);

		g_free (tset[0]);
		g_free (tset[1]);

		i++;
	}

	tset[0] = g_strdup_printf ("%d", i - 1);
	tset[1] = g_strdup_printf ("%d", -1);

	gtk_clist_append (info->fixed_collist, tset);

	g_free (tset[0]);
	g_free (tset[1]);

	/*
	 * If there are no splitpositions than apparantly
	 * no columns where found
	 */

	if (info->fixed_run_parseoptions->splitpositions->len < 1) {
		GtkWidget *dialog = gtk_message_dialog_new (NULL,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO,
			GTK_BUTTONS_OK,
			_("Autodiscovery did not find any columns in the text. Try manually"));
		gnumeric_dialog_run (pagedata->wbcg, GTK_DIALOG (dialog));
	}
}


static void fixed_page_update_preview (DruidPageData_t *pagedata);

static gint
cb_col_event (GtkWidget *button,
	      GdkEvent  *event,
	      gpointer   _col)
{
	int col = GPOINTER_TO_INT (_col);

	if (event->type == GDK_BUTTON_PRESS) {
		DruidPageData_t *data = g_object_get_data (G_OBJECT (button), "fixed-data");
		FixedInfo_t *info = data->fixed_info;
		GdkEventButton *bevent = (GdkEventButton *)event;

		if (bevent->button == 1) {
			/* Split column.  */
			GtkCellRenderer *cell =	stf_preview_get_cell_renderer
				(info->fixed_run_renderdata, col);
			char *row[2];
			int i, charindex, colstart, colend;
			PangoLayout *layout;
			PangoFontDescription *font_desc;
			int dx, width;

			if (col == 0)
				colstart = 0;
			else {
				gtk_clist_get_text (info->fixed_collist, col - 1, 1, row);
				colstart = atoi (row[0]);
			}

			gtk_clist_get_text (info->fixed_collist, col, 1, row);
			colend = atoi (row[0]);

			dx = (int)bevent->x - (GTK_BIN (button)->child->allocation.x - button->allocation.x);

			layout = gtk_widget_create_pango_layout (button, "x");

			g_object_get (G_OBJECT (cell), "font_desc", &font_desc, NULL);
			pango_layout_set_font_description (layout, font_desc);
			pango_layout_get_pixel_size (layout, &width, NULL);
			if (width < 1) width = 1;
			charindex = colstart + (dx + width / 2) / width;
			g_object_unref (layout);
			pango_font_description_free (font_desc);

#if 0
			g_print ("Start at %d; end at %d; charindex=%d\n", colstart, colend, charindex);
#endif

			if (charindex <= colstart || (colend != -1 && charindex >= colend))
				return TRUE;

			row[0] = g_strdup_printf ("%d", col + 1);
			row[1] = g_strdup_printf ("%d", charindex);
			gtk_clist_insert (info->fixed_collist, col, row);
			g_free (row[0]);
			g_free (row[1]);

			/* Patch the following column numbers in the list.  */
			for (i = col; i < GTK_CLIST (info->fixed_collist)->rows; i++) {
				char *text = g_strdup_printf ("%d", i);
				gtk_clist_set_text (info->fixed_collist, i, 0, text);
				g_free (text);
			}

			fixed_page_update_preview (data);
			return TRUE;
		}

		if (bevent->button == 3) {
			/* Remove column.  */
			gtk_clist_select_row (info->fixed_collist, col, 0);
			gnumeric_clist_moveto (info->fixed_collist, col);

			gtk_signal_emit_by_name (GTK_OBJECT (info->fixed_remove),
						 "clicked",
						 data);
			return TRUE;
		}
	}
	    
	return FALSE;
}


/**
 * fixed_page_update_preview
 * @pagedata : mother struct
 *
 * Will simply update the preview
 *
 * returns : nothing
 **/
static void
fixed_page_update_preview (DruidPageData_t *pagedata)
{
	FixedInfo_t *info = pagedata->fixed_info;
	StfParseOptions_t *parseoptions = pagedata->fixed_info->fixed_run_parseoptions;
	RenderData_t *renderdata = info->fixed_run_renderdata;
	GPtrArray *lines;
	char *t[2];
	int i, temp;

	stf_parse_options_fixed_splitpositions_clear (parseoptions);
	for (i = 0; i < GTK_CLIST (info->fixed_collist)->rows; i++) {
		gtk_clist_get_text (info->fixed_collist, i, 1, t);
		temp = atoi (t[0]);
		stf_parse_options_fixed_splitpositions_add (parseoptions, temp);
	}

	lines = stf_parse_general (parseoptions, pagedata->cur);

	stf_preview_render (renderdata, lines);

	for (i = 0; i < renderdata->colcount; i++) {
		GtkTreeViewColumn *column =
			stf_preview_get_column (renderdata, i);
		GtkCellRenderer *cell =
			stf_preview_get_cell_renderer (renderdata, i);

		g_object_set (G_OBJECT (cell),
			      "family", "monospace",
			      NULL);

		g_object_set_data (G_OBJECT (column->button), "fixed-data", pagedata);
		g_object_set (G_OBJECT (column), "clickable", TRUE, NULL);
		g_signal_connect (column->button, "event",
				  G_CALLBACK (cb_col_event),
				  GINT_TO_POINTER (i));
	}
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

#if 0
/**
 * fixed_page_canvas_motion_notify_event
 * @canvas : The gnome canvas that emitted the signal
 * @event : a gdk motion event struct
 * @data : a mother struct
 *
 * This event handles the changing of the mouse cursor and
 * re-sizing of columns
 *
 * returns : always TRUE
 **/
static gboolean
fixed_page_canvas_motion_notify_event (GnomeCanvas *canvas, GdkEventMotion *event, DruidPageData_t *data)
{
	GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (canvas));
	FixedInfo_t *info = data->fixed_info;
	GdkCursor *cursor;
	double worldx, worldy;
	int column;

	gnome_canvas_window_to_world (canvas, event->x, event->y, &worldx, &worldy);

	column = stf_preview_get_column_border_at_x (info->fixed_run_renderdata, worldx);

	if (column != -1 || info->fixed_run_mousedown) {

		cursor = gdk_cursor_new_for_display (display, GDK_SB_H_DOUBLE_ARROW);
		gdk_window_set_cursor (canvas->layout.bin_window, cursor);
		gdk_cursor_unref (cursor);

		/* This is were the actual resizing is done, now we simply wait till
		 * the user moves the mouse "width in pixels of a char" pixels
		 * after that we reset the x_position and adjust the column end and
		 * wait till the same thing happens again
		 */
		if (info->fixed_run_mousedown) {
			char *t[1];
			double diff;
			int min, max;
			int colend, chars;

			diff = worldx - info->fixed_run_xorigin;
			chars = diff / info->fixed_run_renderdata->charwidth;

			if (chars != 0) {

				info->fixed_run_xorigin = worldx;

				gtk_clist_get_text (info->fixed_collist, info->fixed_run_column, 1, t);

				colend = atoi (t[0]);

				if (info->fixed_run_column > 0) {

					gtk_clist_get_text (info->fixed_collist, info->fixed_run_column - 1, 1, t);
					min = atoi (t[0]) + 1;
				} else
					min = 1;

				if (info->fixed_run_column < GTK_CLIST (info->fixed_collist)->rows - 2) {

					gtk_clist_get_text (info->fixed_collist, info->fixed_run_column + 1, 1, t);
					max = atoi (t[0]) - 1;
				} else {
					GtkAdjustment *spinadjust = gtk_spin_button_get_adjustment (info->fixed_colend);

					max = spinadjust->upper;
				}

				colend += chars;

				if (colend < min)
					colend = min;

				if (colend > max)
					colend = max;

				gtk_clist_select_row (info->fixed_collist, info->fixed_run_column, 0);
				gnumeric_clist_moveto (info->fixed_collist, info->fixed_run_column);

				gtk_spin_button_set_value (info->fixed_colend, colend);

				fixed_page_update_preview (data);
			}
		}
	} else {

		cursor = gdk_cursor_new_for_display (display, GDK_HAND2);
		gdk_window_set_cursor (canvas->layout.bin_window, cursor);
		gdk_cursor_unref (cursor);
	}

	return TRUE;
}

/**
 * fixed_page_canvas_motion_notify_event
 * @canvas : The gnome canvas that emitted the signal
 * @event : a gdk event button struct
 * @data : a mother struct
 *
 * This handles single clicking/drag start events :
 * 1) start of re-sizing of existing columns
 * 2) removal of existing columns
 *
 * returns : always TRUE
 **/
static gboolean
fixed_page_canvas_button_press_event (GnomeCanvas *canvas, GdkEventButton *event, DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	double worldx, worldy;
	int column;

	gnome_canvas_window_to_world (canvas, event->x, event->y, &worldx, &worldy);

	column = stf_preview_get_column_border_at_x (info->fixed_run_renderdata, worldx);

	switch (event->button) {
	case 1 : { /* Left button -> Resize */

		if (column != -1) {

			info->fixed_run_xorigin   = worldx;
			info->fixed_run_column    = column;
			info->fixed_run_mousedown = TRUE;
		}
	} break;
	case 3 : { /* Right button -> Remove */

		if (column != -1) {

			gtk_clist_select_row (info->fixed_collist, column, 0);
			gnumeric_clist_moveto (info->fixed_collist, column);

			gtk_signal_emit_by_name (GTK_OBJECT (info->fixed_remove),
						 "clicked",
						 data);
		}
	} break;
	}

	return TRUE;
}

/**
 * fixed_page_canvas_motion_notify_event
 * @canvas : The gnome canvas that emitted the signal
 * @event : a gdk event button struct
 * @data : a mother struct
 *
 * This signal handler actually handles creating new columns
 *
 * returns : always TRUE
 **/
static gboolean
fixed_page_canvas_button_release_event (GnomeCanvas *canvas, GdkEventButton *event, DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	double worldx, worldy;

	gnome_canvas_window_to_world (canvas, event->x, event->y, &worldx, &worldy);

	/* User must not drag an have clicked the LEFT mousebutton */
	if (!info->fixed_run_mousedown && event->button == 1) {
		int charindex = stf_preview_get_char_at_x (info->fixed_run_renderdata, worldx);

		/* If the user clicked on a sane place, create a new column */
		if (charindex != -1) {
			int colindex = stf_preview_get_column_at_x (info->fixed_run_renderdata, worldx);
			int i;
			char *row[2];

			if (colindex > 0) {
				char *coltext[1];

				gtk_clist_get_text (info->fixed_collist, colindex - 1, 1, coltext);
				if (atoi (coltext[0]) == charindex) /* Don't create a new column with the same splitposition */
					return TRUE;
			} else if (charindex == 0) /* don't create a leading empty column */
				return TRUE;

			row[0] = g_strdup_printf ("%d", colindex + 1);
			row[1] = g_strdup_printf ("%d", charindex);
			gtk_clist_insert (info->fixed_collist, colindex, row);
			g_free (row[0]);
			g_free (row[1]);

			for (i = colindex; i < GTK_CLIST (info->fixed_collist)->rows; i++) {
				char *text = g_strdup_printf ("%d", i);
				gtk_clist_set_text (info->fixed_collist, i, 0, text);
				g_free (text);
			}

			fixed_page_update_preview (data);
		}
	} else
		info->fixed_run_mousedown = FALSE;

	return TRUE;
}
#endif

/**
 * fixed_page_collist_select_row
 * @clist : the GtkClist that emitted the signal
 * @row : row the user clicked on
 * @column : column the user clicked on
 * @event : information on the buttons that were pressed
 * @data : mother struct
 *
 * This will update the widgets on the right side of the dialog to
 * reflect the new column selection
 *
 * returns : nothing
 **/
static void
fixed_page_collist_select_row (GtkCList *clist, int row,
			       G_GNUC_UNUSED int column,
			       G_GNUC_UNUSED GdkEventButton *event,
			       DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	char *t[2];

	if (info->fixed_run_manual) {
		info->fixed_run_manual = FALSE;
		return;
	}

	info->fixed_run_index = row;

	gtk_clist_get_text (clist, row, 1, t);
	gtk_spin_button_set_value (info->fixed_colend, atoi (t[0]));

	gtk_widget_set_sensitive (GTK_WIDGET (info->fixed_colend), !(row == clist->rows - 1));
}

/**
 * fixed_page_colend_changed
 * @button : the gtkspinbutton that emitted the signal
 * @data : a mother struct
 *
 * if the user changes the end of the current column the preview will be redrawn
 * and the @data->fixed_info->fixed_collist will be updated
 *
 * returns : nothing
 **/
static void
fixed_page_colend_changed (GtkSpinButton *button, DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	char *text;

	if (info->fixed_run_index < 0 || (info->fixed_run_index == GTK_CLIST (info->fixed_collist)->rows - 1))
		return;

	text = gtk_editable_get_chars (GTK_EDITABLE (button), 0, -1);
	gtk_clist_set_text (info->fixed_collist, info->fixed_run_index, 1, text);
	g_free (text);

	fixed_page_update_preview (data);
}

/**
 * fixed_page_add_clicked
 * @button : the GtkButton that emitted the signal
 * @data : the mother struct
 *
 * This will add a new column to the @data->fixed_info->fixed_collist
 *
 * returns : nothing
 **/
static void
fixed_page_add_clicked (G_GNUC_UNUSED GtkButton *button,
			DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	char *tget[1], *tset[2];
	int colindex = GTK_CLIST (info->fixed_collist)->rows;

	if (colindex > 1) {

		gtk_clist_get_text (info->fixed_collist, colindex - 2, 1, tget);
		tget[0] = g_strdup_printf ("%d", atoi (tget[0]) + 1);
		gtk_clist_set_text (info->fixed_collist, colindex - 1, 1, tget[0]);
		g_free (tget[0]);
	}
	else {

		tget[0] = g_strdup ("1");
		gtk_clist_set_text (info->fixed_collist, colindex - 1, 1, tget[0]);
		g_free (tget[0]);
	}

	tset[0] = g_strdup_printf ("%d", colindex);
	tset[1] = g_strdup_printf ("%d", -1);
	gtk_clist_append (info->fixed_collist, tset);
	g_free (tset[0]);
	g_free (tset[1]);

	gtk_clist_select_row (info->fixed_collist, GTK_CLIST (info->fixed_collist)->rows - 2, 0);
	gnumeric_clist_moveto (info->fixed_collist, GTK_CLIST (info->fixed_collist)->rows - 2);

	fixed_page_update_preview (data);
}

/**
 * fixed_page_remove_clicked
 * @button : the GtkButton that emitted the signal
 * @data : the mother struct
 *
 * This will remove the selected item from the @data->fixed_info->fixed_collist
 *
 * returns : nothing
 **/
static void
fixed_page_remove_clicked (G_GNUC_UNUSED GtkButton *button,
			   DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	int i;

	if (info->fixed_run_index < 0 || (info->fixed_run_index == GTK_CLIST (info->fixed_collist)->rows - 1))
		info->fixed_run_index--;

	gtk_clist_remove (info->fixed_collist, info->fixed_run_index);

	for (i = info->fixed_run_index; i < GTK_CLIST (info->fixed_collist)->rows; i++) {
		char *text = g_strdup_printf ("%d", i);

		gtk_clist_set_text (info->fixed_collist, i, 0, text);
		g_free (text);
	}

	gtk_clist_select_row (info->fixed_collist, info->fixed_run_index, 0);
	gnumeric_clist_moveto (info->fixed_collist, info->fixed_run_index);

	fixed_page_update_preview (data);
}

/**
 * fixed_page_clear_clicked:
 * @button: GtkButton
 * @data: mother struct
 *
 * Will clear all entries in fixed_collist
 **/
static void
fixed_page_clear_clicked (G_GNUC_UNUSED GtkButton *button,
			  DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	char *tset[2];

	gtk_clist_clear (info->fixed_collist);

	tset[0] = g_strdup ("0");
	tset[1] = g_strdup ("-1");

	gtk_clist_append (info->fixed_collist, tset);

	g_free (tset[0]);
	g_free (tset[1]);

	fixed_page_update_preview (data);
}

/**
 * fixed_page_auto_clicked:
 * @button: GtkButton
 * @data: mother struct
 *
 * Will try to automatically recognize columns in the
 * text.
 **/
static void
fixed_page_auto_clicked (G_GNUC_UNUSED GtkButton *button,
			 DruidPageData_t *data)
{
	fixed_page_autodiscover (data);

	fixed_page_update_preview (data);
}

/*************************************************************************************************
 * FIXED EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * stf_dialog_fixed_page_prepare
 * @page : The druidpage that emitted the signal
 * @druid : The gnomedruid that houses @page
 * @data : mother struct
 *
 * Will prepare the fixed page
 *
 * returns : nothing
 **/
void
stf_dialog_fixed_page_prepare (G_GNUC_UNUSED GnomeDruidPage *page,
			       G_GNUC_UNUSED GnomeDruid *druid,
			       DruidPageData_t *pagedata)
{
	FixedInfo_t *info = pagedata->fixed_info;
	GtkAdjustment *spinadjust;

	stf_parse_options_set_trim_spaces (info->fixed_run_parseoptions, TRIM_TYPE_NEVER);

#if 0
	stf_preview_set_startrow (info->fixed_run_renderdata, GTK_RANGE (info->fixed_scroll)->adjustment->value);
#endif

	spinadjust = gtk_spin_button_get_adjustment (info->fixed_colend);
	spinadjust->lower = 1;
	spinadjust->upper = stf_parse_get_longest_row_width (info->fixed_run_parseoptions, pagedata->cur);
	gtk_spin_button_set_adjustment (info->fixed_colend, spinadjust);

	fixed_page_update_preview (pagedata);
}

/**
 * stf_dialog_fixed_page_cleanup
 * @pagedata : mother struct
 *
 * Will cleanup fixed page run-time data
 *
 * returns : nothing
 **/
void
stf_dialog_fixed_page_cleanup (DruidPageData_t *pagedata)
{
	FixedInfo_t *info = pagedata->fixed_info;

	stf_preview_free (info->fixed_run_renderdata);

	if (info->fixed_run_parseoptions) {
		stf_parse_options_free (info->fixed_run_parseoptions);
		info->fixed_run_parseoptions = NULL;
	}
}

/**
 * stf_dialog_fixed_page_init
 * @gui : The glade gui of the dialog
 * @pagedata : pagedata mother struct passed to signal handlers etc.
 *
 * This routine prepares/initializes all widgets on the fixed Page of the
 * Druid.
 *
 * returns : nothing
 **/
void
stf_dialog_fixed_page_init (GladeXML *gui, DruidPageData_t *pagedata)
{
	FixedInfo_t *info;
	char *t[2];

	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);
	g_return_if_fail (pagedata->fixed_info != NULL);

	info = pagedata->fixed_info;

        /* Create/get object and fill information struct */
	info->fixed_collist = GTK_CLIST       (glade_xml_get_widget (gui, "fixed_collist"));
	info->fixed_colend  = GTK_SPIN_BUTTON (glade_xml_get_widget (gui, "fixed_colend"));
	info->fixed_add     = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_add"));
	info->fixed_remove  = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_remove"));
	info->fixed_clear   = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_clear"));
	info->fixed_auto    = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_auto"));
	info->fixed_data_container =          (glade_xml_get_widget (gui, "fixed_data_container"));

	/* Set properties */
	info->fixed_run_renderdata    = stf_preview_new (info->fixed_data_container, NULL);
	info->fixed_run_parseoptions  = stf_parse_options_new ();
	info->fixed_run_manual        = FALSE;
	info->fixed_run_index         = -1;
	info->fixed_run_mousedown     = FALSE;
	info->fixed_run_xorigin       = 0;

	stf_parse_options_set_type  (info->fixed_run_parseoptions, PARSE_TYPE_FIXED);

	gtk_clist_column_titles_passive (info->fixed_collist);

	t[0] = g_strdup ("0");
	t[1] = g_strdup ("-1");
	gtk_clist_append (info->fixed_collist, t);
	g_free (t[0]);
	g_free (t[1]);

	/* Connect signals */
	g_signal_connect (G_OBJECT (info->fixed_collist),
		"select_row",
		G_CALLBACK (fixed_page_collist_select_row), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_colend),
		"changed",
		G_CALLBACK (fixed_page_colend_changed), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_add),
		"clicked",
		G_CALLBACK (fixed_page_add_clicked), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_remove),
		"clicked",
		G_CALLBACK (fixed_page_remove_clicked), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_clear),
		"clicked",
		G_CALLBACK (fixed_page_clear_clicked), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_auto),
		"clicked",
		G_CALLBACK (fixed_page_auto_clicked), pagedata);

#if 0
	g_signal_connect (G_OBJECT (info->fixed_canvas),
		"motion_notify_event",
		G_CALLBACK (fixed_page_canvas_motion_notify_event), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_canvas),
		"button_press_event",
		G_CALLBACK (fixed_page_canvas_button_press_event), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_canvas),
		"button_release_event",
		G_CALLBACK (fixed_page_canvas_button_release_event), pagedata);
#endif
}
