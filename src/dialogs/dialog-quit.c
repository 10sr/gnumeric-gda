/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-quit.c:
 *   Dialog for quit (selecting what to save)
 *
 * Author:
 *   Morten Welinder (terra@gnome.org)
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
#include "dialogs.h"
#include <gui-file.h>
#include <gui-util.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <gui-clipboard.h>
#include <application.h>
#include <dead-kittens.h>

#include <goffice/goffice.h>
#include <gtk/gtk.h>

#include <glib/gi18n-lib.h>

#include <string.h>

enum {
	RESPONSE_ALL = 1,
	RESPONSE_NONE = 2
};

enum {
	QUIT_COL_CHECK,
	QUIT_COL_DOC
};

/* ------------------------------------------------------------------------- */

static void
url_renderer_func (GtkTreeViewColumn *tree_column,
		   GtkCellRenderer   *cell,
		   GtkTreeModel      *model,
		   GtkTreeIter       *iter,
		   gpointer           user_data)
{
	GODoc *doc = NULL;
	const char *uri;
	char *markup, *shortname, *filename, *longname, *duri;

	gtk_tree_model_get (model, iter,
			    QUIT_COL_DOC, &doc,
			    -1);
	g_return_if_fail (GO_IS_DOC (doc));

	uri = go_doc_get_uri (doc);
	filename = go_filename_from_uri (uri);
	if (filename) {
		shortname = g_filename_display_basename (filename);
	} else {
		shortname = g_filename_display_basename (uri);
	}

	duri = go_url_decode (uri);
	longname = duri
		? g_filename_display_name (duri)
		: g_strdup (uri);

	markup = g_markup_printf_escaped (_("<b>%s</b>\n"
					    "<small>Location: %s</small>"),
					  shortname,
					  longname);
	g_object_set (cell, "markup", markup, NULL);
	g_free (markup);
	g_free (shortname);
	g_free (longname);
	g_free (duri);
	g_free (filename);
}

static gboolean
foreach_is_file_set (GtkTreeModel *model, GtkTreePath *path,
		     GtkTreeIter *iter, gboolean *data)
{
	gboolean value;

	gtk_tree_model_get (GTK_TREE_MODEL (model), iter,
			    QUIT_COL_CHECK, &value, -1);

	*data = value;

	return value;
}

static gboolean
files_set (GtkTreeModel *model)
{
	gboolean files_set_state = FALSE;

	gtk_tree_model_foreach (GTK_TREE_MODEL (model),
				(GtkTreeModelForeachFunc) foreach_is_file_set,
				&files_set_state);

	return files_set_state;
}

static void
cb_toggled_save (GtkCellRendererToggle *cell,
		 gchar                 *path_string,
		 gpointer               data)
{
	GtkTreeModel *model = GTK_TREE_MODEL (data);
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);

	if (gtk_tree_model_get_iter (model, &iter, path)) {
		gboolean value;
		gtk_tree_model_get (model, &iter,
				    QUIT_COL_CHECK, &value, -1);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    QUIT_COL_CHECK, !value, -1);
	} else {
		g_warning ("Did not get a valid iterator");
	}

	gtk_tree_path_free (path);
}

static void
set_all (GtkTreeModel *model, gboolean value)
{
	GtkTreeIter iter;
	gboolean ok = gtk_tree_model_get_iter_first (model, &iter);

	while (ok) {
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    QUIT_COL_CHECK, value, -1);
		ok = gtk_tree_model_iter_next (model, &iter);
	}
}

static void
cb_select_all (G_GNUC_UNUSED GtkWidget *button,
	       GtkTreeModel *model)
{
	set_all (model, TRUE);
}

static void
cb_clear_all (G_GNUC_UNUSED GtkWidget *button,
	      GtkTreeModel *model)
{
	set_all (model, FALSE);
}

static void
cb_list_row_changed_save_sensitivity (GtkListStore *list, GtkTreePath *path_string,
				      GtkTreeIter *iter, GtkWidget *widget)
{
	GtkTreeModel *model = GTK_TREE_MODEL (list);

	if (files_set (model) == TRUE)
		gtk_widget_set_sensitive (GTK_WIDGET (widget), TRUE);
	else
		gtk_widget_set_sensitive (GTK_WIDGET (widget), FALSE);
}

static void
cb_list_row_changed_discard_sensitivity (GtkListStore *list,
					 GtkTreePath *path_string,
					 GtkTreeIter *iter,
					 GtkWidget *widget)
{
	GtkTreeModel *model = GTK_TREE_MODEL (list);

	if (files_set (model) == TRUE)
		gtk_widget_set_sensitive (GTK_WIDGET (widget), FALSE);
	else
		gtk_widget_set_sensitive (GTK_WIDGET (widget), TRUE);
}

