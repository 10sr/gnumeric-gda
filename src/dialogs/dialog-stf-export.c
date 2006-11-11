/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-stf-export.c : implementation of the STF export dialog
 *
 * Copyright (C) Almer. S. Tigelaar.
 * EMail: almer1@dds.nl or almer-t@bigfoot.com
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
#include <gnumeric.h>
#include "dialog-stf-export.h"

#include <command-context.h>
#include <workbook.h>
#include <sheet.h>
#include <gui-util.h>
#include <goffice/gtk/go-charmap-sel.h>
#include <goffice/gtk/go-locale-sel.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

typedef enum {
	PAGE_SHEETS,
	PAGE_FORMAT
} TextExportPage;
enum {
	STF_EXPORT_COL_EXPORTED,
	STF_EXPORT_COL_SHEET_NAME,
	STF_EXPORT_COL_SHEET,
	STF_EXPORT_COL_NON_EMPTY,
	STF_EXPORT_COL_MAX
};

typedef struct {
	Workbook		*wb;

	GladeXML		*gui;
	WorkbookControlGUI	*wbcg;
	GtkWindow		*window;
	GtkWidget		*notebook;
	GtkWidget		*back_button, *next_button, *finish_button;

	struct {
		GtkListStore *model;
		GtkTreeView  *view;
		GtkWidget    *select_all, *select_none;
		GtkWidget    *up, *down, *top, *bottom;
		int num, num_selected, non_empty;
	} sheets;
	struct {
		GtkComboBox	 *termination;
		GtkComboBox	 *separator;
		GtkWidget	 *custom;
		GtkComboBox	 *quote;
		GtkComboBoxEntry *quotechar;
		GtkWidget	 *charset;
		GtkWidget	 *locale;
		GtkComboBox	 *transliterate;
		GtkComboBox	 *format;
	} format;

	GnmStfExport *result;
} TextExportState;

static void
sheet_page_separator_menu_changed (TextExportState *state)
{
	/* 9 == the custom entry */
	if (gtk_combo_box_get_active (state->format.separator) == 9) {
		gtk_widget_set_sensitive (state->format.custom, TRUE);
		gtk_widget_grab_focus (state->format.custom);
		gtk_editable_select_region (GTK_EDITABLE (state->format.custom), 0, -1);
	} else {
		gtk_widget_set_sensitive (state->format.custom, FALSE);
		/* If we don't use this the selection will remain blue */
		gtk_editable_select_region (GTK_EDITABLE (state->format.custom), 0, 0);
	}
}

static void
stf_export_dialog_format_page_init (TextExportState *state)
{
	GtkWidget *table;

	state->format.termination = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "format_termination"));
	gtk_combo_box_set_active (state->format.termination, 0);
	state->format.separator   = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "format_separator"));
	gtk_combo_box_set_active (state->format.separator, 0);
	state->format.custom      = glade_xml_get_widget (state->gui, "format_custom");
	state->format.quote       = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "format_quote"));
	gtk_combo_box_set_active (state->format.quote, 0);
	state->format.quotechar   = GTK_COMBO_BOX_ENTRY      (glade_xml_get_widget (state->gui, "format_quotechar"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (state->format.quotechar), 0);
	state->format.format      = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "format"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (state->format.format), 0);
	state->format.charset	  = go_charmap_sel_new (GO_CHARMAP_SEL_FROM_UTF8);
	state->format.locale	  = go_locale_sel_new ();
	state->format.transliterate = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "format_transliterate"));
	gnumeric_editable_enters (state->window, state->format.custom);
	gnumeric_editable_enters (state->window,
			gtk_bin_get_child (GTK_BIN (state->format.quotechar)));

	if (gnm_stf_export_can_transliterate ()) {
		gtk_combo_box_set_active (state->format.transliterate,
			GNM_STF_TRANSLITERATE_MODE_TRANS);
	} else {
		gtk_combo_box_set_active (state->format.transliterate,
			GNM_STF_TRANSLITERATE_MODE_ESCAPE);
		/* It might be better to render insensitive only one option than
		the whole list as in the following line but it is not possible
		with gtk-2.4. May be it should be changed when 2.6 is available	
		(inactivate only the transliterate item) */
		gtk_widget_set_sensitive (GTK_WIDGET (state->format.transliterate), FALSE);
	}

	table = glade_xml_get_widget (state->gui, "format_table");
	gtk_table_attach_defaults (GTK_TABLE (table), state->format.charset,
				   1, 2, 5, 6);
	gtk_table_attach_defaults (GTK_TABLE (table), state->format.locale,
				   1, 2, 6, 7);
	gtk_widget_show_all (table);

	g_signal_connect_swapped (state->format.separator,
		"changed",
		G_CALLBACK (sheet_page_separator_menu_changed), state);
}

