/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-preferences.c: Dialog to change the order of sheets in the Gnumeric
 * spreadsheet
 *
 * Author:
 * 	Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001, 2002 Jody Goldberg <jody@gnome.org>
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
#include <gnumeric.h>
#include "application.h"
#include "dialogs.h"
#include "help.h"

#include "mstyle.h"
#include "value.h"
#include "workbook.h"
#include "number-match.h"
#include "widgets/widget-font-selector.h"
#include "widgets/gnumeric-cell-renderer-text.h"

#include "gnumeric-gconf-priv.h"
#include "gnumeric-gconf.h"

#include <gui-util.h>
#include <glade/glade.h>
#include <gtk/gtk.h>
#include <string.h>

enum {
	ITEM_ICON,
	ITEM_NAME,
	PAGE_NUMBER,
	NUM_COLUMNS
};

typedef struct {
	GladeXML	*gui;
	GtkWidget	*dialog;
	GtkWidget	*notebook;
	GtkTextView	*description;
	GSList		*pages;
	GtkTreeStore    *store;
	GtkTreeView     *view;
	GConfClient	*gconf;
	Workbook	*wb;
} PrefState;

typedef struct {
	char const *page_name;
	char const *icon_name;
	char const *parent_path;
	GtkWidget * (*page_initializer) (PrefState *state, gpointer data,
					 GtkNotebook *notebook, gint page_num);
	void	    (*page_open)	(PrefState *state, gpointer data,
					 GtkNotebook *notebook, gint page_num);
	gpointer data;
} page_info_t;

static void
dialog_pref_add_item (PrefState *state, char const *page_name, char const *icon_name, 
		      int page, char const* parent_path)
{
	GtkTreeIter iter, parent;
	GdkPixbuf * icon = gtk_widget_render_icon (state->dialog, icon_name,
						   GTK_ICON_SIZE_MENU,
						   "Gnumeric-Preference-Dialog");
	if ((parent_path != NULL) && gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (state->store),
									  &parent, parent_path))
		gtk_tree_store_append (state->store, &iter, &parent);
	else
		gtk_tree_store_append (state->store, &iter, NULL);

	gtk_tree_store_set (state->store, &iter,
			    ITEM_ICON, icon,
			    ITEM_NAME, _(page_name),
			    PAGE_NUMBER, page,
			    -1);
	g_object_unref (icon);
}


static gboolean
cb_pref_notification_destroy (G_GNUC_UNUSED GtkWidget *page,
			      guint notification)
{
	gconf_client_notify_remove (application_get_gconf_client (), notification);
	return TRUE;
}

static void
dialog_pref_page_open (PrefState *state)
{
	GtkTextIter start;
	GtkTextIter end;
	GtkTextBuffer *buffer;

	buffer = gtk_text_view_get_buffer (state->description);
	gtk_text_buffer_get_bounds (buffer, &start, &end);
	gtk_text_buffer_delete (buffer, &start, &end);
}

static void
dialog_pref_load_description_from_schema (PrefState *state, char const *schema_path)
{
	GConfSchema *the_schema;
	GtkTextBuffer *buffer;

	the_schema = gconf_client_get_schema (state->gconf, schema_path, NULL);
	buffer = gtk_text_view_get_buffer (state->description);

	g_return_if_fail (the_schema != NULL);

	gtk_text_buffer_set_text (buffer, gconf_schema_get_long_desc (the_schema), -1);
	gconf_schema_free (the_schema);
}

static void
dialog_pref_load_description (PrefState *state, char const *text)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (state->description);

	gtk_text_buffer_set_text (buffer, text, -1);
}

typedef void (* cb_pref_window_set_t) (GConfClient *gconf, guint cnxn_id, GConfEntry *entry,
				       GtkSpinButton *button);
typedef void  (* cb_pref_window_changed_t) (GtkSpinButton *button, PrefState *state);

static void
dialog_pref_create_int_spin (char const *key, char const *schema_key,
			     GtkWidget *table, gint row, PrefState *state,
			     gint val, gint from, gint to, gint step,
			     cb_pref_window_set_t pref_window_set,
			     cb_pref_window_changed_t pref_window_changed)
{
	guint notif;
	GtkWidget *item;
	GConfSchema *the_schema;
	GtkTooltips *the_tip;

	the_schema = gconf_client_get_schema (state->gconf, schema_key, NULL);
	item = gtk_label_new (the_schema ? gconf_schema_get_short_desc (the_schema)
			      : "schema missing");
	gtk_label_set_justify (GTK_LABEL (item), GTK_JUSTIFY_LEFT);
	gtk_table_attach (GTK_TABLE (table), item, 0, 1, row, row + 1, 0,
			  GTK_FILL, 5, 5);
	item =  gtk_spin_button_new (GTK_ADJUSTMENT (gtk_adjustment_new (val, from, to,
									 step, step, step)),
				     1, 0);
	pref_window_set (state->gconf, 0, NULL, GTK_SPIN_BUTTON (item));
	notif = gconf_client_notify_add (state->gconf, key,
			  (GConfClientNotifyFunc) pref_window_set,
			  item, NULL, NULL);
	g_signal_connect (G_OBJECT (table),
		"destroy",
		G_CALLBACK (cb_pref_notification_destroy), GINT_TO_POINTER (notif));
	g_signal_connect (G_OBJECT (item),
		"value-changed",
		G_CALLBACK (pref_window_changed), state);
	gtk_table_attach (GTK_TABLE (table), item, 1, 2, row, row + 1, GTK_FILL | GTK_EXPAND,
			  GTK_FILL | GTK_SHRINK, 5, 5);
	if (the_schema) {
		the_tip = gtk_tooltips_new ();
		gtk_tooltips_set_tip (the_tip, item,
				      gconf_schema_get_long_desc (the_schema), NULL);
		gconf_schema_free (the_schema);
	}
}

static void
dialog_pref_create_float_spin (char const *key, char const *schema_key,
			       GtkWidget *table, gint row, PrefState *state,
			       gnm_float val, gnm_float from, gnm_float to, gnm_float step,
			       gint digits,
			       cb_pref_window_set_t pref_window_set,
			       cb_pref_window_changed_t pref_window_changed)
{
	guint notif;
	GtkWidget *item;
	GConfSchema *the_schema;
	GtkTooltips *the_tip;

	the_schema = gconf_client_get_schema (state->gconf, schema_key, NULL);
	item = gtk_label_new (the_schema ? gconf_schema_get_short_desc (the_schema)
			      : "schema missing");
	gtk_label_set_justify (GTK_LABEL (item), GTK_JUSTIFY_LEFT);
	gtk_table_attach (GTK_TABLE (table), item, 0, 1, row, row + 1, 0,
			  GTK_FILL, 5, 5);
	item =  gtk_spin_button_new (GTK_ADJUSTMENT (gtk_adjustment_new (val, from, to,
									 step, step, step)),
				     1, digits);
	pref_window_set (state->gconf, 0, NULL, GTK_SPIN_BUTTON (item));
	notif = gconf_client_notify_add (state->gconf, key,
			  (GConfClientNotifyFunc) pref_window_set,
			  item, NULL, NULL);
	g_signal_connect (G_OBJECT (table),
		"destroy",
		G_CALLBACK (cb_pref_notification_destroy), GINT_TO_POINTER (notif));
	g_signal_connect (G_OBJECT (item),
		"value-changed",
		G_CALLBACK (pref_window_changed), state);
	gtk_table_attach (GTK_TABLE (table), item, 1, 2, row, row + 1, GTK_FILL | GTK_EXPAND,
			  GTK_FILL | GTK_SHRINK, 5, 5);
	if (the_schema) {
		the_tip = gtk_tooltips_new ();
		gtk_tooltips_set_tip (the_tip, item,
				      gconf_schema_get_long_desc (the_schema), NULL);
		gconf_schema_free (the_schema);
	}
}