static gboolean
show_quit_dialog (GList *dirty, WBCGtk *wbcg)
{
	GtkDialog *dialog;
	GtkTreeView *tree;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GtkWidget *scrollw, *button;
	GtkListStore *list;
	int res;
	GList *l;
	GtkTreeIter iter;
	gboolean ok;
	GtkTreeModel *model;
	gboolean quit;
	gboolean multiple = (dirty->next != NULL);

	list = gtk_list_store_new (2,
				   G_TYPE_BOOLEAN,
				   G_TYPE_POINTER);

	dialog = (GtkDialog *)gtk_dialog_new_with_buttons
		(_("Some Documents have not Been Saved"),
		 wbcg_toplevel (wbcg), 0,
		 NULL);

	button = go_gtk_dialog_add_button (GTK_DIALOG (dialog),
					   _("_Discard All"),
					   GTK_STOCK_DELETE,
					   GTK_RESPONSE_NO);
	go_widget_set_tooltip_text (GTK_WIDGET (button), _("Discard changes in all files"));

	if (multiple)
		g_signal_connect (G_OBJECT (list),
				  "row-changed",
				  G_CALLBACK (cb_list_row_changed_discard_sensitivity),
				  GTK_WIDGET (button));

	button = go_gtk_dialog_add_button (GTK_DIALOG (dialog),
					   _("Don't Quit"),
					   GTK_STOCK_CANCEL,
					   GTK_RESPONSE_CANCEL);
	go_widget_set_tooltip_text (button, _("Resume editing"));

	if (multiple) {
		button = go_gtk_dialog_add_button (GTK_DIALOG (dialog),
						   _("_Save Selected"),
						   GTK_STOCK_SAVE,
						   GTK_RESPONSE_OK);
		go_widget_set_tooltip_text (GTK_WIDGET (button),
					    _("Save selected documents and then quit"));

		g_signal_connect (G_OBJECT (list),
				  "row-changed",
				  G_CALLBACK (cb_list_row_changed_save_sensitivity),
				  GTK_WIDGET (button));
	} else {
		button = go_gtk_dialog_add_button (GTK_DIALOG (dialog),
						   _("Save"),
						   GTK_STOCK_SAVE,
						   GTK_RESPONSE_OK);
		go_widget_set_tooltip_text (button, _("Save document"));
	}

	scrollw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrollw),
					     GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrollw),
					GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (dialog)),
			    scrollw, TRUE, TRUE, 0);

	if (multiple) {
		GtkWidget *hbox = gtk_hbutton_box_new ();
		gtk_button_box_set_layout (GTK_BUTTON_BOX (hbox), GTK_BUTTONBOX_END);
		gtk_box_set_spacing (GTK_BOX (hbox), 5);

		button = go_gtk_button_new_with_stock (_("Select _All"),
						       GTK_STOCK_SELECT_ALL);
		go_widget_set_tooltip_text (GTK_WIDGET (button),
					    _("Select all documents for saving"));

		g_signal_connect (G_OBJECT (button), "clicked",
				  G_CALLBACK (cb_select_all),
				  list);

		gtk_box_pack_end (GTK_BOX (hbox),
				    GTK_WIDGET (button), FALSE, TRUE, 0);

		button = go_gtk_button_new_with_stock (_("_Clear Selection"),
						       GTK_STOCK_CLEAR);
		go_widget_set_tooltip_text (GTK_WIDGET(button),
					    _("Unselect all documents for saving"));

		g_signal_connect (G_OBJECT (button), "clicked",
				  G_CALLBACK (cb_clear_all),
				  list);

		gtk_box_pack_end (GTK_BOX (hbox),
				  GTK_WIDGET (button), FALSE, TRUE, 0);

		gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (dialog)),
				    GTK_WIDGET (hbox), FALSE, FALSE, 0);
	}

	gtk_dialog_set_default_response (dialog, GTK_RESPONSE_OK);

	tree = (GtkTreeView *)gtk_tree_view_new ();
	gtk_tree_view_set_enable_search (tree, FALSE);
	gtk_container_add (GTK_CONTAINER (scrollw), GTK_WIDGET (tree));
	gtk_tree_view_set_model (tree, GTK_TREE_MODEL (list));

	if (multiple) {
		renderer = gtk_cell_renderer_toggle_new ();
		g_signal_connect (G_OBJECT (renderer),
				  "toggled",
				  G_CALLBACK (cb_toggled_save), list);
		column = gtk_tree_view_column_new_with_attributes
			(_("Save?"),
			 renderer,
			 "active", QUIT_COL_CHECK,
			 NULL);
		gtk_tree_view_append_column (tree, column);
	}

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (G_OBJECT (renderer),
		      "ellipsize", PANGO_ELLIPSIZE_END,
		      NULL);
	column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (column, _("Document"));
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (column,
						 renderer,
						 url_renderer_func,
						 NULL,
						 NULL);

	gtk_tree_view_append_column (tree, column);

	/* ---------------------------------------- */
	/* Size the dialog.  */

	{
		GtkWidget *w = GTK_WIDGET (wbcg_toplevel (wbcg));
		int width, height, vsep;
		PangoLayout *layout =
			gtk_widget_create_pango_layout (w, "Mg19");

		gtk_widget_style_get (GTK_WIDGET (tree),
				      "vertical_separator", &vsep,
				      NULL);

		pango_layout_get_pixel_size (layout, &width, &height);
		gtk_widget_set_size_request (GTK_WIDGET (tree),
					     width * 60 / 4,
					     (2 * height + vsep) * (4 + 1));
		g_object_unref (layout);
	}

	/* ---------------------------------------- */
	/* Populate the model.  */

	for (l = dirty; l; l = l->next) {
		GODoc *doc = l->data;
		GtkTreeIter iter;

		gtk_list_store_append (list, &iter);
		gtk_list_store_set (list,
				    &iter,
				    QUIT_COL_CHECK, TRUE,
				    QUIT_COL_DOC, doc,
				    -1);
	}

	/* ---------------------------------------- */

	atk_object_set_role (gtk_widget_get_accessible (GTK_WIDGET (dialog)),
			     ATK_ROLE_ALERT);

	gtk_widget_show_all (GTK_WIDGET (dialog));
	res = go_gtk_dialog_run (dialog, wbcg_toplevel (wbcg));
	switch (res) {
	case GTK_RESPONSE_CANCEL:
	case GTK_RESPONSE_DELETE_EVENT:
		quit = FALSE;
		break;

	case GTK_RESPONSE_NO:
		quit = TRUE;
		break;

	default:
		model = GTK_TREE_MODEL (list);
		ok = gtk_tree_model_get_iter_first (model, &iter);

		g_return_val_if_fail (ok, FALSE);
		quit = TRUE;
		do {
			gboolean save = TRUE;
			GODoc *doc = NULL;

			gtk_tree_model_get (model, &iter,
					    QUIT_COL_CHECK, &save,
					    QUIT_COL_DOC, &doc,
					    -1);
			if (save) {
				gboolean ok;
				Workbook *wb = WORKBOOK (doc);
				WBCGtk *wbcg2 = wbcg_find_for_workbook (wb, wbcg, NULL, NULL);

				ok = wbcg2 && gui_file_save (wbcg2, wb_control_view (WORKBOOK_CONTROL (wbcg2)));
				if (!ok)
					quit = FALSE;
			}

			ok = gtk_tree_model_iter_next (model, &iter);
		} while (ok);
		break;
	}

	g_object_unref (list);
	return quit;
}