static gboolean
cb_collect_exported_sheets (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
			    TextExportState *state)
{
	gboolean exported;
	Sheet *sheet;

	gtk_tree_model_get (model, iter,
		STF_EXPORT_COL_EXPORTED, &exported,
		STF_EXPORT_COL_SHEET,	 &sheet,
		-1);
	if (exported)
		gnm_stf_export_options_sheet_list_add (state->result, sheet);
	g_object_unref (sheet);
	return FALSE;
}
static void
stf_export_dialog_finish (TextExportState *state)
{
	GsfOutputCsvQuotingMode	quotingmode;
	GnmStfTransliterateMode transliteratemode;
	GnmStfFormatMode format;
	const char *eol;
	GString *triggers = g_string_new (NULL);
	char *separator, *quote;
	const char *charset;
	char *locale;

	/* What options */
	switch (gtk_combo_box_get_active (state->format.termination)) {
	default:
	case 0: eol = "\n"; break;
	case 1: eol = "\r"; break;
	case 2: eol = "\r\n"; break;
	}

	switch (gtk_combo_box_get_active (state->format.quote)) {
	default:
	case 0: quotingmode = GSF_OUTPUT_CSV_QUOTING_MODE_AUTO; break;
	case 1: quotingmode = GSF_OUTPUT_CSV_QUOTING_MODE_ALWAYS; break;
	case 2: quotingmode = GSF_OUTPUT_CSV_QUOTING_MODE_NEVER; break;
	}

	switch (gtk_combo_box_get_active (state->format.transliterate)) {
	case 0 :  transliteratemode = GNM_STF_TRANSLITERATE_MODE_TRANS; break;
	default:
	case 1 :  transliteratemode = GNM_STF_TRANSLITERATE_MODE_ESCAPE; break;
	}

	switch (gtk_combo_box_get_active (state->format.format)) {
	default:
	case 0: format = GNM_STF_FORMAT_AUTO; break;
	case 1: format = GNM_STF_FORMAT_RAW; break;
	case 2: format = GNM_STF_FORMAT_PRESERVE; break;
	}

	quote = gtk_editable_get_chars (GTK_EDITABLE (gtk_bin_get_child (GTK_BIN (state->format.quotechar))), 0, -1);

	switch (gtk_combo_box_get_active (state->format.separator)) {
	case 0: separator = g_strdup (" "); break;
	case 1: separator = g_strdup ("\t"); break;
	case 2: separator = g_strdup ("!"); break;
	case 3: separator = g_strdup (":"); break;
	default:
	case 4: separator = g_strdup (","); break;
	case 5: separator = g_strdup ("-"); break;
	case 6: separator = g_strdup ("|"); break;
	case 7: separator = g_strdup (";"); break;
	case 8: separator = g_strdup ("/"); break;
	case 9:
		separator = gtk_editable_get_chars
			(GTK_EDITABLE (state->format.custom), 0, -1);
		break;
	}

	charset = go_charmap_sel_get_encoding (GO_CHARMAP_SEL (state->format.charset));
	locale = go_locale_sel_get_locale (GO_LOCALE_SEL (state->format.locale));

	if (quotingmode == GSF_OUTPUT_CSV_QUOTING_MODE_AUTO) {
		g_string_append (triggers, " \t");
		g_string_append (triggers, eol);
		g_string_append (triggers, quote);
		g_string_append (triggers, separator);
	}

	state->result = g_object_new
		(GNM_STF_EXPORT_TYPE,
		 "eol", eol,
		 "quote", quote,
		 "quoting-mode", quotingmode,
		 "quoting-triggers", triggers->str,
		 "separator", separator,
		 "transliterate-mode", transliteratemode,
		 "format", format,
		 "charset", charset,
		 "locale", locale,
		 NULL);

	/* Which sheets */
	gnm_stf_export_options_sheet_list_clear (state->result);
	gtk_tree_model_foreach (GTK_TREE_MODEL (state->sheets.model),
		(GtkTreeModelForeachFunc) cb_collect_exported_sheets, state);

	g_free (separator);
	g_free (quote);
	g_string_free (triggers, TRUE);
	g_free (locale);

	gtk_dialog_response (GTK_DIALOG (state->window), GTK_RESPONSE_OK);
}