typedef void (* cb_pref_window_set_toggled_t) (GConfClient *gconf, guint cnxn_id,
					       GConfEntry *entry,
					       GtkToggleButton *button);
typedef void  (* cb_pref_window_toggled_t) (GtkToggleButton *button, PrefState *state);

static void
dialog_pref_create_checkbox (char const *key, char const *schema_key,
			     GtkWidget *table, gint row, PrefState *state,
			     cb_pref_window_set_toggled_t pref_window_set,
			     cb_pref_window_toggled_t pref_window_toggled)
{
	guint notif;
	GtkWidget *item;
	GConfSchema *the_schema;
	GtkTooltips *the_tip;

	the_schema = gconf_client_get_schema (state->gconf, schema_key, NULL);
	item = gtk_check_button_new_with_label (the_schema
						? gconf_schema_get_short_desc (the_schema)
						: "schema missing");
	pref_window_set (state->gconf, 0, NULL, GTK_TOGGLE_BUTTON (item));
	notif = gconf_client_notify_add (state->gconf, key,
			  (GConfClientNotifyFunc) pref_window_set,
			  item, NULL, NULL);
	g_signal_connect (G_OBJECT (table),
		"destroy",
		G_CALLBACK (cb_pref_notification_destroy), GINT_TO_POINTER (notif));
	g_signal_connect (G_OBJECT (item),
		"toggled",
		G_CALLBACK (pref_window_toggled), state);
	gtk_table_attach (GTK_TABLE (table), item, 0, 2, row, row + 1, GTK_FILL | GTK_SHRINK,
			  GTK_FILL | GTK_SHRINK, 5, 5);
	if (the_schema) {
		the_tip = gtk_tooltips_new ();
		gtk_tooltips_set_tip (the_tip, item,
				      gconf_schema_get_long_desc (the_schema), NULL);
		gconf_schema_free (the_schema);
	}
}


/*******************************************************************************************/
/*                     Tree View of selected configuration variables                       */
/*******************************************************************************************/

enum {
	PREF_NAME,
	PREF_VALUE,
	PREF_PATH,
	PREF_SCHEMA,
	IS_EDITABLE,
	NUM_COLMNS
};

typedef struct {
	char const *path;
	char const *parent;
	char const *schema;
	GtkTreeView *treeview;
} pref_tree_data_t;

typedef struct {
	char const *path;
	GtkTreeIter iter;
	gboolean iter_valid;
} search_cb_t;


static pref_tree_data_t pref_tree_data[] = {
	{FUNCTION_SELECT_GCONF_NUM_OF_RECENT, NULL,
	                      "/schemas" FUNCTION_SELECT_GCONF_NUM_OF_RECENT},
	{GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE, NULL,
	                      "/schemas" GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE},
	{DIALOGS_GCONF_UNFOCUSED_RS, NULL,
	                      "/schemas" DIALOGS_GCONF_UNFOCUSED_RS},
	{NULL, NULL, NULL}
};

static pref_tree_data_t pref_tree_data_danger[] = {
	{GNUMERIC_GCONF_GUI_RES_H, NULL,
	                      "/schemas" GNUMERIC_GCONF_GUI_RES_H},
	{GNUMERIC_GCONF_GUI_RES_V, NULL,
	                      "/schemas" GNUMERIC_GCONF_GUI_RES_V},
	{GNUMERIC_GCONF_GUI_ED_RECALC_LAG, NULL,
	                      "/schemas" GNUMERIC_GCONF_GUI_ED_RECALC_LAG},
	{NULL, NULL, NULL}
};

#define OBJECT_DATA_PATH_MODEL "treeview %i"

static void
cb_pref_tree_selection_changed (GtkTreeSelection *selection,
				PrefState *state)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	char *schema_path;

	g_return_if_fail (selection != NULL);

	if (gtk_tree_selection_get_selected (selection,  &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    PREF_SCHEMA, &schema_path,
				    -1);
		if (schema_path) {
			dialog_pref_load_description_from_schema (state, schema_path);
			g_free (schema_path);
			return;
		}
	}
	dialog_pref_page_open (state);
	return;
}

static void
pref_tree_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		     GtkNotebook *notebook, gint page_num)
{
	char *object_data_path;
	GtkTreeView *view;

	object_data_path = g_strdup_printf (OBJECT_DATA_PATH_MODEL, page_num);
	view =  g_object_get_data (G_OBJECT (notebook), object_data_path);
	g_free (object_data_path);

	cb_pref_tree_selection_changed (gtk_tree_view_get_selection (view), state);
}


static gboolean
pref_tree_find_iter (GtkTreeModel *model,
		     G_GNUC_UNUSED GtkTreePath *tree_path,
		     GtkTreeIter *iter, search_cb_t *data)
{
	char *path;

	gtk_tree_model_get (model, iter,
			    PREF_PATH, &path,
			    -1);

	if (strcmp (path, data->path) == 0) {
		data->iter = *iter;
		data->iter_valid = TRUE;
	}

	g_free (path);
	return data->iter_valid;
}

static void
pref_tree_set_model (GConfClient *gconf, GtkTreeModel *model, GtkTreeIter *iter)
{
	char *schema_path;
	char *key;
	GConfSchema *the_schema;
	char *value_string;

	gtk_tree_model_get (model, iter,
			    PREF_SCHEMA, &schema_path,
			    PREF_PATH, &key,
			    -1);
	the_schema = gconf_client_get_schema (gconf, schema_path, NULL);

	g_return_if_fail (the_schema != NULL);

	switch (gconf_schema_get_type (the_schema)) {
	case GCONF_VALUE_STRING:
		value_string = gconf_client_get_string (gconf, key, NULL);

		break;
	case GCONF_VALUE_INT:
		value_string = g_strdup_printf ("%i", gconf_client_get_int (gconf, key,
									    NULL));
		break;
	case GCONF_VALUE_FLOAT:
		value_string = g_strdup_printf ("%f", gconf_client_get_float (gconf, key,
									    NULL));
		break;
	case GCONF_VALUE_BOOL:
		value_string = gconf_client_get_bool (gconf, key, NULL) ?
			g_strdup (_("TRUE")) :
			g_strdup (_("FALSE"));
		break;
	default:
		value_string = g_strdup ("ERROR FIXME");
	}
	gtk_tree_store_set (GTK_TREE_STORE (model), iter,
			    PREF_NAME, gconf_schema_get_short_desc (the_schema),
			    PREF_VALUE, value_string,
			    -1);
	g_free (key);
	g_free (schema_path);
	g_free (value_string);
	gconf_schema_free (the_schema);
}