/* ------------------------------------------------------------------------- */

static gint
doc_order (gconstpointer a_, gconstpointer b_)
{
	GODoc *a = (GODoc *)a_;
	GODoc *b = (GODoc *)b_;

	/* Primitive, but will work for now.  */
	return go_str_compare (go_doc_get_uri (a), go_doc_get_uri (b));
}

void
dialog_quit (WBCGtk *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GList *l, *dirty = NULL;
	gboolean quit;

	for (l = gnm_app_workbook_list (); l; l = l->next) {
		GODoc *doc = l->data;
		if (go_doc_is_dirty (GO_DOC (doc)))
			dirty = g_list_prepend (dirty, doc);
	}

	if (dirty) {
		dirty = g_list_sort (dirty, doc_order);
		quit = show_quit_dialog (dirty, wbcg);
		g_list_free (dirty);
		if (!quit)
			return;
	}

	gnm_x_store_clipboard_if_needed (wb_control_get_workbook (wbc));

	l = g_list_copy (gnm_app_workbook_list ());
	while (l) {
		Workbook *wb = l->data;
		l = g_list_remove (l, wb);
		go_doc_set_dirty (GO_DOC (wb), FALSE);

		/* This is how we kill it?  Ugh!  */
		g_object_unref (wb);
	}
}

/* ------------------------------------------------------------------------- */