static void
set_sheet_selection_count (TextExportState *state, int n)
{
	state->sheets.num_selected = n;
	gtk_widget_set_sensitive (state->sheets.select_all, 
				  state->sheets.non_empty > n);
	gtk_widget_set_sensitive (state->sheets.select_none, n != 0);
	gtk_widget_set_sensitive (state->next_button, n != 0);
}

static gboolean
cb_set_sheet (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
	      gpointer data)
{
	gboolean value;

	gtk_tree_model_get (GTK_TREE_MODEL (model), iter,
			    STF_EXPORT_COL_NON_EMPTY, &value,
			    -1);
	if (value)
		gtk_list_store_set (GTK_LIST_STORE (model), iter,
				    STF_EXPORT_COL_EXPORTED, 
				    GPOINTER_TO_INT (data),
				    -1);
	return FALSE;
}

static void
cb_sheet_select_all (TextExportState *state)
{
	gtk_tree_model_foreach (GTK_TREE_MODEL (state->sheets.model),
		cb_set_sheet, GINT_TO_POINTER (TRUE));
	set_sheet_selection_count (state, state->sheets.non_empty);
}
static void
cb_sheet_select_none (TextExportState *state)
{
	gtk_tree_model_foreach (GTK_TREE_MODEL (state->sheets.model),
		cb_set_sheet, GINT_TO_POINTER (FALSE));
	set_sheet_selection_count (state, 0);
}

/**
 * Refreshes the buttons on a row (un)selection and selects the chosen sheet
 * for this view.
 */
static void
cb_selection_changed (GtkTreeSelection *new_selection,
		      TextExportState *state)
{
	GtkTreeIter iter, it;
	gboolean first_selected = TRUE, last_selected = TRUE;

	GtkTreeSelection  *selection = 
		(new_selection == NULL) 
		? gtk_tree_view_get_selection (state->sheets.view) 
		: new_selection;

	if (selection != NULL 
	    && gtk_tree_selection_count_selected_rows (selection) > 0
	    && gtk_tree_model_get_iter_first 
		       (GTK_TREE_MODEL (state->sheets.model),
			&iter)) {
		first_selected = gtk_tree_selection_iter_is_selected 
			(selection, &iter);
		it = iter;
		while (gtk_tree_model_iter_next 
		       (GTK_TREE_MODEL (state->sheets.model),
			&it))
			iter = it;
		last_selected = gtk_tree_selection_iter_is_selected 
			(selection, &iter);
	}

	gtk_widget_set_sensitive (state->sheets.top, !first_selected);
	gtk_widget_set_sensitive (state->sheets.up, !first_selected);
	gtk_widget_set_sensitive (state->sheets.bottom, !last_selected);
	gtk_widget_set_sensitive (state->sheets.down, !last_selected);

	return;
}

static void
move_element (TextExportState *state, gnm_iter_search_t iter_search)
{
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheets.view);
	GtkTreeModel *model;
	GtkTreeIter  a, b;

	g_return_if_fail (selection != NULL);

	if (!gtk_tree_selection_get_selected  (selection, &model, &a))
		return;

	b = a;
	if (!iter_search (model, &b))
		return;

	gtk_list_store_swap (state->sheets.model, &a, &b);
	cb_selection_changed (NULL, state);
}

static void 
cb_sheet_up   (TextExportState *state) 
{ 
	move_element (state, gnm_tree_model_iter_prev); 
}

static void 
cb_sheet_down (TextExportState *state) 
{ 
	move_element (state, gnm_tree_model_iter_next); 
}

static void 
cb_sheet_top   (TextExportState *state) 
{ 
	GtkTreeIter this_iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheets.view);

	g_return_if_fail (selection != NULL);

	if (!gtk_tree_selection_get_selected  (selection, NULL, &this_iter))
		return;
	
	gtk_list_store_move_after (state->sheets.model, &this_iter, NULL);
	cb_selection_changed (NULL, state);
}