static void
cb_pref_tree_changed_notification (GConfClient *gconf,
				   G_GNUC_UNUSED guint cnxn_id,
				   GConfEntry *entry,
				   GtkTreeModel *model)
{
	search_cb_t search_cb;
	search_cb.iter_valid = FALSE;
	search_cb.path = gconf_entry_get_key (entry);

	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) pref_tree_find_iter,
				&search_cb);
	if (search_cb.iter_valid) {
		pref_tree_set_model (gconf, model, &search_cb.iter);
	} else
		g_warning ("Unexpected gconf notification!");
}

static void
cb_value_edited (GtkCellRendererText *cell,
		 gchar		*path_string,
		 gchar		*new_text,
		 PrefState	*state)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	char        *key;
	char        *schema;
	gint        the_int;
	gnm_float  the_float;
	gboolean    the_bool;
	Value       *value;
	GConfClient *client = application_get_gconf_client ();
	GConfSchema *the_schema;
	gboolean     err;
	GtkTreeModel *model = g_object_get_data (G_OBJECT (cell), "model");
	path = gtk_tree_path_new_from_string (path_string);

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    PREF_PATH, &key,
			    PREF_SCHEMA, &schema,
			    -1);
	the_schema = gconf_client_get_schema (client, schema, NULL);
	switch (gconf_schema_get_type (the_schema)) {
	case GCONF_VALUE_STRING:
		gconf_client_set_string (client, key, new_text, NULL);
		break;
	case GCONF_VALUE_FLOAT:
		value = format_match_number (new_text, NULL,
				workbook_date_conv (state->wb));
		if (value != NULL) {
			the_float =  value_get_as_float (value);
			gconf_client_set_float (client, key, the_float, NULL);
		}
		if (value)
			value_release (value);
		break;
	case GCONF_VALUE_INT:
		value = format_match_number (new_text, NULL,
				workbook_date_conv (state->wb));
		if (value != NULL) {
			the_int =  value_get_as_int (value);
			gconf_client_set_int (client, key, the_int, NULL);
		}
		if (value)
			value_release (value);
		break;
	case GCONF_VALUE_BOOL:
		value = format_match_number (new_text, NULL,
				workbook_date_conv (state->wb));
		if (value != NULL) {
			err = FALSE;
			the_bool =  value_get_as_bool (value, &err);
			gconf_client_set_bool (client, key, the_bool, NULL);
		}
		if (value)
			value_release (value);
		break;
	default:
		g_warning ("Unsupported gconf type in preference dialog");
	}


	gtk_tree_path_free (path);
	g_free (key);
	g_free (schema);
	gconf_schema_free (the_schema);
}

static GtkWidget *
pref_tree_initializer (PrefState *state, gpointer data,
		       G_GNUC_UNUSED GtkNotebook *notebook,
		       gint page_num)
{
	pref_tree_data_t  *this_pref_tree_data = data;
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;
	GtkTreeStore      *model;
	GtkTreeView       *view;
	GtkWidget         *page = gtk_scrolled_window_new (NULL, NULL);
	gint               i;
	GtkCellRenderer   *renderer;
	gchar             *object_data_path;

	gtk_widget_set_size_request (page, 350, 250);
	gtk_scrolled_window_set_policy  (GTK_SCROLLED_WINDOW (page),
					 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	model = gtk_tree_store_new (NUM_COLMNS,
				    G_TYPE_STRING,
				    G_TYPE_STRING,
				    G_TYPE_STRING,
				    G_TYPE_STRING,
				    G_TYPE_BOOLEAN);
	view = GTK_TREE_VIEW (gtk_tree_view_new_with_model
			      (GTK_TREE_MODEL (model)));
	selection = gtk_tree_view_get_selection (view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	column = gtk_tree_view_column_new_with_attributes (_("Description"),
							   gtk_cell_renderer_text_new (),
							   "text", PREF_NAME,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, PREF_NAME);
	gtk_tree_view_append_column (view, column);

	renderer = gnumeric_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Value"),
							   renderer,
							   "text", PREF_VALUE,
							   "editable", IS_EDITABLE,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, PREF_VALUE);
	gtk_tree_view_append_column (view, column);
	g_signal_connect (G_OBJECT (renderer),
		"edited",
		G_CALLBACK (cb_value_edited), state);
	g_object_set_data (G_OBJECT (renderer), "model", model);

	gtk_tree_view_set_headers_visible (view, TRUE);
	gtk_container_add (GTK_CONTAINER (page), GTK_WIDGET (view));

	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (view)),
		"changed",
		G_CALLBACK (cb_pref_tree_selection_changed), state);

	for (i = 0; this_pref_tree_data[i].path; i++) {
		pref_tree_data_t *this_pref = &this_pref_tree_data[i];
		GtkTreeIter      iter;
		guint notification;

		gtk_tree_store_append (model, &iter, NULL);

		gtk_tree_store_set (model, &iter,
				    PREF_PATH, this_pref->path,
				    PREF_SCHEMA, this_pref->schema,
				    IS_EDITABLE, TRUE,
				    -1);
		pref_tree_set_model (state->gconf, GTK_TREE_MODEL (model), &iter);

		notification = gconf_client_notify_add
			(state->gconf, this_pref_tree_data[i].path,
			 (GConfClientNotifyFunc) cb_pref_tree_changed_notification,
			 model, NULL, NULL);

		g_signal_connect (G_OBJECT (page),
			"destroy",
			G_CALLBACK (cb_pref_notification_destroy), GINT_TO_POINTER (notification));
	}

	object_data_path = g_strdup_printf (OBJECT_DATA_PATH_MODEL, page_num);
	g_object_set_data_full (G_OBJECT (state->notebook), object_data_path, view, NULL);
	g_free (object_data_path);

	gtk_widget_show_all (page);

	return page;
}

/*******************************************************************************************/
/*                     Default Font Selector                                               */
/*******************************************************************************************/

static void
pref_font_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		     G_GNUC_UNUSED GtkNotebook *notebook,
		     G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description_from_schema (state, "/schemas" GNUMERIC_GCONF_FONT_NAME);
}

