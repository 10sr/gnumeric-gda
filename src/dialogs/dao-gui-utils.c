/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dao-gui-utils.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2001, 2002 by Andreas J. Guelzow  <aguelzow@taliesin.ca>
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

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "dao-gui-utils.h"

#include "value.h"
#include "gui-util.h"
#include "workbook-control-gui.h"
#include "selection.h"

#include <gtk/gtktogglebutton.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtktable.h>

const char *output_group[] = {
	"newsheet-button",
	"newworkbook-button",
	"outputrange-button",
	"inplace-button",          /* used only in advanced filter  */
	0
};


/**
 * cb_focus_on_entry:
 * @widget:
 * @entry:
 *
 * callback to focus on an entry when the output
 * range button is pressed.
 *
 **/
static void
cb_focus_on_entry (GtkWidget *widget, GtkWidget *entry)
{
        if (GTK_TOGGLE_BUTTON (widget)->active)
		gtk_widget_grab_focus
			(GTK_WIDGET (gnm_expr_entry_get_entry
				     (GNM_EXPR_ENTRY (entry))));
}

/**
 * tool_set_focus_output_range:
 * @widget:
 * @focus_widget:
 * @state:
 *
 * Output range entry was focused. Switch to output range.
 *
 **/
static void
tool_set_focus_output_range (G_GNUC_UNUSED GtkWidget *widget,
			     G_GNUC_UNUSED GdkEventFocus *event,
			     GenericToolState *state)
{
	    gtk_toggle_button_set_active
		    (GTK_TOGGLE_BUTTON (state->output_range), TRUE);
}

/**
 * dialog__tool_preset_to_range:
 * @state:
 *
 * Load selection into range and switch to output range
 *
 **/

void 
dialog_tool_preset_to_range (GenericToolState *state)
{
	GnmRange const *sel;
	GtkWidget *w = glade_xml_get_widget (state->gui,
						  "notebook1");

	sel = selection_first_range (state->sv, NULL, NULL);
	if (sel)
		gnm_expr_entry_load_from_range (state->output_entry,
						state->sheet, sel);
	tool_set_focus_output_range (NULL, NULL, state);

	if (w && GTK_IS_NOTEBOOK (w))
		gtk_notebook_set_current_page (GTK_NOTEBOOK(w), 0); 
}


/**
 * dialog_tool_init_outputs:
 * @state:
 * @sensitivity_cb:
 *
 * Setup the standard output information
 *
 **/
void
dialog_tool_init_outputs (GenericToolState *state, GtkSignalFunc sensitivity_cb)
{
	GtkTable *table;

	state->new_sheet  = glade_xml_get_widget (state->gui,
						  "newsheet-button");
	if (state->new_sheet == NULL)
                return;
	state->new_workbook  = glade_xml_get_widget (state->gui,
						     "newworkbook-button");
	state->output_range  = glade_xml_get_widget (state->gui,
						     "outputrange-button");
	state->clear_outputrange_button = glade_xml_get_widget
		(state->gui, "clear_outputrange_button");
	state->retain_format_button = glade_xml_get_widget
		(state->gui, "retain_format_button");
	state->retain_comments_button = glade_xml_get_widget
		(state->gui, "retain_comments_button");
	state->put_menu = glade_xml_get_widget
		(state->gui, "put_menu");
	if (state->put_menu)
		gtk_option_menu_set_history 
			(GTK_OPTION_MENU (state->put_menu), 1);
	table = GTK_TABLE (glade_xml_get_widget (state->gui, "output-table"));
	state->output_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->output_entry,
				  GNM_EE_SINGLE_RANGE, GNM_EE_MASK);
	gtk_table_attach (table, GTK_WIDGET (state->output_entry),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnm_setup_label_atk (state->output_range,
			     GTK_WIDGET (state->output_entry));
	g_signal_connect (G_OBJECT (state->output_range),
		"toggled",
		G_CALLBACK (cb_focus_on_entry), state->output_entry);
	g_signal_connect
		(G_OBJECT (gnm_expr_entry_get_entry
			   (GNM_EXPR_ENTRY (state->output_entry))),
		 "focus-in-event",
		 G_CALLBACK (tool_set_focus_output_range), state);
	g_signal_connect_after (G_OBJECT (state->output_entry),
		"changed",
		G_CALLBACK (sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->output_range),
		"toggled",
		G_CALLBACK (sensitivity_cb), state);

 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->output_entry));
	gtk_widget_show (GTK_WIDGET (state->output_entry));

	return;
}

/**
 * parse_output:
 *
 * @state:
 * @dao:
 *
 * fill dao with information from the standard output section of a dialog
 */
data_analysis_output_t *
parse_output (GenericToolState *state, data_analysis_output_t *dao)
{
        GnmValue *output_range;
	GtkWidget *button;


	switch (gnumeric_glade_group_value (state->gui, output_group)) {
	case 0:
	default:
		dao = dao_init (dao, NewSheetOutput);
		break;
	case 1:
		dao = dao_init (dao, NewWorkbookOutput);
		break;
	case 2:
		output_range = gnm_expr_entry_parse_as_value
			(GNM_EXPR_ENTRY (state->output_entry), state->sheet);
		g_return_val_if_fail (output_range != NULL, NULL);
		g_return_val_if_fail (output_range->type == VALUE_CELLRANGE, NULL);

		dao = dao_init (dao, RangeOutput);
		dao->start_col = output_range->v_range.cell.a.col;
		dao->start_row = output_range->v_range.cell.a.row;
		dao->cols = output_range->v_range.cell.b.col
			- output_range->v_range.cell.a.col + 1;
		dao->rows = output_range->v_range.cell.b.row
			- output_range->v_range.cell.a.row + 1;
		dao->sheet = output_range->v_range.cell.a.sheet;

		value_release (output_range);
		break;
	case 3:
		output_range = gnm_expr_entry_parse_as_value (
			state->input_entry, state->sheet);

		g_return_val_if_fail (output_range != NULL, NULL);
		g_return_val_if_fail (output_range->type == VALUE_CELLRANGE, NULL);

		dao = dao_init (dao, InPlaceOutput);
		dao->start_col = output_range->v_range.cell.a.col;
		dao->start_row = output_range->v_range.cell.a.row;
		dao->cols = output_range->v_range.cell.b.col
			- output_range->v_range.cell.a.col + 1;
		dao->rows = output_range->v_range.cell.b.row
			- output_range->v_range.cell.a.row + 1;
		dao->sheet = output_range->v_range.cell.a.sheet;

		value_release (output_range);
		break;
	}

	button = glade_xml_get_widget (state->gui, "autofit_button");
	if (button != NULL)
		dao->autofit_flag = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (button));

	if (state->clear_outputrange_button != NULL)
		dao->clear_outputrange = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (state->clear_outputrange_button));

	if (state->retain_format_button != NULL)
		dao->retain_format = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (state->retain_format_button));

	if (state->retain_comments_button != NULL)
		dao->retain_comments = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (state->retain_comments_button));
	if (state->put_menu != NULL)
		dao->put_formulas 
			= (gtk_option_menu_get_history 
			   (GTK_OPTION_MENU (state->put_menu)) 
			   != 0);

	return dao;
}