static void 
cb_sheet_bottom (TextExportState *state)
{
	GtkTreeIter this_iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheets.view);

	g_return_if_fail (selection != NULL);

	if (!gtk_tree_selection_get_selected  (selection, NULL, &this_iter))
		return;
	gtk_list_store_move_before (state->sheets.model, &this_iter, NULL);
	cb_selection_changed (NULL, state);
}

static void
cb_sheet_export_toggled (GtkCellRendererToggle *cell,
			 const gchar *path_string,
			 TextExportState *state)
{
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	GtkTreeIter  iter;
	gboolean value;

	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (state->sheets.model), 
				     &iter, path)) {
		gtk_tree_model_get 
			(GTK_TREE_MODEL (state->sheets.model), &iter,
			 STF_EXPORT_COL_EXPORTED,	&value,
			 -1);
		gtk_list_store_set 
			(state->sheets.model, &iter,
			 STF_EXPORT_COL_EXPORTED,	!value,
			 -1);
		set_sheet_selection_count 
			(state,
			 state->sheets.num_selected + (value ? -1 : 1));
	} else {
		g_warning ("Did not get a valid iterator");
	}
	gtk_tree_path_free (path);
}

static void
stf_export_dialog_sheet_page_init (TextExportState *state)
{
	int i;
	Sheet *sheet, *cur_sheet;
	GtkTreeSelection  *selection;
	GtkTreeIter iter;
	GtkCellRenderer *renderer;

	state->sheets.select_all  = glade_xml_get_widget (state->gui, "sheet_select_all");
	state->sheets.select_none = glade_xml_get_widget (state->gui, "sheet_select_none");
	state->sheets.up	  = glade_xml_get_widget (state->gui, "sheet_up");
	state->sheets.down	  = glade_xml_get_widget (state->gui, "sheet_down");
	state->sheets.top	  = glade_xml_get_widget (state->gui, "sheet_top");
	state->sheets.bottom	  = glade_xml_get_widget (state->gui, "sheet_bottom");
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.up), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.down), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.top), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.bottom), 0., .5);

	state->sheets.model	  = gtk_list_store_new (STF_EXPORT_COL_MAX,
		G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_OBJECT, G_TYPE_BOOLEAN);
	state->sheets.view	  = GTK_TREE_VIEW (
		glade_xml_get_widget (state->gui, "sheet_list"));
	gtk_tree_view_set_model (state->sheets.view, GTK_TREE_MODEL (state->sheets.model));

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
		"toggled",
		G_CALLBACK (cb_sheet_export_toggled), state);
	gtk_tree_view_append_column (GTK_TREE_VIEW (state->sheets.view),
		gtk_tree_view_column_new_with_attributes 
				     (_("Export"),
				      renderer, 
				      "active", STF_EXPORT_COL_EXPORTED,
				      "activatable", STF_EXPORT_COL_NON_EMPTY, 
				      NULL));
	gtk_tree_view_append_column (GTK_TREE_VIEW (state->sheets.view),
		gtk_tree_view_column_new_with_attributes (_("Sheet"),
			gtk_cell_renderer_text_new (),
			"text", STF_EXPORT_COL_SHEET_NAME, NULL));

	selection = gtk_tree_view_get_selection (state->sheets.view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

	cur_sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (state->wbcg));
	state->sheets.num = workbook_sheet_count (state->wb);
	state->sheets.num_selected = 0;
	state->sheets.non_empty = 0;
	for (i = 0 ; i < state->sheets.num ; i++) {
		GnmRange total_range;
		gboolean export;
		sheet = workbook_sheet_by_index (state->wb, i);
		total_range = sheet_get_extent (sheet, TRUE);
		export =  !sheet_is_region_empty (sheet, &total_range);
		gtk_list_store_append (state->sheets.model, &iter);
		gtk_list_store_set (state->sheets.model, &iter,
				    STF_EXPORT_COL_EXPORTED,	export,
				    STF_EXPORT_COL_SHEET_NAME,	sheet->name_quoted,
				    STF_EXPORT_COL_SHEET,	sheet,
				    STF_EXPORT_COL_NON_EMPTY,   export,
				    -1);
		if (export) {
			state->sheets.num_selected++;
			state->sheets.non_empty++;
			gtk_tree_selection_select_iter (selection, &iter);
		}
	}
	set_sheet_selection_count (state, state->sheets.num_selected);

	g_signal_connect_swapped (G_OBJECT (state->sheets.select_all),
		"clicked",
		G_CALLBACK (cb_sheet_select_all), state);
	g_signal_connect_swapped (G_OBJECT (state->sheets.select_none),
		"clicked",
		G_CALLBACK (cb_sheet_select_none), state);
	g_signal_connect_swapped (G_OBJECT (state->sheets.up),
		"clicked",
		G_CALLBACK (cb_sheet_up), state);
	g_signal_connect_swapped (G_OBJECT (state->sheets.down),
		"clicked",
		G_CALLBACK (cb_sheet_down), state);
	g_signal_connect_swapped (G_OBJECT (state->sheets.top),
		"clicked",
		G_CALLBACK (cb_sheet_top), state);
	g_signal_connect_swapped (G_OBJECT (state->sheets.bottom),
		"clicked",
		G_CALLBACK (cb_sheet_bottom), state);

	cb_selection_changed (NULL, state);
	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (cb_selection_changed), state);

	gtk_tree_view_set_reorderable (state->sheets.view, TRUE);	
}