static void
cb_pref_font_set_fonts (GConfClient *gconf,
		        G_GNUC_UNUSED guint cnxn_id,
			GConfEntry *entry,
		        GtkWidget *page)
{
	if (entry == NULL || 0 == strcmp (gconf_entry_get_key (entry),
					  GNUMERIC_GCONF_FONT_NAME)) {
		char      *font_name = gconf_client_get_string (gconf,
					  GNUMERIC_GCONF_FONT_NAME, NULL);
		font_selector_set_name      (FONT_SELECTOR (page), font_name);
		g_free (font_name);
	}
	if (entry == NULL || 0 == strcmp (gconf_entry_get_key (entry),
					  GNUMERIC_GCONF_FONT_SIZE)) {
		double    size = gconf_client_get_float (gconf,
					  GNUMERIC_GCONF_FONT_SIZE, NULL);
		font_selector_set_points    (FONT_SELECTOR (page), size);
	}
	if (entry == NULL || 0 == strcmp (gconf_entry_get_key (entry),
					  GNUMERIC_GCONF_FONT_BOLD)
	    || 0 == strcmp (gconf_entry_get_key (entry),
			    GNUMERIC_GCONF_FONT_ITALIC)) {
		gboolean  is_bold = gconf_client_get_bool (gconf,
			    GNUMERIC_GCONF_FONT_BOLD, NULL);
		gboolean  is_italic = gconf_client_get_bool (gconf,
			    GNUMERIC_GCONF_FONT_ITALIC, NULL);
		font_selector_set_style     (FONT_SELECTOR (page), is_bold, is_italic);
	}
}

static gboolean
cb_pref_font_has_changed (G_GNUC_UNUSED FontSelector *fs,
			  MStyle *mstyle, PrefState *state)
{
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_SIZE))
		gconf_client_set_float (state->gconf,
					GNUMERIC_GCONF_FONT_SIZE,
					mstyle_get_font_size (mstyle), NULL);
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_NAME))
		gconf_client_set_string (state->gconf,
					 GNUMERIC_GCONF_FONT_NAME,
					 mstyle_get_font_name (mstyle), NULL);
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_BOLD))
		gconf_client_set_bool (state->gconf,
				       GNUMERIC_GCONF_FONT_BOLD,
				       mstyle_get_font_bold (mstyle), NULL);
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_ITALIC))
		gconf_client_set_bool (state->gconf,
				       GNUMERIC_GCONF_FONT_ITALIC,
				       mstyle_get_font_italic (mstyle), NULL);
	return TRUE;
}

static GtkWidget *
pref_font_initializer (PrefState *state,
		       G_GNUC_UNUSED gpointer data,
		       G_GNUC_UNUSED GtkNotebook *notebook,
		       G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = font_selector_new ();
	guint notification;

	cb_pref_font_set_fonts (state->gconf, 0, NULL, page);

	notification = gconf_client_notify_add (state->gconf, GNUMERIC_GCONF_FONT_DIRECTORY,
						(GConfClientNotifyFunc) cb_pref_font_set_fonts,
						page, NULL, NULL);

	g_signal_connect (G_OBJECT (page),
		"destroy",
		G_CALLBACK (cb_pref_notification_destroy), GINT_TO_POINTER (notification));
	g_signal_connect (G_OBJECT (page),
		"font_changed",
		G_CALLBACK (cb_pref_font_has_changed), state);

	gtk_widget_show_all (page);

	return page;
}

/*******************************************************************************************/
/*                     Undo Preferences Page                                              */
/*******************************************************************************************/

static void
pref_undo_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		     G_GNUC_UNUSED GtkNotebook *notebook,
		     G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description (state,
				      _("The items on this page customize the "
					"behaviour of the undo/redo system."));
}

static void
cb_pref_undo_set_sheet_name (GConfClient *gconf,
			     G_GNUC_UNUSED guint cnxn_id,
			     G_GNUC_UNUSED GConfEntry *entry,
			     GtkToggleButton *button)
{
	gboolean is_set_gconf = gconf_client_get_bool (gconf,
						       GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME,
						       NULL);
	gboolean is_set_button = gtk_toggle_button_get_active (button);
	if (is_set_gconf != is_set_button)
		gtk_toggle_button_set_active (button, is_set_gconf);
}

static void
cb_pref_undo_sheet_name_toggled (GtkToggleButton *button, PrefState *state)
{
		gconf_client_set_bool (state->gconf,
				       GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME,
				       gtk_toggle_button_get_active (button),
				       NULL);
}

static void
cb_pref_undo_set_max_descriptor_width (GConfClient *gconf,
				       G_GNUC_UNUSED guint cnxn_id,
				       G_GNUC_UNUSED GConfEntry *entry,
				       GtkSpinButton *button)
{
	gint int_in_gconf = gconf_client_get_int (gconf,
						  GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH,
						  NULL);
	gint int_in_button = gtk_spin_button_get_value_as_int (button);
	if (int_in_gconf != int_in_button)
		gtk_spin_button_set_value (button, (gdouble) int_in_gconf);
}

static void
cb_pref_undo_max_descriptor_width_changed (GtkSpinButton *button, PrefState *state)
{
		gconf_client_set_int (state->gconf,
				       GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH,
				       gtk_spin_button_get_value_as_int (button),
				       NULL);
}

static void
cb_pref_undo_set_size (GConfClient *gconf,
		       G_GNUC_UNUSED guint cnxn_id,
		       G_GNUC_UNUSED GConfEntry *entry,
		       GtkSpinButton *button)
{
	gint int_in_gconf = gconf_client_get_int (gconf,
						  GNUMERIC_GCONF_UNDO_SIZE,
						  NULL);
	gint int_in_button = gtk_spin_button_get_value_as_int (button);
	if (int_in_gconf != int_in_button)
		gtk_spin_button_set_value (button, (gdouble) int_in_gconf);
}

static void
cb_pref_undo_size_changed (GtkSpinButton *button, PrefState *state)
{
		gconf_client_set_int (state->gconf,
				       GNUMERIC_GCONF_UNDO_SIZE,
				       gtk_spin_button_get_value_as_int (button),
				       NULL);
}

static void
cb_pref_undo_set_maxnum (GConfClient *gconf,
			 G_GNUC_UNUSED guint cnxn_id,
			 G_GNUC_UNUSED GConfEntry *entry,
			 GtkSpinButton *button)
{
	gint int_in_gconf = gconf_client_get_int (gconf,
						  GNUMERIC_GCONF_UNDO_MAXNUM,
						  NULL);
	gint int_in_button = gtk_spin_button_get_value_as_int (button);
	if (int_in_gconf != int_in_button)
		gtk_spin_button_set_value (button, (gdouble) int_in_gconf);
}

static void
cb_pref_undo_maxnum_changed (GtkSpinButton *button, PrefState *state)
{
		gconf_client_set_int (state->gconf,
				       GNUMERIC_GCONF_UNDO_MAXNUM,
				       gtk_spin_button_get_value_as_int (button),
				       NULL);
}

static GtkWidget *
pref_undo_page_initializer (PrefState *state,
			    G_GNUC_UNUSED gpointer data,
			    G_GNUC_UNUSED GtkNotebook *notebook,
			    G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (4, 2, FALSE);
	gint row = 0;

	/* Sheet name check box */
	dialog_pref_create_checkbox (GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME,
				     "/schemas" GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME,
				     page, row++, state,
				     cb_pref_undo_set_sheet_name,
				     cb_pref_undo_sheet_name_toggled);

	/* Descriptor Width Spin Button */
	dialog_pref_create_int_spin (GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH,
				     "/schemas" GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH,
				     page, row++, state,
				     5, 5, 200, 1,
				     cb_pref_undo_set_max_descriptor_width,
				     cb_pref_undo_max_descriptor_width_changed);

	/* Undo Size Spin Button */
	dialog_pref_create_int_spin (GNUMERIC_GCONF_UNDO_SIZE,
				     "/schemas" GNUMERIC_GCONF_UNDO_SIZE,
				     page, row++, state,
				     1000, 0, 30000, 100,
				     cb_pref_undo_set_size,
				     cb_pref_undo_size_changed);

	/* Undo Size Spin Button */
	dialog_pref_create_int_spin (GNUMERIC_GCONF_UNDO_MAXNUM,
				     "/schemas" GNUMERIC_GCONF_UNDO_MAXNUM,
				     page, row++, state,
				     20, 1, 200, 1,
				     cb_pref_undo_set_maxnum,
				     cb_pref_undo_maxnum_changed);

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     Sort Preferences Page                                              */
/*******************************************************************************************/

static void
pref_sort_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		     G_GNUC_UNUSED GtkNotebook *notebook,
		     G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description (state,
				      _("The items on this page customize the "
					"inital settings of the sort dialog and "
					"the behaviour of the sort toolbar buttons."));
}

static void
cb_pref_sort_set_retain_formats (GConfClient *gconf,
				 G_GNUC_UNUSED guint cnxn_id,
				 G_GNUC_UNUSED GConfEntry *entry,
			         GtkToggleButton *button)
{
	gboolean is_set_gconf = gconf_client_get_bool (gconf,
						       GNUMERIC_GCONF_SORT_DEFAULT_RETAIN_FORM,
						       NULL);
	gboolean is_set_button = gtk_toggle_button_get_active (button);
	if (is_set_gconf != is_set_button)
		gtk_toggle_button_set_active (button, is_set_gconf);
}

static void
cb_pref_sort_retain_formats_toggled(GtkToggleButton *button, PrefState *state)
{
		gconf_client_set_bool (state->gconf,
				       GNUMERIC_GCONF_SORT_DEFAULT_RETAIN_FORM,
				       gtk_toggle_button_get_active (button),
				       NULL);
}

static void
cb_pref_sort_set_case (GConfClient *gconf,
		       G_GNUC_UNUSED guint cnxn_id,
		       G_GNUC_UNUSED GConfEntry *entry,
		       GtkToggleButton *button)
{
	gboolean is_set_gconf = gconf_client_get_bool (gconf,
						       GNUMERIC_GCONF_SORT_DEFAULT_BY_CASE,
						       NULL);
	gboolean is_set_button = gtk_toggle_button_get_active (button);
	if (is_set_gconf != is_set_button)
		gtk_toggle_button_set_active (button, is_set_gconf);
}

static void
cb_pref_sort_case_toggled(GtkToggleButton *button, PrefState *state)
{
		gconf_client_set_bool (state->gconf,
				       GNUMERIC_GCONF_SORT_DEFAULT_BY_CASE,
				       gtk_toggle_button_get_active (button),
				       NULL);
}

static void
cb_pref_sort_set_ascending (GConfClient *gconf,
			    G_GNUC_UNUSED guint cnxn_id,
			    G_GNUC_UNUSED GConfEntry *entry,
			    GtkToggleButton *button)
{
	gboolean is_set_gconf = gconf_client_get_bool (gconf,
						       GNUMERIC_GCONF_SORT_DEFAULT_ASCENDING,
						       NULL);
	gboolean is_set_button = gtk_toggle_button_get_active (button);
	if (is_set_gconf != is_set_button)
		gtk_toggle_button_set_active (button, is_set_gconf);
}

static void
cb_pref_sort_ascending_toggled(GtkToggleButton *button, PrefState *state)
{
		gconf_client_set_bool (state->gconf,
				       GNUMERIC_GCONF_SORT_DEFAULT_ASCENDING,
				       gtk_toggle_button_get_active (button),
				       NULL);
}

static void
cb_pref_sort_set_initial_clauses (GConfClient *gconf,
				  G_GNUC_UNUSED guint cnxn_id,
				  G_GNUC_UNUSED GConfEntry *entry,
			          GtkSpinButton *button)
{
	gint int_in_gconf = gconf_client_get_int (gconf,
						  GNUMERIC_GCONF_SORT_DIALOG_MAX_INITIAL,
						  NULL);
	gint int_in_button = gtk_spin_button_get_value_as_int (button);
	if (int_in_gconf != int_in_button)
		gtk_spin_button_set_value (button, (gdouble) int_in_gconf);
}

static void
cb_pref_sort_initial_clauses_changed (GtkSpinButton *button, PrefState *state)
{
	gconf_client_set_int (state->gconf,
			      GNUMERIC_GCONF_SORT_DIALOG_MAX_INITIAL,
			      gtk_spin_button_get_value_as_int (button),
			      NULL);
}