static void
stf_export_dialog_switch_page (TextExportState *state, TextExportPage new_page)
{
	gtk_notebook_set_current_page (GTK_NOTEBOOK (state->notebook),
				       new_page);
	if (new_page == PAGE_FORMAT) {
		gtk_widget_hide (state->next_button);
		gtk_widget_show (state->finish_button);
	} else {
		gtk_widget_show (state->next_button);
		gtk_widget_hide (state->finish_button);
	}

	if (state->sheets.non_empty > 1) {
		gtk_widget_show (state->back_button);
		gtk_widget_set_sensitive (state->back_button, new_page > 0);
	} else
		gtk_widget_hide (state->back_button);
}

static void
cb_back_page (TextExportState *state)
{
	int p = gtk_notebook_get_current_page (GTK_NOTEBOOK (state->notebook));
	stf_export_dialog_switch_page (state, p - 1);
}

static void
cb_next_page (TextExportState *state)
{
	int p = gtk_notebook_get_current_page (GTK_NOTEBOOK (state->notebook));
	stf_export_dialog_switch_page (state, p + 1);
}

/**
 * stf_dialog
 * @wbcg : #WorkbookControlGUI (can be NULL)
 * @wb : The #Workbook to export
 *
 * This will start the export assistant.
 * returns : A newly allocated GnmStfExport struct on success, NULL otherwise.
 **/
GnmStfExport *
stf_export_dialog (WorkbookControlGUI *wbcg, Workbook *wb)
{
	TextExportState state;

	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);

	state.gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"dialog-stf-export.glade", NULL, NULL);
	if (state.gui == NULL)
		return NULL;

	state.wb	  = wb;
	state.wbcg	  = wbcg;
	state.window	  = GTK_WINDOW (glade_xml_get_widget (state.gui, "text-export"));
	state.notebook	  = glade_xml_get_widget (state.gui, "text-export-notebook");
	state.back_button = glade_xml_get_widget (state.gui, "button-back");
	state.next_button = glade_xml_get_widget (state.gui, "button-next");
	state.finish_button = glade_xml_get_widget (state.gui, "button-finish");
	state.result	  = NULL;
	stf_export_dialog_sheet_page_init (&state);
	stf_export_dialog_format_page_init (&state);
	if (state.sheets.non_empty == 0) {
		gtk_widget_destroy (GTK_WIDGET (state.window));
		go_gtk_notice_dialog (wbcg_toplevel (wbcg),  GTK_MESSAGE_ERROR, 
				 _("This workbook does not contain any "
				   "exportable content."));
	} else {
		stf_export_dialog_switch_page 
			(&state,
			 (1 >= state.sheets.non_empty) ? 
			 PAGE_FORMAT : PAGE_SHEETS);
		gtk_widget_grab_default (state.next_button);
		g_signal_connect_swapped (G_OBJECT (state.back_button),
					  "clicked",
					  G_CALLBACK (cb_back_page), &state);
		g_signal_connect_swapped (G_OBJECT (state.next_button),
					  "clicked",
					  G_CALLBACK (cb_next_page), &state);
		g_signal_connect_swapped (G_OBJECT (state.finish_button),
					  "clicked",
					  G_CALLBACK (stf_export_dialog_finish),
					  &state);

		go_gtk_dialog_run (GTK_DIALOG (state.window), wbcg_toplevel (wbcg));
	}
	g_object_unref (state.gui);
	g_object_unref (state.sheets.model);

	return state.result;
}