static GtkWidget *
pref_sort_page_initializer (PrefState *state,
			    G_GNUC_UNUSED gpointer data,
			    G_GNUC_UNUSED GtkNotebook *notebook,
			    G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (3, 2, FALSE);
	gint row = 0;

	/* Retain Formats check box */
	dialog_pref_create_checkbox (GNUMERIC_GCONF_SORT_DEFAULT_RETAIN_FORM,
				     "/schemas" GNUMERIC_GCONF_SORT_DEFAULT_RETAIN_FORM,
				     page, row++, state,
				     cb_pref_sort_set_retain_formats,
				     cb_pref_sort_retain_formats_toggled);
	/* Sort by Case check box */
	dialog_pref_create_checkbox (GNUMERIC_GCONF_SORT_DEFAULT_BY_CASE,
				     "/schemas" GNUMERIC_GCONF_SORT_DEFAULT_BY_CASE,
				     page, row++, state,
				     cb_pref_sort_set_case,
				     cb_pref_sort_case_toggled);
	/* Sort Ascending check box */
	dialog_pref_create_checkbox (GNUMERIC_GCONF_SORT_DEFAULT_ASCENDING,
				     "/schemas" GNUMERIC_GCONF_SORT_DEFAULT_ASCENDING,
				     page, row++, state,
				     cb_pref_sort_set_ascending,
				     cb_pref_sort_ascending_toggled);
	/* Max Intial Clauses Spin Button */
	dialog_pref_create_int_spin (GNUMERIC_GCONF_SORT_DIALOG_MAX_INITIAL,
				     "/schemas" GNUMERIC_GCONF_SORT_DIALOG_MAX_INITIAL,
				     page, row++, state,
				     10, 0, 50, 1,
				     cb_pref_sort_set_initial_clauses,
				     cb_pref_sort_initial_clauses_changed);

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     Window Preferences Page                                              */
/*******************************************************************************************/

static void
pref_window_page_open (PrefState *state,
		       G_GNUC_UNUSED gpointer data,
		       G_GNUC_UNUSED GtkNotebook *notebook,
		       G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description (state,
				      _("The items on this page customize the "
					"new default workbook."));
}

static void
cb_pref_window_set_zoom (GConfClient *gconf,
			 G_GNUC_UNUSED guint cnxn_id,
			 G_GNUC_UNUSED GConfEntry *entry,
			 GtkSpinButton *button)
{
	gnm_float float_in_gconf = gconf_client_get_float (gconf,
							    GNUMERIC_GCONF_GUI_ZOOM,
							    NULL);
	gnm_float float_in_button = gtk_spin_button_get_value (button);
	if (float_in_gconf != float_in_button)
		gtk_spin_button_set_value (button, (gdouble) float_in_gconf);
}


static void
cb_pref_window_zoom_changed (GtkSpinButton *button, PrefState *state)
{
	gconf_client_set_float (state->gconf,
				GNUMERIC_GCONF_GUI_ZOOM,
				gtk_spin_button_get_value (button),
				NULL);
}

static void
cb_pref_window_set_window_height (GConfClient *gconf,
				  G_GNUC_UNUSED guint cnxn_id,
				  G_GNUC_UNUSED GConfEntry *entry,
				GtkSpinButton *button)
{
	gnm_float float_in_gconf = gconf_client_get_float (gconf,
						  GNUMERIC_GCONF_GUI_WINDOW_Y,
						  NULL);
	gnm_float float_in_button = gtk_spin_button_get_value (button);
	if (float_in_gconf != float_in_button)
		gtk_spin_button_set_value (button, (gdouble) float_in_gconf);
}


static void
cb_pref_window_height_changed (GtkSpinButton *button, PrefState *state)
{
	gconf_client_set_float (state->gconf,
				GNUMERIC_GCONF_GUI_WINDOW_Y,
				gtk_spin_button_get_value (button),
				NULL);
}

static void
cb_pref_window_set_window_width (GConfClient *gconf,
				 G_GNUC_UNUSED guint cnxn_id,
				 G_GNUC_UNUSED GConfEntry *entry,
				GtkSpinButton *button)
{
	gnm_float float_in_gconf = gconf_client_get_float (gconf,
						  GNUMERIC_GCONF_GUI_WINDOW_X,
						  NULL);
	gnm_float float_in_button = gtk_spin_button_get_value (button);
	if (float_in_gconf != float_in_button)
		gtk_spin_button_set_value (button, (gdouble) float_in_gconf);
}


static void
cb_pref_window_width_changed (GtkSpinButton *button, PrefState *state)
{
	gconf_client_set_float (state->gconf,
				GNUMERIC_GCONF_GUI_WINDOW_X,
				gtk_spin_button_get_value (button),
				NULL);
}

static void
cb_pref_window_set_sheet_num (GConfClient *gconf,
			      G_GNUC_UNUSED guint cnxn_id,
			      G_GNUC_UNUSED GConfEntry *entry,
			      GtkSpinButton *button)
{
	gint int_in_gconf = gconf_client_get_int (gconf,
						  GNUMERIC_GCONF_WORKBOOK_NSHEETS,
						  NULL);
	gint int_in_button = gtk_spin_button_get_value_as_int (button);
	if (int_in_gconf != int_in_button)
		gtk_spin_button_set_value (button, (gdouble) int_in_gconf);
}

static void
cb_pref_window_set_transition_keys (GConfClient *gconf,
				    G_GNUC_UNUSED guint cnxn_id,
				    G_GNUC_UNUSED GConfEntry *entry,
				    GtkToggleButton *button)
{
	gboolean is_set_gconf = gconf_client_get_bool (gconf,
						       GNUMERIC_GCONF_GUI_ED_TRANSITION_KEYS,
						       NULL);
	gboolean is_set_button = gtk_toggle_button_get_active (button);
	if (is_set_gconf != is_set_button)
		gtk_toggle_button_set_active (button, is_set_gconf);
}

static void
cb_pref_window_transition_keys_toggled (GtkToggleButton *button, PrefState *state)
{
	gconf_client_set_bool (state->gconf,
			       GNUMERIC_GCONF_GUI_ED_TRANSITION_KEYS,
			       gtk_toggle_button_get_active (button),
			       NULL);
	application_set_transition_keys(gtk_toggle_button_get_active (button));
}


static void
cb_pref_window_sheet_num_changed (GtkSpinButton *button, PrefState *state)
{
	gconf_client_set_int (state->gconf,
			      GNUMERIC_GCONF_WORKBOOK_NSHEETS,
			      gtk_spin_button_get_value_as_int (button),
			      NULL);
}

static void
cb_pref_window_set_live_scrolling (GConfClient *gconf,
				   G_GNUC_UNUSED guint cnxn_id,
				   G_GNUC_UNUSED GConfEntry *entry,
				   GtkToggleButton *button)
{
	gboolean is_set_gconf = gconf_client_get_bool (gconf,
						       GNUMERIC_GCONF_GUI_ED_LIVESCROLLING,
						       NULL);
	gboolean is_set_button = gtk_toggle_button_get_active (button);
	if (is_set_gconf != is_set_button)
		gtk_toggle_button_set_active (button, is_set_gconf);
}

static void
cb_pref_window_live_scrolling_toggled (GtkToggleButton *button, PrefState *state)
{
	gconf_client_set_bool (state->gconf,
			       GNUMERIC_GCONF_GUI_ED_LIVESCROLLING,
			       gtk_toggle_button_get_active (button),
			       NULL);
}


static GtkWidget *
pref_window_page_initializer (PrefState *state,
			      G_GNUC_UNUSED gpointer data,
			      G_GNUC_UNUSED GtkNotebook *notebook,
			      G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (4, 2, FALSE);
	gint row = 0;

	/* Transition keys check box */
	dialog_pref_create_checkbox (GNUMERIC_GCONF_GUI_ED_TRANSITION_KEYS,
				     "/schemas" GNUMERIC_GCONF_GUI_ED_TRANSITION_KEYS,
				     page, row++, state,
				     cb_pref_window_set_transition_keys,
				     cb_pref_window_transition_keys_toggled);

	/* Window Height Spin Button */
	dialog_pref_create_float_spin (GNUMERIC_GCONF_GUI_WINDOW_Y,
				       "/schemas" GNUMERIC_GCONF_GUI_WINDOW_Y,
				       page, row++, state,
				       0.75, 0.25, 1, 0.05, 2,
				       cb_pref_window_set_window_height,
				       cb_pref_window_height_changed);

	/* Window Width Spin Button */
	dialog_pref_create_float_spin (GNUMERIC_GCONF_GUI_WINDOW_X,
				       "/schemas" GNUMERIC_GCONF_GUI_WINDOW_X,
				       page, row++, state,
				       0.75, 0.25, 1, 0.05, 2,
				       cb_pref_window_set_window_width,
				       cb_pref_window_width_changed);

	/* Zoom Factor Spin Button */
	dialog_pref_create_float_spin (GNUMERIC_GCONF_GUI_ZOOM,
				       "/schemas" GNUMERIC_GCONF_GUI_ZOOM,
				       page, row++, state,
				       1.00, 0.10, 5.00, 0.05, 2,
				       cb_pref_window_set_zoom,
				       cb_pref_window_zoom_changed);

	/* Sheet Num Spin Button */
	dialog_pref_create_int_spin (GNUMERIC_GCONF_WORKBOOK_NSHEETS,
				     "/schemas" GNUMERIC_GCONF_WORKBOOK_NSHEETS,
				     page, row++, state,
				     1, 1, 100, 1,
				     cb_pref_window_set_sheet_num,
				     cb_pref_window_sheet_num_changed);

	/* Live Scrolling check box */
	dialog_pref_create_checkbox (GNUMERIC_GCONF_GUI_ED_LIVESCROLLING,
				     "/schemas" GNUMERIC_GCONF_GUI_ED_LIVESCROLLING,
				     page, row++, state,
				     cb_pref_window_set_live_scrolling,
				     cb_pref_window_live_scrolling_toggled);

	gtk_widget_show_all (page);
	return page;
}

/*******************************************************************************************/
/*                     File/XML Preferences Page                                           */
/*******************************************************************************************/

static void
pref_file_page_open (PrefState *state, G_GNUC_UNUSED gpointer data,
		     G_GNUC_UNUSED GtkNotebook *notebook,
		     G_GNUC_UNUSED gint page_num)
{
	dialog_pref_load_description (state,
				      _("The items on this page are related to the saving "
					"and opening of files."));
}


static void
cb_pref_file_set_file_history_num (GConfClient *gconf,
				   G_GNUC_UNUSED guint cnxn_id,
				   G_GNUC_UNUSED GConfEntry *entry,
			     GtkSpinButton *button)
{
	gint int_in_gconf = gconf_client_get_int (gconf,
						  GNUMERIC_GCONF_FILE_HISTORY_N,
						  NULL);
	gint int_in_button = gtk_spin_button_get_value_as_int (button);
	if (int_in_gconf != int_in_button)
		gtk_spin_button_set_value (button, (gdouble) int_in_gconf);
}

static void
cb_pref_file_file_history_changed (GtkSpinButton *button, PrefState *state)
{
		gconf_client_set_int (state->gconf,
				      GNUMERIC_GCONF_FILE_HISTORY_N,
				      gtk_spin_button_get_value_as_int (button),
				      NULL);
}

static void
cb_pref_file_set_xml_compression (GConfClient *gconf,
				  G_GNUC_UNUSED guint cnxn_id,
				  G_GNUC_UNUSED GConfEntry *entry,
			          GtkSpinButton *button)
{
	gint int_in_gconf = gconf_client_get_int (gconf,
						  GNUMERIC_GCONF_XML_COMPRESSION,
						  NULL);
	gint int_in_button = gtk_spin_button_get_value_as_int (button);
	if (int_in_gconf != int_in_button)
		gtk_spin_button_set_value (button, (gdouble) int_in_gconf);
}

static void
cb_pref_file_xml_compression_changed (GtkSpinButton *button, PrefState *state)
{
		gconf_client_set_int (state->gconf,
				       GNUMERIC_GCONF_XML_COMPRESSION,
				       gtk_spin_button_get_value_as_int (button),
				       NULL);
}

static void
cb_pref_file_set_overwrite (GConfClient *gconf,
			    G_GNUC_UNUSED guint cnxn_id,
			    G_GNUC_UNUSED GConfEntry *entry,
			    GtkToggleButton *button)
{
	gboolean is_set_gconf = gconf_client_get_bool (gconf,
						       GNUMERIC_GCONF_FILE_OVERWRITE_DEFAULT,
						       NULL);
	gboolean is_set_button = gtk_toggle_button_get_active (button);
	if (is_set_gconf != is_set_button)
		gtk_toggle_button_set_active (button, is_set_gconf);
}

static void
cb_pref_file_overwrite_toggled (GtkToggleButton *button, PrefState *state)
{
		gconf_client_set_bool (state->gconf,
				       GNUMERIC_GCONF_FILE_OVERWRITE_DEFAULT,
				       gtk_toggle_button_get_active (button),
				       NULL);
}

static void
cb_pref_file_set_single_sheet_warn (GConfClient *gconf,
				    G_GNUC_UNUSED guint cnxn_id,
				    G_GNUC_UNUSED GConfEntry *entry,
				    GtkToggleButton *button)
{
	gboolean is_set_gconf = gconf_client_get_bool (gconf,
						       GNUMERIC_GCONF_FILE_SINGLE_SHEET_SAVE,
						       NULL);
	gboolean is_set_button = gtk_toggle_button_get_active (button);
	if (is_set_gconf != is_set_button)
		gtk_toggle_button_set_active (button, is_set_gconf);
}

static void
cb_pref_file_single_sheet_warn_toggled (GtkToggleButton *button, PrefState *state)
{
		gconf_client_set_bool (state->gconf,
				       GNUMERIC_GCONF_FILE_SINGLE_SHEET_SAVE,
				       gtk_toggle_button_get_active (button),
				       NULL);
}

static void
cb_pref_latex_set_use_utf8 (GConfClient *gconf,
			    G_GNUC_UNUSED guint cnxn_id,
			    G_GNUC_UNUSED GConfEntry *entry,
			    GtkToggleButton *button)
{
	gboolean is_set_gconf = gconf_client_get_bool (gconf,
						       PLUGIN_GCONF_LATEX_USE_UTF8,
						       NULL);
	gboolean is_set_button = gtk_toggle_button_get_active (button);
	if (is_set_gconf != is_set_button)
		gtk_toggle_button_set_active (button, is_set_gconf);
}

static void
cb_pref_latex_set_use_utf8_toggled (GtkToggleButton *button, PrefState *state)
{
	gnm_gconf_set_latex_use_utf8 (gtk_toggle_button_get_active (button));
}

static GtkWidget *
pref_file_page_initializer (PrefState *state,
			    G_GNUC_UNUSED gpointer data,
			    G_GNUC_UNUSED GtkNotebook *notebook,
			    G_GNUC_UNUSED gint page_num)
{
	GtkWidget *page = gtk_table_new (2, 2, FALSE);
	gint row = 0;

	/* File History Number Spin Button */
	dialog_pref_create_int_spin (GNUMERIC_GCONF_FILE_HISTORY_N,
				     "/schemas" GNUMERIC_GCONF_FILE_HISTORY_N,
				     page, row++, state,
				     4, 0, 40, 1,
				     cb_pref_file_set_file_history_num,
				     cb_pref_file_file_history_changed);

	/* XML Compression Spin Button */
	dialog_pref_create_int_spin (GNUMERIC_GCONF_XML_COMPRESSION,
				     "/schemas" GNUMERIC_GCONF_XML_COMPRESSION,
				     page, row++, state,
				     9, 0, 9, 1,
				     cb_pref_file_set_xml_compression,
				     cb_pref_file_xml_compression_changed);

	/* Overwrite Default check box */
	dialog_pref_create_checkbox (GNUMERIC_GCONF_FILE_OVERWRITE_DEFAULT,
				     "/schemas" GNUMERIC_GCONF_FILE_OVERWRITE_DEFAULT,
				     page, row++, state,
				     cb_pref_file_set_overwrite,
				     cb_pref_file_overwrite_toggled);

	/* Single Sheet Warning check box */
	dialog_pref_create_checkbox (GNUMERIC_GCONF_FILE_SINGLE_SHEET_SAVE,
				     "/schemas" GNUMERIC_GCONF_FILE_SINGLE_SHEET_SAVE,
				     page, row++, state,
				     cb_pref_file_set_single_sheet_warn,
				     cb_pref_file_single_sheet_warn_toggled);

	/* LaTeX Exporter uses UTF-8 check box */
	dialog_pref_create_checkbox (PLUGIN_GCONF_LATEX_USE_UTF8,
				     "/schemas" PLUGIN_GCONF_LATEX_USE_UTF8,
				     page, row++, state,
				     cb_pref_latex_set_use_utf8,
				     cb_pref_latex_set_use_utf8_toggled);

	gtk_widget_show_all (page);
	return page;
}


/*******************************************************************************************/
/*               General Preference Dialog Routines                                        */
/*******************************************************************************************/

static page_info_t page_info[] = {
	{N_("Font"),       GTK_STOCK_ITALIC,	         NULL, pref_font_initializer, pref_font_page_open, NULL},
	{N_("Windows"),    "Gnumeric_ObjectCombo",	 NULL, pref_window_page_initializer, pref_window_page_open, NULL},
	{N_("Files"),      GTK_STOCK_FLOPPY,	         NULL, pref_file_page_initializer, pref_file_page_open, NULL},
	{N_("Undo"),       GTK_STOCK_UNDO,		 NULL, pref_undo_page_initializer, pref_undo_page_open, NULL},
	{N_("Sorting"),    GTK_STOCK_SORT_ASCENDING,     NULL, pref_sort_page_initializer, pref_sort_page_open, NULL},
	{N_("Various"),    GTK_STOCK_PREFERENCES,        NULL, pref_tree_initializer, pref_tree_page_open, pref_tree_data},
	{N_("Internal"),   GTK_STOCK_DIALOG_ERROR,       "5",  pref_tree_initializer, pref_tree_page_open, pref_tree_data_danger},
	{NULL, NULL, NULL, NULL, NULL, NULL},
};

static void
dialog_pref_select_page (PrefState *state, char const *page)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->view);
	GtkTreeIter iter;
	GtkTreePath *path;

	path = gtk_tree_path_new_from_string  (page);
	
	if (path != NULL) {
		gtk_tree_selection_select_path (selection, path);
		gtk_tree_path_free (path);
	} else if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (state->store),
						  &iter)) {
		gtk_tree_selection_select_iter (selection, &iter);
	}
}

static void
cb_dialog_pref_selection_changed (GtkTreeSelection *selection,
				  PrefState *state)
{
	GtkTreeIter iter;
	int page;

	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->store), &iter,
				    PAGE_NUMBER, &page,
				    -1);
		gtk_notebook_set_current_page (GTK_NOTEBOOK(state->notebook),
					       page);
	} else {
		dialog_pref_select_page (state, "0");
	}
}

static gboolean
cb_preferences_destroy (G_GNUC_UNUSED GtkWidget *widget,
			PrefState *state)
{
	if (state->gconf)
		gconf_client_suggest_sync (state->gconf, NULL);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;

	g_free (state);

	application_set_pref_dialog (NULL);

	return FALSE;
}

static void
cb_close_clicked (G_GNUC_UNUSED GtkWidget *ignore, PrefState *state)
{
	    gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
cb_dialog_pref_switch_page  (GtkNotebook *notebook,
			     G_GNUC_UNUSED GtkNotebookPage *page,
			     gint page_num, PrefState *state)
{
	if (page_info[page_num].page_open)
		page_info[page_num].page_open (state, page_info[page_num].data,
					       notebook, page_num);
	else
		dialog_pref_page_open (state);
}

/* Note: The first page listed below is opened through File/Preferences, */
/*       and the second through Format/Workbook */
static char const *startup_pages[] = {"1", "0"};

void
dialog_preferences (WorkbookControlGUI *wbcg, gint page)
{
	PrefState *state;
	GladeXML *gui;
	GtkWidget *w;
	gint i;
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;

	w = application_get_pref_dialog ();
	if (w) {
		gtk_widget_show (w);
		gdk_window_raise (w->window);
		return;
	}

	gui = gnm_glade_xml_new (COMMAND_CONTEXT (wbcg),
		"preferences.glade", NULL, NULL);
	if (gui == NULL)
		return;

	state = g_new0 (PrefState, 1);
	state->gui = gui;
	state->dialog     = glade_xml_get_widget (gui, "preferences");
	state->notebook   = glade_xml_get_widget (gui, "notebook");
	state->pages      = NULL;
	state->gconf      = application_get_gconf_client ();
	state->description = GTK_TEXT_VIEW (glade_xml_get_widget (gui, "description"));
	state->wb	  = wb_control_workbook (WORKBOOK_CONTROL (wbcg));

	state->view = GTK_TREE_VIEW(glade_xml_get_widget (gui, "itemlist"));
	state->store = gtk_tree_store_new (NUM_COLUMNS,
					   GDK_TYPE_PIXBUF,
					   G_TYPE_STRING,
					   G_TYPE_INT);
	gtk_tree_view_set_model (state->view, GTK_TREE_MODEL(state->store));
	selection = gtk_tree_view_get_selection (state->view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	column = gtk_tree_view_column_new_with_attributes ("",
							   gtk_cell_renderer_pixbuf_new (),
							   "pixbuf", ITEM_ICON,
							   NULL);
	gtk_tree_view_append_column (state->view, column);
	column = gtk_tree_view_column_new_with_attributes ("",
							   gtk_cell_renderer_text_new (),
							   "text", ITEM_NAME,
							   NULL);
	gtk_tree_view_append_column (state->view, column);
	gtk_tree_view_set_expander_column (state->view, column);

	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (cb_dialog_pref_selection_changed), state);
	
	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "close_button")),
		"clicked",
		G_CALLBACK (cb_close_clicked), state);

	g_signal_connect (G_OBJECT (state->notebook),
		"switch-page",
		G_CALLBACK (cb_dialog_pref_switch_page), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_PREFERENCES);

	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (cb_preferences_destroy), state);

	application_set_pref_dialog (state->dialog);

	for (i = 0; page_info[i].page_initializer; i++) {
		page_info_t *this_page =  &page_info[i];
		GtkWidget *page = this_page->page_initializer (state, this_page->data,
							       GTK_NOTEBOOK (state->notebook), i);
		GtkWidget *label = NULL;

		state->pages = g_slist_append (state->pages, page);

		if (this_page->icon_name)
			label = gtk_image_new_from_stock (this_page->icon_name,
							  GTK_ICON_SIZE_BUTTON);
		else if (this_page->page_name)
			label = gtk_label_new (this_page->page_name);
		gtk_notebook_append_page (GTK_NOTEBOOK (state->notebook), page, label);
		dialog_pref_add_item (state, this_page->page_name, this_page->icon_name, i, this_page->parent_path);
	}

	if (page != 0 && page != 1) {
		g_warning ("Selected page is %i but should be 0 or 1", page);
		page = 0;
	}

	wbcg_set_transient (wbcg, GTK_WINDOW (state->dialog));
	gtk_widget_show (GTK_WIDGET (state->dialog));

	dialog_pref_select_page (state, startup_pages[page]);
}
