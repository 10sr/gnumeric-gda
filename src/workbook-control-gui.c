/* vim: set sw=8: */
/*
 * workbook-control-gui.c: GUI specific routines for a workbook-control.
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <config.h>
#include "workbook-control-gui-priv.h"
#include "application.h"
#include "workbook-object-toolbar.h"
#include "workbook-format-toolbar.h"
#include "workbook-view.h"
#include "workbook-edit.h"
#include "workbook.h"
#include "sheet.h"
#include "sheet-control-gui.h"
#include "sheet-object.h"
#include "rendered-value.h"
#include "dialogs.h"
#include "commands.h"
#include "cmd-edit.h"
#include "workbook-cmd-format.h"
#include "selection.h"
#include "clipboard.h"
#include "print.h"
#include "gui-clipboard.h"
#include "workbook-edit.h"
#include "main.h"
#include "eval.h"
#include "position.h"
#include "parse-util.h"
#include "ranges.h"
#include "history.h"
#include "str.h"

#ifdef ENABLE_BONOBO
#include "sheet-object-container.h"
#endif
#include "gnumeric-type-util.h"
#include "gnumeric-util.h"
#include "widgets/gnumeric-toolbar.h"

#include "widgets/widget-editable-label.h"
#include <gal/widgets/gtk-combo-text.h>
#include <gal/widgets/gtk-combo-stack.h>
#include <ctype.h>

#include "pixmaps/equal-sign.xpm"

GtkWindow *
wb_control_gui_toplevel (WorkbookControlGUI *wbcg)
{
	return wbcg->toplevel;
}

/**
 * wb_control_gui_focus_cur_sheet :
 * @wbcg : The workbook control to operate on.
 *
 * A utility routine to safely ensure that the keyboard focus
 * is attached to the item-grid.  This is required when a user
 * edits a combo-box or and entry-line which grab focus.
 *
 * It is called for zoom, font name/size, and accept/cancel for the editline.
 */
Sheet *
wb_control_gui_focus_cur_sheet (WorkbookControlGUI *wbcg)
{
	SheetControlGUI *sheet_view;

	g_return_val_if_fail (wbcg != NULL, NULL);

	sheet_view = SHEET_CONTROL_GUI (GTK_NOTEBOOK (wbcg->notebook)->cur_page->child);

	g_return_val_if_fail (sheet_view != NULL, NULL);

	gtk_window_set_focus (sheet_view->wbcg->toplevel,
			      sheet_view->canvas);

	return sheet_view->sheet;
}

/****************************************************************************/
/* Autosave */
static gboolean
cb_autosave (gpointer *data)
{
	WorkbookView *wb_view;
        WorkbookControlGUI *wbcg = (WorkbookControlGUI *)data;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL (wbcg), FALSE);

	wb_view = wb_control_view (WORKBOOK_CONTROL (wbcg));

	if (wb_view == NULL)
		return FALSE;

	if (wbcg->autosave && workbook_is_dirty (wb_view_workbook (wb_view))) {
	        if (wbcg->autosave_prompt && !dialog_autosave_prompt (wbcg))
			return TRUE;
		workbook_save (wbcg, wb_view);
	}
	return TRUE;
}

void
wb_control_gui_autosave_cancel (WorkbookControlGUI *wbcg)
{
	if (wbcg->autosave_timer != 0) {
		gtk_timeout_remove (wbcg->autosave_timer);
		wbcg->autosave_timer = 0;
	}
}

void
wb_control_gui_autosave_set (WorkbookControlGUI *wbcg,
			     int minutes, gboolean prompt)
{
	wb_control_gui_autosave_cancel (wbcg);

	wbcg->autosave = (minutes != 0);
	wbcg->autosave_minutes = minutes;
	wbcg->autosave_prompt = prompt;

	if (wbcg->autosave)
		wbcg->autosave_timer = gtk_timeout_add (minutes * 60000,
			(GtkFunction) cb_autosave, wbcg);
}
/****************************************************************************/

static WorkbookControl *
wbcg_control_new (WorkbookControl *wbc, WorkbookView *wbv, Workbook *wb)
{
	return workbook_control_gui_new (wbv, wb);
}

static void
wbcg_title_set (WorkbookControl *wbc, char const *title)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	char *full_title;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (title != NULL);

	full_title = g_strconcat (title, _(" : Gnumeric"), NULL);

 	gtk_window_set_title (wbcg->toplevel, full_title);
	g_free (full_title);
}

static void
wbcg_size_pixels_set (WorkbookControl *wbc, int width, int height)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	int const screen_width = gdk_screen_width ();
	int const screen_height = gdk_screen_height ();

	/* FIXME : This should really be sizing the notebook */
	gtk_window_set_default_size (wbcg->toplevel,
				     MIN (screen_width - 64, width),
				     MIN (screen_height - 64, height));
}

static void
cb_prefs_update (gpointer key, gpointer value, gpointer user_data)
{
	Sheet *sheet = value;
	sheet_adjust_preferences (sheet);
}

static void
wbcg_prefs_update (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	Workbook	*wb  = wb_control_workbook (wbc);
	WorkbookView	*wbv = wb_control_view (wbc);

	g_hash_table_foreach (wb->sheet_hash_private, cb_prefs_update, NULL);
	gtk_notebook_set_show_tabs (wbcg->notebook,
				    wbv->show_notebook_tabs);
}

static void
wbcg_format_feedback (WorkbookControl *wbc)
{
	workbook_feedback_set ((WorkbookControlGUI *)wbc);
}

static void
zoom_changed (WorkbookControlGUI *wbcg, Sheet* sheet)
{
	gchar buffer [25];

	/* If the user did not initiate this action ignore it.
	 * This happens whenever the ui updates and the current cell makes a
	 * change to the toolbar indicators.
	 */
	if (wbcg->updating_ui)
		return;

	g_return_if_fail (wbcg->zoom_entry != NULL);
	g_return_if_fail (sheet != NULL);

	snprintf (buffer, sizeof (buffer), "%d%%",
		  (int) (sheet->last_zoom_factor_used * 100));
	gtk_combo_text_set_text (GTK_COMBO_TEXT (wbcg->zoom_entry), buffer);
}

static void
wbcg_zoom_feedback (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	Sheet *sheet = wb_control_cur_sheet (wbc);

	/* Do not update the zoom when we update the status display */
	g_return_if_fail (!wbcg->updating_ui);
	wbcg->updating_ui = TRUE;
	zoom_changed (wbcg, sheet);
	wbcg->updating_ui = FALSE;
}

static void
wbcg_edit_line_set (WorkbookControl *wbc, char const *text)
{
	GtkEntry *entry = workbook_get_entry ((WorkbookControlGUI*)wbc);
	gtk_entry_set_text (entry, text);
}

static void
wbcg_edit_selection_descr_set (WorkbookControl *wbc, char const *text)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	gtk_entry_set_text (GTK_ENTRY (wbcg->selection_descriptor), text);
}

/*
 * yield_focus
 * @widget   widget
 * @toplevel toplevel
 *
 * Give up focus if we have it. This is called when widget is destroyed. A
 * widget which no longer exists should not have focus!
 */
static gboolean
yield_focus (GtkWidget *widget, GtkWindow *toplevel)
{
	if (toplevel && (toplevel->focus_widget == widget))
		gtk_window_set_focus (toplevel, NULL);

	return FALSE;
}

/*
 * Signal handler for EditableLabel's text_changed signal.
 */
static gboolean
cb_sheet_label_changed (EditableLabel *el,
			const char *new_name, WorkbookControlGUI *wbcg)
{
	gboolean ans = !cmd_rename_sheet (WORKBOOK_CONTROL (wbcg),
					  el->text, new_name);
	wb_control_gui_focus_cur_sheet (wbcg);
	return ans;
}

static void
cb_sheet_label_edit_stopped (EditableLabel *el, WorkbookControlGUI *wbcg)
{
	wb_control_gui_focus_cur_sheet (wbcg);
}

static void
sheet_action_add_sheet (GtkWidget *widget, SheetControlGUI *sheet_view)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (sheet_view->wbcg);
	workbook_sheet_add (wb_control_workbook (wbc), sheet_view->sheet, TRUE);
}

static void
delete_sheet_if_possible (GtkWidget *ignored, SheetControlGUI *sheet_view)
{
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (sheet_view->wbcg));
	GtkWidget *d, *button_no;
	char *message;
	int r;

	/*
	 * If this is the last sheet left, ignore the request
	 */
	if (workbook_sheet_count (wb) == 1)
		return;

	message = g_strdup_printf (
		_("Are you sure you want to remove the sheet called `%s'?"),
		sheet_view->sheet->name_unquoted);

	d = gnome_message_box_new (
		message, GNOME_MESSAGE_BOX_QUESTION,
		GNOME_STOCK_BUTTON_YES,
		GNOME_STOCK_BUTTON_NO,
		NULL);
	g_free (message);
	button_no = g_list_last (GNOME_DIALOG (d)->buttons)->data;
	gtk_widget_grab_focus (button_no);

	r = gnumeric_dialog_run (sheet_view->wbcg, GNOME_DIALOG (d));

	if (r != 0)
		return;

	workbook_sheet_delete (sheet_view->sheet);
	workbook_recalc_all (wb);
}

static void
sheet_action_rename_sheet (GtkWidget *widget, SheetControlGUI *sheet_view)
{
	Sheet *sheet = sheet_view->sheet;
	char *new_name = dialog_get_sheet_name (sheet_view->wbcg, sheet->name_unquoted);
	if (!new_name)
		return;

	/* We do not care if it fails */
	cmd_rename_sheet (WORKBOOK_CONTROL (sheet_view->wbcg),
			  sheet->name_unquoted, new_name);
	g_free (new_name);
}

static void
sheet_action_clone_sheet (GtkWidget *widget, SheetControlGUI *sheet_view)
{
     	Sheet *new_sheet = sheet_duplicate (sheet_view->sheet);
	workbook_sheet_attach (sheet_view->sheet->workbook, new_sheet,
			       sheet_view->sheet);
	sheet_set_dirty (new_sheet, TRUE);
}

static void
sheet_action_reorder_sheet (GtkWidget *widget, SheetControlGUI *sheet_view)
{
	dialog_sheet_order (sheet_view->wbcg);
}

/**
 * sheet_menu_label_run:
 */
static void
sheet_menu_label_run (SheetControlGUI *sheet_view, GdkEventButton *event)
{
#define SHEET_CONTEXT_TEST_SIZE 1

	struct {
		const char *text;
		void (*function) (GtkWidget *widget, SheetControlGUI *sheet_view);
		int  flags;
	} const sheet_label_context_actions [] = {
		{ N_("Add another sheet"), &sheet_action_add_sheet, 0 },
		{ N_("Remove this sheet"), &delete_sheet_if_possible, SHEET_CONTEXT_TEST_SIZE },
		{ N_("Rename this sheet"), &sheet_action_rename_sheet, 0 },
		{ N_("Duplicate this sheet"), &sheet_action_clone_sheet, 0 },
		{ N_("Re-order sheets"), &sheet_action_reorder_sheet, SHEET_CONTEXT_TEST_SIZE },
		{ NULL, NULL }
	};

	GtkWidget *menu;
	GtkWidget *item;
	int i;

	menu = gtk_menu_new ();

	for (i = 0; sheet_label_context_actions [i].text != NULL; i++){
		int flags = sheet_label_context_actions [i].flags;

		if (flags & SHEET_CONTEXT_TEST_SIZE &&
		    workbook_sheet_count (sheet_view->sheet->workbook) < 2)
				continue;

		item = gtk_menu_item_new_with_label (
			_(sheet_label_context_actions [i].text));
		gtk_menu_append (GTK_MENU (menu), item);
		gtk_widget_show (item);

		gtk_signal_connect (
			GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (sheet_label_context_actions [i].function),
			sheet_view);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
}

/**
 * cb_sheet_label_button_press:
 *
 * Invoked when the user has clicked on the EditableLabel widget.
 * This takes care of switching to the notebook that contains the label
 */
static gboolean
cb_sheet_label_button_press (GtkWidget *widget, GdkEventButton *event,
			     GtkWidget *child)
{
	GtkWidget *notebook;
	gint page_number;

	if (event->type != GDK_BUTTON_PRESS)
		return FALSE;

	notebook = child->parent;
	page_number = gtk_notebook_page_num (GTK_NOTEBOOK (notebook), child);

	if (event->button == 1) {
		gtk_notebook_set_page (GTK_NOTEBOOK (notebook), page_number);
		return TRUE;
	}

	if (event->button == 3) {
		g_return_val_if_fail (IS_SHEET_CONTROL_GUI (child), FALSE);
		sheet_menu_label_run (SHEET_CONTROL_GUI (child), event);
		return TRUE;
	}

	return FALSE;
}

static void workbook_setup_sheets (WorkbookControlGUI *wbcg);

static int
sheet_to_page_index (WorkbookControlGUI *wbcg, Sheet *sheet, SheetControlGUI **res)
{
	int i = 0;
	GtkWidget *w;

	g_return_val_if_fail (IS_SHEET (sheet), -1);

	for ( ; NULL != (w = gtk_notebook_get_nth_page (wbcg->notebook, i)) ; i++) {
		SheetControlGUI *view = SHEET_CONTROL_GUI (w);
		if (view != NULL && view->sheet == sheet) {
			if (res)
				*res = view;
			return i;
		}
	}
	return -1;
}

/**
 * wbcg_sheet_add:
 * @sheet: a sheet
 *
 * Creates a new SheetControlGUI for the sheet and adds it to the workbook-control-gui.
 *
 * A callback to yield focus when the sheet view is destroyed is attached
 * to the sheet view. This solves a problem which we encountered e.g. when
 * a file import is canceled or fails:
 * - First, a sheet is attached to the workbook in this routine. The sheet
 *   view gets focus.
 * - Then, the sheet is detached, sheet view is destroyed, but GTK
 *   still believes that the sheet view has focus.
 * - GTK accesses already freed memory. Due to the excellent cast checking
 *   in GTK, we didn't get anything worse than warnings. But the bug had
 *   to be fixed anyway.
 */
static void
wbcg_sheet_add (WorkbookControl *wbc, Sheet *sheet)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *sheet_view;
	GtkWidget *sheet_label;
	GList     *ptr;

	g_return_if_fail (wbcg != NULL);

	if (wbcg->notebook == NULL)
		workbook_setup_sheets (wbcg);

	sheet_view = sheet_new_sheet_view (sheet);
	sheet_view->wbcg = wbcg;
	gtk_signal_connect (
		GTK_OBJECT (sheet_view->canvas), "destroy",
		GTK_SIGNAL_FUNC (yield_focus),
		wbcg->toplevel);

	/*
	 * NB. this is so we can use editable_label_set_text since
	 * gtk_notebook_set_tab_label kills our widget & replaces with a label.
	 */
	sheet_label = editable_label_new (sheet->name_unquoted);
	gtk_signal_connect_after (
		GTK_OBJECT (sheet_label), "text_changed",
		GTK_SIGNAL_FUNC (cb_sheet_label_changed), wbcg);
	gtk_signal_connect (
		GTK_OBJECT (sheet_label), "editing_stopped",
		GTK_SIGNAL_FUNC (cb_sheet_label_edit_stopped), wbcg);
	gtk_signal_connect (
		GTK_OBJECT (sheet_label), "button_press_event",
		GTK_SIGNAL_FUNC (cb_sheet_label_button_press), sheet_view);

	gtk_widget_show (sheet_label);
	gtk_widget_show_all (GTK_WIDGET (sheet_view));

	wbcg->updating_ui = TRUE;
	gtk_notebook_insert_page (wbcg->notebook,
		GTK_WIDGET (sheet_view), sheet_label,
		workbook_sheet_index_get (wb_control_workbook (wbc), sheet));
	wbcg->updating_ui = FALSE;

	/* Only be scrollable if there are more than 3 tabs */
	if (g_list_length (wbcg->notebook->children) > 3)
		gtk_notebook_set_scrollable (wbcg->notebook, TRUE);

	/* create views for the sheet objects */
	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next)
		(void) sheet_object_new_view (ptr->data, sheet_view);
}

static void
wbcg_sheet_remove (WorkbookControl *wbc, Sheet *sheet)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *sheet_view;
	int i;

	/* During destruction we may have already removed the notebook */
	if (wbcg->notebook == NULL)
		return;

	i = sheet_to_page_index (wbcg, sheet, &sheet_view);

	g_return_if_fail (i >= 0);

	gtk_notebook_remove_page (wbcg->notebook, i);
	gtk_object_unref (GTK_OBJECT (sheet_view));

	/* Only be scrollable if there are more than 3 tabs */
	if (g_list_length (wbcg->notebook->children) <= 3)
		gtk_notebook_set_scrollable (wbcg->notebook, FALSE);
}

static void
wbcg_sheet_rename (WorkbookControl *wbc, Sheet *sheet)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	GtkWidget *label;
	SheetControlGUI *sheet_view;
	int i = sheet_to_page_index (wbcg, sheet, &sheet_view);

	g_return_if_fail (i >= 0);

	label = gtk_notebook_get_tab_label (wbcg->notebook, GTK_WIDGET (sheet_view));
	editable_label_set_text (EDITABLE_LABEL (label), sheet->name_unquoted);
}

static void
wbcg_sheet_focus (WorkbookControl *wbc, Sheet *sheet)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *sheet_view;
	int i = sheet_to_page_index (wbcg, sheet, &sheet_view);

	/* A sheet added in another view may not yet have a view */
	if (i >= 0) {
		gtk_notebook_set_page (wbcg->notebook, i);
		zoom_changed (wbcg, sheet);
	}
}

static void
wbcg_sheet_move (WorkbookControl *wbc, Sheet *sheet, int new_pos)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *sheet_view;

	g_return_if_fail (IS_SHEET (sheet));

	/* No need for sanity checking, the workbook did that */
        if (sheet_to_page_index (wbcg, sheet, &sheet_view) >= 0)
		gtk_notebook_reorder_child (wbcg->notebook,
			GTK_WIDGET (sheet_view), new_pos);
}

static void
wbcg_sheet_remove_all (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;

	if (wbcg->notebook != NULL) {
		gtk_container_remove (GTK_CONTAINER (wbcg->table),
				      GTK_WIDGET (wbcg->notebook));
		wbcg->notebook = NULL;
	}
}

static void
wbcg_history_setup (WorkbookControlGUI *wbcg)
{
	GList *hl = application_history_get_list ();
	if (hl)
		history_menu_setup (wbcg, hl);
}

static void
cb_change_zoom (GtkWidget *caller, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	int factor;

	if (sheet == NULL)
		return;

	factor = atoi (gtk_entry_get_text (GTK_ENTRY (caller)));
	sheet_set_zoom_factor (sheet, (double) factor / 100, FALSE, TRUE);

	/* Restore the focus to the sheet */
	wb_control_gui_focus_cur_sheet (wbcg);
}

static void
wbcg_auto_expr_value (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	WorkbookView *wbv = wb_control_view (wbc);

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (wbv != NULL);
	g_return_if_fail (wbv->auto_expr_value_as_string != NULL);
	g_return_if_fail (!wbcg->updating_ui);

	wbcg->updating_ui = TRUE;
	gnome_canvas_item_set (wbcg->auto_expr_label,
			       "text", wbv->auto_expr_value_as_string,
			       NULL);
	wbcg->updating_ui = FALSE;
}

static GtkComboStack *
ur_stack (WorkbookControl *wbc, gboolean is_undo)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	return GTK_COMBO_STACK (is_undo ? wbcg->undo_combo : wbcg->redo_combo);
}

static void
wbcg_undo_redo_clear (WorkbookControl *wbc, gboolean is_undo)
{
	gtk_combo_stack_clear (ur_stack (wbc, is_undo));
}

static void
wbcg_undo_redo_truncate (WorkbookControl *wbc, int n, gboolean is_undo)
{
	gtk_combo_stack_truncate (ur_stack (wbc, is_undo), n);
}

static void
wbcg_undo_redo_pop (WorkbookControl *wbc, gboolean is_undo)
{
	gtk_combo_stack_remove_top (ur_stack (wbc, is_undo), 1);
}

static void
wbcg_undo_redo_push (WorkbookControl *wbc, char const *text, gboolean is_undo)
{
	gtk_combo_stack_push_item (ur_stack (wbc, is_undo), text);
}

static void
change_menu_label (
#ifndef ENABLE_BONOBO
		   GtkWidget *menu_item,
#else
		   WorkbookControlGUI const *wbcg,
		   char const *verb_path,
		   char const *menu_path, /* FIXME we need verb level labels. */
#endif
		   char const *prefix,
		   char const *suffix)
{
	gboolean  sensitive = TRUE;
	gchar    *text;

#ifndef ENABLE_BONOBO
	GtkBin   *bin = GTK_BIN(menu_item);
	GtkLabel *label = GTK_LABEL(bin->child);

	g_return_if_fail (label != NULL);
#else
	CORBA_Environment  ev;

	g_return_if_fail (wbcg != NULL);
#endif

	if (suffix == NULL) {
		suffix = _("Nothing");
		sensitive = FALSE;
	}

	text = g_strdup_printf ("%s : %s", prefix, suffix);

#ifndef ENABLE_BONOBO
	gtk_label_set_text (label, text);
	gtk_widget_set_sensitive (menu_item, sensitive);
#else
	CORBA_exception_init (&ev);

	bonobo_ui_component_set_prop (wbcg->uic, verb_path,
				      "sensitive", sensitive ? "1" : "0", &ev);
	bonobo_ui_component_set_prop (wbcg->uic, menu_path, "label", text, &ev);
	CORBA_exception_free (&ev);
#endif
	g_free (text);
}

static void
wbcg_undo_redo_labels (WorkbookControl *wbc, char const *undo, char const *redo)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	g_return_if_fail (wbcg != NULL);

#ifndef ENABLE_BONOBO
	change_menu_label (wbcg->menu_item_undo, _("Undo"), undo);
	change_menu_label (wbcg->menu_item_redo, _("Redo"), redo);
#else
	change_menu_label (wbcg, "/commands/EditUndo", "/menu/Edit/Undo",
			   _("Undo"), undo);
	change_menu_label (wbcg, "/commands/EditRedo", "/menu/Edit/Redo",
			   _("Redo"), redo);
#endif
}

static void
wbcg_paste_special_enable (WorkbookControl *wbc, gboolean enable)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
#ifndef ENABLE_BONOBO
	gtk_widget_set_sensitive (
		wbcg->menu_item_paste_special, enable);
#else
	bonobo_ui_component_set_prop (wbcg->uic,
				      "/commands/EditPasteSpecial",
				      "sensitive", enable ? "1" : "0", NULL);
#endif
}

static void
wbcg_paste_from_selection (WorkbookControl *wbc, PasteTarget const *pt, guint32 time)
{
	x_request_clipboard ((WorkbookControlGUI *)wbc, pt, time);
}

static gboolean
wbcg_claim_selection  (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	return gtk_selection_owner_set (GTK_WIDGET (wbcg->toplevel),
					GDK_SELECTION_PRIMARY,
					GDK_CURRENT_TIME);
}

static void
wbcg_progress_set (CommandContext *cc, gfloat val)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)cc;
#ifdef ENABLE_BONOBO
	gtk_progress_bar_update (GTK_PROGRESS_BAR (wbcg->progress_bar), val);
#else
	gnome_appbar_set_progress (wbcg->appbar, val);
#endif
}

static void
wbcg_error_system (CommandContext *cc, char const *msg)
{
	gnumeric_notice ((WorkbookControlGUI *)cc, GNOME_MESSAGE_BOX_ERROR, msg);
}
static void
wbcg_error_plugin (CommandContext *cc, char const *msg)
{
	gnumeric_notice ((WorkbookControlGUI *)cc, GNOME_MESSAGE_BOX_ERROR, msg);
}
static void
wbcg_error_read (CommandContext *cc, char const *msg)
{
	gnumeric_notice ((WorkbookControlGUI *)cc, GNOME_MESSAGE_BOX_ERROR, msg);
}

static void
wbcg_error_save (CommandContext *cc, char const *msg)
{
	gnumeric_notice ((WorkbookControlGUI *)cc, GNOME_MESSAGE_BOX_ERROR, msg);
}
static void
wbcg_error_invalid (CommandContext *cc, char const *msg, char const * value)
{
	char *buf = g_strconcat (msg, " : ", value, NULL);
	gnumeric_notice ((WorkbookControlGUI *)cc, GNOME_MESSAGE_BOX_ERROR, buf);
	g_free (buf);
}
static void
wbcg_error_splits_array (CommandContext *context, char const *cmd)
{
	gnumeric_error_invalid (context, cmd, _("Would split an array."));
}

static char const * const preset_zoom [] = {
	"200%",
	"100%",
	"75%",
	"50%",
	"25%",
	NULL
};

/**
 * workbook_close_if_user_permits : If the workbook is dirty the user is
 *  		prompted to see if they should exit.
 *
 * Returns : TRUE is the book remains open.
 *           FALSE if it is closed.
 */
static gboolean
workbook_close_if_user_permits (WorkbookControlGUI *wbcg, WorkbookView *wb_view)
{
	gboolean   can_close = TRUE;
	gboolean   done      = FALSE;
	int        iteration = 0;
	Workbook  *wb = wb_view_workbook (wb_view);
	static int in_can_close;

	g_return_val_if_fail (IS_WORKBOOK (wb), TRUE);

	if (in_can_close)
		return FALSE;
	in_can_close = TRUE;

	while (workbook_is_dirty (wb) && !done) {
		GtkWidget *d;
		int button;
		char *msg;

		iteration++;

		if (wb->filename)
			msg = g_strdup_printf (
				_("Workbook %s has unsaved changes, save them?"),
				g_basename (wb->filename));
		else
			msg = g_strdup (_("Workbook has unsaved changes, save them?"));

		d = gnome_message_box_new (msg,
			GNOME_MESSAGE_BOX_WARNING,
			GNOME_STOCK_BUTTON_YES,
			GNOME_STOCK_BUTTON_NO,
			GNOME_STOCK_BUTTON_CANCEL,
			NULL);

		gtk_window_set_position (GTK_WINDOW (d), GTK_WIN_POS_MOUSE);
		button = gnome_dialog_run_and_close (GNOME_DIALOG (d));
		g_free (msg);

		switch (button) {
		case 0: /* YES */
			done = workbook_save (wbcg, wb_view);
			break;

		case 1: /* NO */
			can_close = TRUE;
			done      = TRUE;
			workbook_set_dirty (wb, FALSE);
			break;

		case -1:
		case 2: /* CANCEL */
			can_close = FALSE;
			done      = TRUE;
			break;
		}
	}

	in_can_close = FALSE;

	if (can_close) {
		workbook_unref (wb);
		return FALSE;
	} else
		return TRUE;
}

/*
 * wbcg_close_control:
 *
 * Returns TRUE if the control should not be closed.
 */
static gboolean
wbcg_close_control (WorkbookControlGUI *wbcg)
{
	WorkbookView *wb_view = wb_control_view (WORKBOOK_CONTROL (wbcg));

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wb_view), TRUE);
	g_return_val_if_fail (wb_view->wb_controls != NULL, TRUE);

	/* If we were editing when the quit request came in save the edit. */
	workbook_finish_editing (wbcg, TRUE);

	/* This is the last control */
	if (wb_view->wb_controls->len <= 1) {
		Workbook *wb = wb_view_workbook (wb_view);

		g_return_val_if_fail (IS_WORKBOOK (wb), TRUE);
		g_return_val_if_fail (wb->wb_views != NULL, TRUE);

		/* This is the last view */
		if (wb_view->wb_controls->len <= 1)
			return workbook_close_if_user_permits (wbcg, wb_view);

		gtk_object_unref (GTK_OBJECT (wb_view));
	} else
		gtk_object_unref (GTK_OBJECT (wbcg));

	return FALSE;
}

/****************************************************************************/

static void
cb_file_new (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	/* FIXME : we should have a user configurable setting
	 * for how many sheets to create by default
	 */
	(void) workbook_control_gui_new (NULL, workbook_new_with_sheets (1));
}

static void
cb_file_open (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	char *fname = dialog_query_load_file (wbcg);

	if (!fname)
		return;

	(void) workbook_read (wbc, fname);
	g_free (fname);
}

static void
cb_file_import (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	char *fname = dialog_query_load_file (wbcg);

	if (!fname)
		return;

	(void) workbook_import (wbcg, fname);
	g_free (fname);
}

static void
cb_file_save (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	workbook_save (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)));
}

static void
cb_file_save_as (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	workbook_save_as (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)));
}

static void
cb_file_print_setup (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	dialog_printer_setup (wbcg, sheet);
}

static void
cb_file_print (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	sheet_print (wbcg, sheet, FALSE, PRINT_ACTIVE_SHEET);
}

static void
cb_file_print_preview (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	sheet_print (wbcg, sheet, TRUE, PRINT_ACTIVE_SHEET);
}

static void
cb_file_summary (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Workbook *wb = wb_control_workbook (wbc);
	dialog_summary_update (wbcg, wb->summary_info);
}

static void
cb_file_close (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	wbcg_close_control (wbcg);
}

static void
cb_file_quit (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GList *ptr, *workbooks;

	/* If we are still loading initial files, short circuit */
	if (!initial_workbook_open_complete) {
		initial_workbook_open_complete = TRUE;
		return;
	}

	/* If we were editing when the quit request came in save the edit. */
	workbook_finish_editing (wbcg, TRUE);

	/* list is modified during workbook destruction */
	workbooks = g_list_copy (application_workbook_list ());

	for (ptr = workbooks; ptr != NULL ; ptr = ptr->next) {
		Workbook *wb = ptr->data;
		WorkbookView *wb_view;

		g_return_if_fail (IS_WORKBOOK (wb));
		g_return_if_fail (wb->wb_views != NULL);

		if (wb_control_workbook (wbc) == wb)
			continue;
		wb_view = g_ptr_array_index (wb->wb_views, 0);
		workbook_close_if_user_permits (wbcg, wb_view);
	}
	workbook_close_if_user_permits (wbcg, wb_control_view (wbc));

	g_list_free (workbooks);
}

/****************************************************************************/

static void
cb_edit_clear_all (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	cmd_clear_selection (wbc, wb_control_cur_sheet (wbc),
			     CLEAR_VALUES | CLEAR_FORMATS | CLEAR_COMMENTS);
}

static void
cb_edit_clear_formats (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	cmd_clear_selection (wbc, wb_control_cur_sheet (wbc),
			     CLEAR_FORMATS);
}

static void
cb_edit_clear_comments (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	cmd_clear_selection (wbc, wb_control_cur_sheet (wbc),
			     CLEAR_COMMENTS);
}

static void
cb_edit_clear_content (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	cmd_clear_selection (wbc, wb_control_cur_sheet (wbc),
			     CLEAR_VALUES);
}

static void
cb_edit_select_all (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_select_all (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_row (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_select_cur_row (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_col (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_select_cur_col (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_array (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_select_cur_array (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_depend (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_select_cur_depends (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}

static void
cb_edit_undo (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	workbook_finish_editing (wbcg, FALSE);
	command_undo (wbc);
}

static void
cb_undo_combo (GtkWidget *widget, gint num, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	int i;

	workbook_finish_editing (wbcg, FALSE);
	for (i = 0; i < num; i++)
		command_undo (wbc);
}

static void
cb_edit_redo (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	workbook_finish_editing (wbcg, FALSE);
	command_redo (wbc);
}

static void
cb_redo_combo (GtkWidget *widget, gint num, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	int i;

	workbook_finish_editing (wbcg, FALSE);
	for (i = 0; i < num; i++)
		command_redo (wbc);
}

static void
cb_edit_cut (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);

	if (sheet->current_object != NULL)
		gtk_object_destroy (GTK_OBJECT (sheet->current_object));
	else
		sheet_selection_cut (wbc, sheet);
	sheet_mode_edit	(sheet);
}

static void
cb_edit_copy (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	sheet_selection_copy (wbc, sheet);
}

static void
cb_edit_paste (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	cmd_paste_to_selection (wbc, sheet, PASTE_DEFAULT);
}

static void
cb_edit_paste_special (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	int flags = dialog_paste_special (wbcg);
	if (flags != 0)
		cmd_paste_to_selection (wbc, sheet, flags);
}

static void
cb_edit_delete (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	dialog_delete_cells (wbcg, sheet);
}

static void
cb_edit_delete_sheet (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetControlGUI *res;

	if (sheet_to_page_index (wbcg, wb_control_cur_sheet (wbc), &res) >= 0)
		delete_sheet_if_possible (NULL, res);
}

static void
cb_edit_goto (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_goto_cell (wbcg);
}

static void
cb_edit_recalc (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	workbook_recalc_all (wb_control_workbook (WORKBOOK_CONTROL (wbcg)));
}

/****************************************************************************/

static void
cb_view_zoom (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_zoom (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_view_new_shared (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wb_control_wrapper_new (wbc, wb_control_view (wbc),
				wb_control_workbook (wbc));
}

static void
cb_view_new_unshared (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wb_control_wrapper_new (wbc, NULL,
				wb_control_workbook (wbc));
}

/****************************************************************************/

static void
cb_insert_current_date (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	cmd_set_date_time (wbc, sheet, &sheet->cursor.edit_pos, TRUE);
}

static void
cb_insert_current_time (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	cmd_set_date_time (wbc, sheet, &sheet->cursor.edit_pos, FALSE);
}

static void
cb_define_name (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_define_names (wbcg);
}

static void
cb_insert_sheet (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	workbook_sheet_add (wb_control_workbook (WORKBOOK_CONTROL (wbcg)),
			    NULL, TRUE);
}

static void
cb_insert_rows (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetSelection *ss;
	int rows;

	/* TODO : No need to check simplicty.  XL applies for each
	 * non-discrete selected region, (use selection_apply) */
	if (!selection_is_simple (wbc, sheet, _("Insert rows")))
		return;

	ss = sheet->selections->data;

	/* TODO : Have we have selected columns rather than rows
	 * This menu item should be disabled when a full column is selected
	 *
	 * at minimum a warning if things are about to be cleared ?
	 */
	rows = ss->user.end.row - ss->user.start.row + 1;
	cmd_insert_rows (wbc, sheet, ss->user.start.row, rows);
}

static void
cb_insert_cols (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetSelection *ss;
	int cols;

	/* TODO : No need to check simplicty.  XL applies for each
	 * non-discrete selected region, (use selection_apply) */
	if (!selection_is_simple (wbc, sheet, _("Insert cols")))
		return;

	ss = sheet->selections->data;

	/* TODO : Have we have selected rows rather than columns
	 * This menu item should be disabled when a full row is selected
	 *
	 * at minimum a warning if things are about to be cleared ?
	 */
	cols = ss->user.end.col - ss->user.start.col + 1;
	cmd_insert_cols (wbc, sheet, ss->user.start.col, cols);
}

static void
cb_insert_cells (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_insert_cells (wbcg, wb_control_cur_sheet (wbc));
}

#ifdef ENABLE_BONOBO
static void
cb_insert_bonobo_object (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	char  *obj_id;

	obj_id = bonobo_selector_select_id (
		_("Select an object to add"), NULL);

	if (obj_id != NULL)
		sheet_mode_create_object (
			sheet_object_container_new_object (sheet, obj_id));
	else
		sheet_mode_edit	(sheet);
}
#endif

static void
cb_insert_comment (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);

	Cell *cell = sheet_cell_fetch (sheet,
				       sheet->cursor.edit_pos.col,
				       sheet->cursor.edit_pos.row);
	dialog_cell_comment (wbcg, cell);
}

/****************************************************************************/

static void
cb_sheet_change_name (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	char *new_name;

	new_name = dialog_get_sheet_name (wbcg, sheet->name_unquoted);
	if (!new_name)
		return;

	cmd_rename_sheet (WORKBOOK_CONTROL (wbcg),
			  sheet->name_unquoted, new_name);
	g_free (new_name);
}

static void
cb_sheet_order (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
        dialog_sheet_order (wbcg);
}

static void
cb_cell_rerender (gpointer element, gpointer userdata)
{
	Dependent *dep = element;
	int const t = (dep->flags & DEPENDENT_TYPE_MASK);
	if (t == DEPENDENT_CELL)
		sheet_cell_calc_span (DEP_TO_CELL (dep), SPANCALC_RE_RENDER);
}

static void
cb_sheet_pref_display_formulas (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));

	sheet->display_formulas = !sheet->display_formulas;
	g_list_foreach (wb->dependents, &cb_cell_rerender, NULL);
	sheet_redraw_all (sheet);
}
static void
cb_sheet_pref_hide_zeros (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	sheet->display_zero = ! sheet->display_zero;
	sheet_redraw_all (sheet);
}
static void
cb_sheet_pref_hide_grid_lines (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	sheet->show_grid = !sheet->show_grid;
	sheet_redraw_all (sheet);
}
static void
cb_sheet_pref_hide_col_header (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	sheet->show_col_header = ! sheet->show_col_header;
	sheet_adjust_preferences (sheet);
}
static void
cb_sheet_pref_hide_row_header (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	sheet->show_row_header = ! sheet->show_row_header;
	sheet_adjust_preferences (sheet);
}

/****************************************************************************/

static void
cb_format_cells (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_cell_format (wbcg, wb_control_cur_sheet (wbc), FD_CURRENT);
}

static void
cb_autoformat (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_autoformat (wbcg);
}

static void
cb_workbook_attr (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_workbook_attr (wbcg);
}

static void
cb_tools_plugins (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_plugin_manager (wbcg);
}

static void
cb_tools_autocorrect (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_autocorrect (wbcg);
}

static void
cb_tools_auto_save (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_autosave (wbcg);
}

static void
cb_tools_goal_seek (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_goal_seek (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_solver (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_solver (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_data_analysis (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_data_analysis (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_data_sort (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_cell_sort (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_data_filter (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_advanced_filter (wbcg);
}

static void
cb_help_about (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_about (wbcg);
}

static void
cb_autosum (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	GtkEntry *entry;
	gchar *txt;

	if (wbcg->editing)
		return;

	entry = workbook_get_entry (wbcg);
	txt = gtk_entry_get_text (entry);
	if (strncmp (txt, "=sum(", 5)) {
		workbook_start_editing_at_cursor (wbcg, TRUE, TRUE);
		gtk_entry_set_text (entry, "=sum()");
		gtk_entry_set_position (entry, 5);
	} else {
		workbook_start_editing_at_cursor (wbcg, FALSE, TRUE);

		/*
		 * FIXME : This is crap!
		 * When the function druid is more complete use that.
		 */
		gtk_entry_set_position (entry, entry->text_length-1);
	}
}

static void
cb_formula_guru (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_formula_guru (wbcg);
}

static void
sort_cmd (WorkbookControlGUI *wbcg, int asc)
{
	Sheet *sheet;
	Range *sel;
	SortData *data;
	SortClause *clause;
	int numclause, i;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	g_return_if_fail (IS_SHEET (sheet));

	sel = range_copy (selection_first_range (sheet, TRUE));

	/* We can't sort complex ranges */
	if (!selection_is_simple (WORKBOOK_CONTROL (wbcg), sheet, _("sort")))
		return;

	range_clip_to_finite (sel, sheet);

	numclause = sel->end.col - sel->start.col + 1;
	clause = g_new0 (SortClause, numclause);
	for (i=0; i < numclause; i++) {
		clause[i].offset = i;
		clause[i].asc = asc;
		clause[i].cs = FALSE;
		clause[i].val = TRUE;
	}

	data = g_new (SortData, 1);
	data->sheet = sheet;
	data->range = sel;
	data->num_clause = numclause;
	data->clauses = clause;
	data->top = TRUE;

	if (range_has_header (data->sheet, data->range, TRUE)) {
		data->range->start.row += 1;
	}

	cmd_sort (WORKBOOK_CONTROL (wbcg), data);
}

static void
cb_sort_ascending (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	sort_cmd (wbcg, 0);
}

static void
cb_sort_descending (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	sort_cmd (wbcg, 1);
}

#ifdef ENABLE_BONOBO
static void
cb_launch_graph_guru (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_graph_guru (wbcg);
}

static void
select_component_id (Sheet *sheet, char const *interface)
{
	char *obj_id;
	char const *required_interfaces [2];

	required_interfaces [0] = interface;
	required_interfaces [1] = NULL;

	obj_id = bonobo_selector_select_id (_("Select an object to add"),
					    required_interfaces);
	if (obj_id != NULL)
		sheet_mode_create_object (
			sheet_object_container_new_object (sheet, obj_id));
	else
		sheet_mode_edit	(sheet);
}

static void
cb_insert_component (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	select_component_id (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)),
			     "IDL:Bonobo/Embeddable:1.0");
}

static void
cb_insert_shaped_component (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	select_component_id (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)),
			     "IDL:Bonobo/Canvas/Item:1.0");
}

static void
cb_dump_xml (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	bonobo_window_dump (BONOBO_WINDOW(wbcg->toplevel), "on demand");
}
#endif

#ifndef ENABLE_BONOBO
/*
 * Hide/show some toolbar items depending on the toolbar orientation
 */
static void
workbook_standard_toolbar_orient (GtkToolbar *toolbar,
				  GtkOrientation orientation,
				  gpointer data)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)data;

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
		gtk_widget_show (wbcg->zoom_entry);
	else
		gtk_widget_hide (wbcg->zoom_entry);
}


/* File menu */
static GnomeUIInfo workbook_menu_file [] = {
        GNOMEUIINFO_MENU_NEW_ITEM (N_("_New"), N_("Create a new spreadsheet"),
				  cb_file_new, NULL),

	GNOMEUIINFO_MENU_OPEN_ITEM (cb_file_open, NULL),
	GNOMEUIINFO_ITEM_STOCK (N_("_Import..."), N_("Imports a file"),
				cb_file_import, GNOME_STOCK_MENU_OPEN),
	GNOMEUIINFO_MENU_SAVE_ITEM (cb_file_save, NULL),

	GNOMEUIINFO_MENU_SAVE_AS_ITEM (cb_file_save_as, NULL),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_PRINT_SETUP_ITEM (cb_file_print_setup, NULL),
	GNOMEUIINFO_MENU_PRINT_ITEM (cb_file_print, NULL),
	GNOMEUIINFO_ITEM_STOCK (N_("Print pre_view"), N_("Print preview"),
				cb_file_print_preview,
				"Menu_Gnumeric_PrintPreview"),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE (N_("Su_mmary..."),
			       N_("Summary information"),
			       cb_file_summary),

	GNOMEUIINFO_MENU_CLOSE_ITEM (cb_file_close, NULL),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_EXIT_ITEM (cb_file_quit, NULL),
	GNOMEUIINFO_END
};

/* Edit menu */
static GnomeUIInfo workbook_menu_edit_clear [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_All"),
		N_("Clear the selected cells' formats, comments, and contents"),
		cb_edit_clear_all),
	GNOMEUIINFO_ITEM_NONE (N_("_Formats"),
		N_("Clear the selected cells' formats"),
		cb_edit_clear_formats),
	GNOMEUIINFO_ITEM_NONE (N_("Co_mments"),
		N_("Clear the selected cells' comments"),
		cb_edit_clear_comments),
	GNOMEUIINFO_ITEM_NONE (N_("_Content"),
		N_("Clear the selected cells' contents"),
		cb_edit_clear_content),
	GNOMEUIINFO_END
};

#define GNOME_MENU_EDIT_PATH D_("_Edit/")

static GnomeUIInfo workbook_menu_edit_select [] = {
	{ GNOME_APP_UI_ITEM, N_("Select _All"),
	  N_("Select all cells in the spreadsheet"),
	  cb_edit_select_all, NULL,
	  NULL, 0, 0, 'a', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select _Row"),
	  N_("Select an entire row"),
	  cb_edit_select_row, NULL,
	  NULL, 0, 0, ' ', GDK_SHIFT_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select _Column"),
	  N_("Select an entire column"),
	  cb_edit_select_col, NULL,
	  NULL, 0, 0, ' ', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select Arra_y"),
	  N_("Select an array of cells"),
	  cb_edit_select_array, NULL,
	  NULL, 0, 0, '/', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select _Depends"),
	  N_("Select all the cells that depend on the current edit cell."),
	  cb_edit_select_depend, NULL,
	  NULL, 0, 0, 0, 0 },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_edit [] = {
	GNOMEUIINFO_MENU_UNDO_ITEM (cb_edit_undo, NULL),
	GNOMEUIINFO_MENU_REDO_ITEM (cb_edit_redo, NULL),

	GNOMEUIINFO_SEPARATOR,

        GNOMEUIINFO_MENU_CUT_ITEM (cb_edit_cut, NULL),
	GNOMEUIINFO_MENU_COPY_ITEM (cb_edit_copy, NULL),
	GNOMEUIINFO_MENU_PASTE_ITEM (cb_edit_paste, NULL),

	GNOMEUIINFO_ITEM_NONE (N_("P_aste special..."),
		N_("Paste with optional filters and transformations"),
		cb_edit_paste_special),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("C_lear"), workbook_menu_edit_clear),

	GNOMEUIINFO_ITEM_NONE (N_("_Delete..."),
		N_("Remove selected cells, shifting other into their place"),
		cb_edit_delete),
	GNOMEUIINFO_ITEM_NONE (N_("De_lete Sheet"),
		N_("Irrevocably remove an entire sheet"),
		cb_edit_delete_sheet),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("_Select..."), workbook_menu_edit_select),

	/* Default <Ctrl-G> to be goto */
	{ GNOME_APP_UI_ITEM, N_("_Goto cell..."),
		  N_("Jump to a specified cell"),
		  cb_edit_goto,
		  NULL, NULL, 0, 0, GDK_G, GDK_CONTROL_MASK },

	/* Default <F9> to recalculate */
	{ GNOME_APP_UI_ITEM, N_("_Recalculate"),
		  N_("Recalculate the spreadsheet"),
		  cb_edit_recalc,
		  NULL, NULL, 0, 0, GDK_F9, 0 },

	GNOMEUIINFO_END
};

/* View menu */

static GnomeUIInfo workbook_menu_view [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Zoom..."),
		N_("Zoom the spreadsheet in or out"),
		cb_view_zoom),
	GNOMEUIINFO_ITEM_NONE (N_("New _Shared"),
		N_("Create a new shared view of the workbook"),
		cb_view_new_shared),
	GNOMEUIINFO_ITEM_NONE (N_("New _Unshared"),
		N_("Create a new unshared view of the workbook"),
		cb_view_new_unshared),
	GNOMEUIINFO_END
};

/* Insert menu */

static GnomeUIInfo workbook_menu_insert_special [] = {
	/* Default <Ctrl-;> (control semi-colon) to insert the current date */
	{ GNOME_APP_UI_ITEM, N_("Current _date"),
	  N_("Insert the current date into the selected cell(s)"),
	  cb_insert_current_date,
	  NULL, NULL, 0, 0, ';', GDK_CONTROL_MASK },

	/* Default <Ctrl-:> (control colon) to insert the current time */
	{ GNOME_APP_UI_ITEM, N_("Current _time"),
	  N_("Insert the current time into the selected cell(s)"),
	  cb_insert_current_time,
	  NULL, NULL, 0, 0, ':', GDK_CONTROL_MASK },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_names [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Define..."),
		N_("Edit sheet and workbook names"),
		cb_define_name),
#if 0
	GNOMEUIINFO_ITEM_NONE (N_("_Auto generate names..."),
		N_("Use the current selection to create names"),
		cb_auto_generate__named_expr),
#endif
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_insert [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Sheet"),
		N_("Insert a new spreadsheet"),
		cb_insert_sheet),
	GNOMEUIINFO_ITEM_NONE (N_("_Rows"),
		N_("Insert new rows"),
		cb_insert_rows),
	GNOMEUIINFO_ITEM_NONE (N_("_Columns"),
		N_("Insert new columns"),
		cb_insert_cols),
	GNOMEUIINFO_ITEM_NONE (N_("C_ells..."),
		N_("Insert new cells"),
		cb_insert_cells),

#ifdef ENABLE_BONOBO
	GNOMEUIINFO_ITEM_NONE (N_("_Object..."),
		N_("Inserts a Bonobo object"),
		cb_insert_bonobo_object),
#endif

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("_Name"), workbook_menu_names),

	GNOMEUIINFO_ITEM_NONE (N_("_Add\\modify comment..."),
		N_("Edit the selected cell's comment"),
		cb_insert_comment),

	GNOMEUIINFO_SUBTREE(N_("S_pecial"), workbook_menu_insert_special),
	GNOMEUIINFO_END
};

/* Format menu */
static GnomeUIInfo workbook_menu_format_column [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Auto fit selection"),
		NULL,
		workbook_cmd_format_column_auto_fit),
	GNOMEUIINFO_ITEM_NONE (N_("_Width..."),
		NULL,
		sheet_dialog_set_column_width),
	GNOMEUIINFO_ITEM_NONE (N_("_Hide"),
		NULL,
		workbook_cmd_format_column_hide),
	GNOMEUIINFO_ITEM_NONE (N_("_Unhide"),
		NULL,
		workbook_cmd_format_column_unhide),
	GNOMEUIINFO_ITEM_NONE (N_("_Standard Width"),
		NULL,
		workbook_cmd_format_column_std_width),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_row [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Auto fit selection"),
		NULL,
		workbook_cmd_format_row_auto_fit),
	GNOMEUIINFO_ITEM_NONE (N_("_Height..."),
		NULL,
		sheet_dialog_set_row_height),
	GNOMEUIINFO_ITEM_NONE (N_("_Hide"),
		NULL,
		workbook_cmd_format_row_hide),
	GNOMEUIINFO_ITEM_NONE (N_("_Unhide"),
		NULL,
		workbook_cmd_format_row_unhide),
	GNOMEUIINFO_ITEM_NONE (N_("_Standard Height"),
		NULL,
		workbook_cmd_format_row_std_height),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_sheet [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Change name"),
		NULL,
		cb_sheet_change_name),
	GNOMEUIINFO_ITEM_NONE (N_("Re-_Order Sheets"),
		NULL,
		cb_sheet_order),
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Display _Formulas"), NULL,
		cb_sheet_pref_display_formulas, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Hide _Zeros"), NULL,
		cb_sheet_pref_hide_zeros, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Hide _Gridlines"), NULL,
		cb_sheet_pref_hide_grid_lines, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Hide _Column Header"), NULL,
		cb_sheet_pref_hide_col_header, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Hide _Row Header"), NULL,
		cb_sheet_pref_hide_row_header, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format [] = {
	/* Default <Ctrl-1> invoke the format dialog */
	{ GNOME_APP_UI_ITEM, N_("_Cells..."),
		N_("Modify the formatting of the selected cells"),
		cb_format_cells, NULL, NULL, 0, 0, GDK_1, GDK_CONTROL_MASK },

	GNOMEUIINFO_SUBTREE(N_("C_olumn"), workbook_menu_format_column),
	GNOMEUIINFO_SUBTREE(N_("_Row"),    workbook_menu_format_row),
	GNOMEUIINFO_SUBTREE(N_("_Sheet"),  workbook_menu_format_sheet),

	GNOMEUIINFO_ITEM_NONE (N_("_Autoformat..."),
			      N_("Format a region of cells according to a pre-defined template"),
			      cb_autoformat),

	GNOMEUIINFO_ITEM_NONE (N_("_Workbook..."),
		N_("Modify the workbook attributes"),
		cb_workbook_attr),
	GNOMEUIINFO_END
};

/* Tools menu */
static GnomeUIInfo workbook_menu_tools [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Plug-ins..."),
		N_("Manage available plugin modules."),
		cb_tools_plugins),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE (N_("Auto _Correct..."),
		N_("Automatically perform simple spell checking"),
		cb_tools_autocorrect),
	GNOMEUIINFO_ITEM_NONE (N_("_Auto Save..."),
		N_("Automatically save the current document at regular intervals"),
		cb_tools_auto_save),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE (N_("_Goal Seek..."),
		N_("Iteratively recalculate to find a target value"),
		cb_tools_goal_seek),
	GNOMEUIINFO_ITEM_NONE (N_("_Solver..."),
		N_("Iteratively recalculate with constraints to approach a target value"),
		cb_tools_solver),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE (N_("_Data Analysis..."),
		N_("Statistical methods."),
		cb_tools_data_analysis),

	GNOMEUIINFO_END
};

/* Data menu */
static GnomeUIInfo workbook_menu_data [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Sort..."),
		N_("Sort the selected cells"),
		cb_data_sort),
	GNOMEUIINFO_ITEM_NONE (N_("_Filter..."),
		N_("Filter data with given criteria"),
		cb_data_filter),

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_help [] = {
	GNOMEUIINFO_HELP ("gnumeric"),
        GNOMEUIINFO_MENU_ABOUT_ITEM (cb_help_about, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu [] = {
        GNOMEUIINFO_MENU_FILE_TREE (workbook_menu_file),
	GNOMEUIINFO_MENU_EDIT_TREE (workbook_menu_edit),
	GNOMEUIINFO_MENU_VIEW_TREE (workbook_menu_view),
	GNOMEUIINFO_SUBTREE(N_("_Insert"), workbook_menu_insert),
	GNOMEUIINFO_SUBTREE(N_("F_ormat"), workbook_menu_format),
	GNOMEUIINFO_SUBTREE(N_("_Tools"),  workbook_menu_tools),
	GNOMEUIINFO_SUBTREE(N_("_Data"),   workbook_menu_data),
	GNOMEUIINFO_MENU_HELP_TREE (workbook_menu_help),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_standard_toolbar [] = {
	GNOMEUIINFO_ITEM_STOCK (
		N_("New"), N_("Creates a new workbook"),
		cb_file_new, GNOME_STOCK_PIXMAP_NEW),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Open"), N_("Opens an existing workbook"),
		cb_file_open, GNOME_STOCK_PIXMAP_OPEN),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Save"), N_("Saves the workbook"),
		cb_file_save, GNOME_STOCK_PIXMAP_SAVE),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Print"), N_("Prints the workbook"),
		cb_file_print, GNOME_STOCK_PIXMAP_PRINT),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Print pre_view"), N_("Print preview"),
		cb_file_print_preview, "Gnumeric_PrintPreview"),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Cut"), N_("Cuts the selection to the clipboard"),
		cb_edit_cut, GNOME_STOCK_PIXMAP_CUT),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Copy"), N_("Copies the selection to the clipboard"),
		cb_edit_copy, GNOME_STOCK_PIXMAP_COPY),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Paste"), N_("Pastes the clipboard"),
		cb_edit_paste, GNOME_STOCK_PIXMAP_PASTE),

	GNOMEUIINFO_SEPARATOR,

#if 0
	GNOMEUIINFO_ITEM_STOCK (
		N_("Undo"), N_("Undo the operation"),
		cb_edit_undo, GNOME_STOCK_PIXMAP_UNDO),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Redo"), N_("Redo the operation"),
		cb_edit_redo, GNOME_STOCK_PIXMAP_REDO),
#else
#define TB_UNDO_POS 11
#define TB_REDO_POS 12
#endif

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Sum"), N_("Sum into the current cell."),
		cb_autosum, "Gnumeric_AutoSum"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Function"), N_("Edit a function in the current cell."),
		cb_formula_guru, "Gnumeric_FormulaGuru"),

	GNOMEUIINFO_ITEM_STOCK (
		N_("Sort Ascending"), N_("Sorts the selected region in ascending order based on the first column selected."),
		cb_sort_ascending, "Gnumeric_SortAscending"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Sort Descending"), N_("Sorts the selected region in descending order based on the first column selected."),
		cb_sort_descending, "Gnumeric_SortDescending"),

	GNOMEUIINFO_END
};
#else
static BonoboUIVerb verbs [] = {

	BONOBO_UI_UNSAFE_VERB ("FileNew", cb_file_new),
	BONOBO_UI_UNSAFE_VERB ("FileOpen", cb_file_open),
	BONOBO_UI_UNSAFE_VERB ("FileImport", cb_file_import),
	BONOBO_UI_UNSAFE_VERB ("FileSave", cb_file_save),
	BONOBO_UI_UNSAFE_VERB ("FileSaveAs", cb_file_save_as),
	BONOBO_UI_UNSAFE_VERB ("FilePrintSetup", cb_file_print_setup),
	BONOBO_UI_UNSAFE_VERB ("FilePrint", cb_file_print),
	BONOBO_UI_UNSAFE_VERB ("FilePrintPreview", cb_file_print_preview),
	BONOBO_UI_UNSAFE_VERB ("FileSummary", cb_file_summary),
	BONOBO_UI_UNSAFE_VERB ("FileClose", cb_file_close),
	BONOBO_UI_UNSAFE_VERB ("FileExit", cb_file_quit),

	BONOBO_UI_UNSAFE_VERB ("EditClearAll", cb_edit_clear_all),
	BONOBO_UI_UNSAFE_VERB ("EditClearFormats", cb_edit_clear_formats),
	BONOBO_UI_UNSAFE_VERB ("EditClearComments", cb_edit_clear_comments),
	BONOBO_UI_UNSAFE_VERB ("EditClearContent", cb_edit_clear_content),

	BONOBO_UI_UNSAFE_VERB ("EditSelectAll", cb_edit_select_all),
	BONOBO_UI_UNSAFE_VERB ("EditSelectRow", cb_edit_select_row),
	BONOBO_UI_UNSAFE_VERB ("EditSelectColumn", cb_edit_select_col),
	BONOBO_UI_UNSAFE_VERB ("EditSelectArray", cb_edit_select_array),
	BONOBO_UI_UNSAFE_VERB ("EditSelectDepends", cb_edit_select_depend),

	BONOBO_UI_UNSAFE_VERB ("EditUndo", cb_edit_undo),
	BONOBO_UI_UNSAFE_VERB ("EditRedo", cb_edit_redo),
	BONOBO_UI_UNSAFE_VERB ("EditCut", cb_edit_cut),
	BONOBO_UI_UNSAFE_VERB ("EditCopy", cb_edit_copy),
	BONOBO_UI_UNSAFE_VERB ("EditPaste", cb_edit_paste),
	BONOBO_UI_UNSAFE_VERB ("EditPasteSpecial", cb_edit_paste_special),
	BONOBO_UI_UNSAFE_VERB ("EditDelete", cb_edit_delete),
	BONOBO_UI_UNSAFE_VERB ("EditDeleteSheet", cb_edit_delete_sheet),
	BONOBO_UI_UNSAFE_VERB ("EditGoto", cb_edit_goto),
	BONOBO_UI_UNSAFE_VERB ("EditRecalc", cb_edit_recalc),

	BONOBO_UI_UNSAFE_VERB ("ViewZoom", cb_view_zoom),
	BONOBO_UI_UNSAFE_VERB ("ViewNewShared", cb_view_new_shared),
	BONOBO_UI_UNSAFE_VERB ("ViewNewUnshared", cb_view_new_unshared),

	BONOBO_UI_UNSAFE_VERB ("InsertCurrentDate", cb_insert_current_date),
	BONOBO_UI_UNSAFE_VERB ("InsertCurrentTime", cb_insert_current_time),
	BONOBO_UI_UNSAFE_VERB ("EditNames", cb_define_name),

	BONOBO_UI_UNSAFE_VERB ("InsertSheet", cb_insert_sheet),
	BONOBO_UI_UNSAFE_VERB ("InsertRows", cb_insert_rows),
	BONOBO_UI_UNSAFE_VERB ("InsertColumns", cb_insert_cols),
	BONOBO_UI_UNSAFE_VERB ("InsertCells", cb_insert_cells),
	BONOBO_UI_UNSAFE_VERB ("InsertObject", cb_insert_bonobo_object),
	BONOBO_UI_UNSAFE_VERB ("InsertComment", cb_insert_comment),

	BONOBO_UI_UNSAFE_VERB ("ColumnAutoSize",
		workbook_cmd_format_column_auto_fit),
	BONOBO_UI_UNSAFE_VERB ("ColumnSize",
		sheet_dialog_set_column_width),
	BONOBO_UI_UNSAFE_VERB ("ColumnHide",
		workbook_cmd_format_column_hide),
	BONOBO_UI_UNSAFE_VERB ("ColumnUnhide",
		workbook_cmd_format_column_unhide),
	BONOBO_UI_UNSAFE_VERB ("ColumnDefaultSize",
		workbook_cmd_format_column_std_width),

	BONOBO_UI_UNSAFE_VERB ("RowAutoSize",
		workbook_cmd_format_row_auto_fit),
	BONOBO_UI_UNSAFE_VERB ("RowSize",
		sheet_dialog_set_row_height),
	BONOBO_UI_UNSAFE_VERB ("RowHide",
		workbook_cmd_format_row_hide),
	BONOBO_UI_UNSAFE_VERB ("RowUnhide",
		workbook_cmd_format_row_unhide),
	BONOBO_UI_UNSAFE_VERB ("RowDefaultSize",
		workbook_cmd_format_row_std_height),

	BONOBO_UI_UNSAFE_VERB ("SheetChangeName",
		cb_sheet_change_name),
	BONOBO_UI_UNSAFE_VERB ("SheetReorder",
		cb_sheet_order),
	BONOBO_UI_UNSAFE_VERB ("SheetDisplayFormulas",
		cb_sheet_pref_display_formulas),
	BONOBO_UI_UNSAFE_VERB ("SheetHideZeros",
		cb_sheet_pref_hide_zeros),
	BONOBO_UI_UNSAFE_VERB ("SheetHideGridlines",
		cb_sheet_pref_hide_grid_lines),
	BONOBO_UI_UNSAFE_VERB ("SheetHideColHeader",
		cb_sheet_pref_hide_col_header),
	BONOBO_UI_UNSAFE_VERB ("SheetHideRowHeader",
		cb_sheet_pref_hide_row_header),

	BONOBO_UI_UNSAFE_VERB ("FormatCells", cb_format_cells),
	BONOBO_UI_UNSAFE_VERB ("FormatAuto", cb_autoformat),
	BONOBO_UI_UNSAFE_VERB ("FormatWorkbook", cb_workbook_attr),

	BONOBO_UI_UNSAFE_VERB ("ToolsPlugins", cb_tools_plugins),
	BONOBO_UI_UNSAFE_VERB ("ToolsAutoCorrect", cb_tools_autocorrect),
	BONOBO_UI_UNSAFE_VERB ("ToolsAutoSave", cb_tools_auto_save),
	BONOBO_UI_UNSAFE_VERB ("ToolsGoalSeek", cb_tools_goal_seek),
	BONOBO_UI_UNSAFE_VERB ("ToolsSolver", cb_tools_solver),
	BONOBO_UI_UNSAFE_VERB ("ToolsDataAnalysis", cb_tools_data_analysis),

	BONOBO_UI_UNSAFE_VERB ("DataSort", cb_data_sort),
	BONOBO_UI_UNSAFE_VERB ("DataFilter", cb_data_filter),

	BONOBO_UI_UNSAFE_VERB ("AutoSum", cb_autosum),
	BONOBO_UI_UNSAFE_VERB ("FunctionGuru", cb_formula_guru),
	BONOBO_UI_UNSAFE_VERB ("SortAscending", cb_sort_ascending),
	BONOBO_UI_UNSAFE_VERB ("SortDescending", cb_sort_descending),
	BONOBO_UI_UNSAFE_VERB ("GraphGuru", cb_launch_graph_guru),
	BONOBO_UI_UNSAFE_VERB ("InsertComponent", cb_insert_component),
	BONOBO_UI_UNSAFE_VERB ("InsertShapedComponent", cb_insert_shaped_component),

	BONOBO_UI_UNSAFE_VERB ("HelpAbout", cb_help_about),

	BONOBO_UI_UNSAFE_VERB ("DebugDumpXml", cb_dump_xml),

	BONOBO_UI_VERB_END
};
#endif

/*
 * These create toolbar routines are kept independent, as they
 * will need some manual customization in the future (like adding
 * special purposes widgets for fonts, size, zoom
 */
static void
workbook_create_standard_toolbar (WorkbookControlGUI *wbcg)
{
	int i, len;
	GtkWidget *toolbar, *zoom, *entry, *undo, *redo;
#ifndef ENABLE_BONOBO
	GnomeApp *app;
	GnomeDockItemBehavior behavior;
	const char *name = "StandardToolbar";
#endif

	/* Zoom combo box */
	zoom = wbcg->zoom_entry = gtk_combo_text_new (FALSE);
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (zoom), GTK_RELIEF_NONE);
	entry = GTK_COMBO_TEXT (zoom)->entry;
	gtk_signal_connect (GTK_OBJECT (entry), "activate",
			    GTK_SIGNAL_FUNC (cb_change_zoom), wbcg);
	gtk_combo_box_set_title (GTK_COMBO_BOX (zoom), _("Zoom"));

	/* Set a reasonable default width */
	len = gdk_string_measure (entry->style->font, "%10000");
	gtk_widget_set_usize (entry, len, 0);

	/* Preset values */
	for (i = 0; preset_zoom[i] != NULL ; ++i)
		gtk_combo_text_add_item(GTK_COMBO_TEXT (zoom),
					preset_zoom[i], preset_zoom[i]);

	/* Undo dropdown list */
	undo = wbcg->undo_combo = gtk_combo_stack_new (GNOME_STOCK_PIXMAP_UNDO, TRUE);
	gtk_combo_box_set_title (GTK_COMBO_BOX (undo), _("Undo"));
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (undo), GTK_RELIEF_NONE);
	gtk_signal_connect (GTK_OBJECT (undo), "pop",
			    (GtkSignalFunc) cb_undo_combo, wbcg);

	/* Redo dropdown list */
	redo = wbcg->redo_combo = gtk_combo_stack_new (GNOME_STOCK_PIXMAP_REDO, TRUE);
	gtk_combo_box_set_title (GTK_COMBO_BOX (redo), _("Redo"));
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (redo), GTK_RELIEF_NONE);
	gtk_signal_connect (GTK_OBJECT (redo), "pop",
			    (GtkSignalFunc) cb_redo_combo, wbcg);

#ifdef ENABLE_BONOBO
	gnumeric_inject_widget_into_bonoboui (wbcg, undo, "/StandardToolbar/EditUndo");
	gnumeric_inject_widget_into_bonoboui (wbcg, redo, "/StandardToolbar/EditRedo");
	gnumeric_inject_widget_into_bonoboui (wbcg, zoom, "/StandardToolbar/SheetZoom");
	toolbar = NULL;
#else
	app = GNOME_APP (wbcg->toplevel);

	g_return_if_fail (app != NULL);

	toolbar = gnumeric_toolbar_new (workbook_standard_toolbar,
					app->accel_group, wbcg);

	behavior = GNOME_DOCK_ITEM_BEH_NORMAL;

	if (!gnome_preferences_get_menubar_detachable ())
		behavior |= GNOME_DOCK_ITEM_BEH_LOCKED;
	gnome_app_add_toolbar (
		GNOME_APP (wbcg->toplevel),
		GTK_TOOLBAR (toolbar),
		name,
		behavior,
		GNOME_DOCK_TOP, 1, 0, 0);

	/* Add them to the toolbar */
	gnumeric_toolbar_insert_with_eventbox (GTK_TOOLBAR (toolbar),
					       undo, _("Undo"), NULL, TB_UNDO_POS);
	gnumeric_toolbar_insert_with_eventbox (GTK_TOOLBAR (toolbar),
					       redo, _("Redo"), NULL, TB_REDO_POS);
	gnumeric_toolbar_append_with_eventbox (GTK_TOOLBAR (toolbar),
					       zoom, _("Zoom"), NULL);

	gtk_signal_connect (
		GTK_OBJECT(toolbar), "orientation-changed",
		GTK_SIGNAL_FUNC (workbook_standard_toolbar_orient), wbcg);

	wbcg->standard_toolbar = toolbar;
	gtk_widget_show (toolbar);
#endif
}

static void
workbook_create_toolbars (WorkbookControlGUI *wbcg)
{
	workbook_create_standard_toolbar (wbcg);
	workbook_create_format_toolbar (wbcg);
	workbook_create_object_toolbar (wbcg);
}

static void
cb_cancel_input (GtkWidget *IGNORED, WorkbookControlGUI *wbcg)
{
	workbook_finish_editing (wbcg, FALSE);
}

static void
cb_accept_input (GtkWidget *IGNORED, WorkbookControlGUI *wbcg)
{
	workbook_finish_editing (wbcg, TRUE);
}

static gboolean
cb_editline_focus_in (GtkWidget *w, GdkEventFocus *event,
		      WorkbookControlGUI *wbcg)
{
	if (!wbcg->editing)
		workbook_start_editing_at_cursor (wbcg, FALSE, TRUE);

	return TRUE;
}

static int
wb_edit_key_pressed (GtkEntry *entry, GdkEventKey *event,
		     WorkbookControlGUI *wbcg)
{
	switch (event->keyval) {
	case GDK_Escape:
		workbook_finish_editing (wbcg, FALSE);
		return TRUE;

	case GDK_F4:
	{
		/* FIXME FIXME FIXME
		 * 1) Make sure that a cursor move after an F4
		 *    does not insert.
		 * 2) Handle calls for a range rather than a single cell.
		 */
		int end_pos = GTK_EDITABLE (entry)->current_pos;
		int start_pos;
		int row_status_pos, col_status_pos;
		gboolean abs_row = FALSE, abs_col = FALSE;

		/* Ignore this character */
		event->keyval = GDK_VoidSymbol;

		/* Only applies while editing */
		if (!wbcg->editing)
			return TRUE;

		/* Only apply do this for formulas */
		if (NULL == gnumeric_char_start_expr_p (entry->text_mb))
			return TRUE;

		/*
		 * Find the end of the current range
		 * starting from the current position.
		 * Don't bother validating.  The goal is the find the
		 * end.  We'll validate on the way back.
		 */
		if (entry->text[end_pos] == '$')
			++end_pos;
		while (isalpha (entry->text[end_pos]))
			++end_pos;
		if (entry->text[end_pos] == '$')
			++end_pos;
		while (isdigit ((unsigned char)entry->text[end_pos]))
			++end_pos;

		/*
		 * Try to find the begining of the current range
		 * starting from the end we just found
		 */
		start_pos = end_pos - 1;
		while (start_pos >= 0 && isdigit ((unsigned char)entry->text[start_pos]))
			--start_pos;
		if (start_pos == end_pos)
			return TRUE;

		row_status_pos = start_pos + 1;
		if ((abs_row = (entry->text[start_pos] == '$')))
			--start_pos;

		while (start_pos >= 0 && isalpha (entry->text[start_pos]))
			--start_pos;
		if (start_pos == end_pos)
			return TRUE;

		col_status_pos = start_pos + 1;
		if ((abs_col = (entry->text[start_pos] == '$')))
			--start_pos;

		/* Toggle the relative vs absolute flags */
		if (abs_col) {
			--end_pos;
			--row_status_pos;
			gtk_editable_delete_text (GTK_EDITABLE (entry),
						  col_status_pos-1, col_status_pos);
		} else {
			++end_pos;
			++row_status_pos;
			gtk_editable_insert_text (GTK_EDITABLE (entry), "$", 1,
						  &col_status_pos);
		}

		if (!abs_col) {
			if (abs_row) {
				--end_pos;
				gtk_editable_delete_text (GTK_EDITABLE (entry),
							  row_status_pos-1, row_status_pos);
			} else {
				++end_pos;
				gtk_editable_insert_text (GTK_EDITABLE (entry), "$", 1,
							  &row_status_pos);
			}
		}

		/* Do not select the current range, and do not change the position. */
		gtk_entry_set_position (entry, end_pos);
	}

	case GDK_KP_Up:
	case GDK_Up:
	case GDK_KP_Down:
	case GDK_Down:
		/* Ignore these keys.  The default behaviour is certainly
		   not what we want.  */
		/* FIXME: what is the proper way to stop the key getting to
		   the gtkentry?  */
		event->keyval = GDK_VoidSymbol;
		return TRUE;

	case GDK_KP_Enter:
	case GDK_Return:
		if (wbcg->editing) {
			if (event->state == GDK_CONTROL_MASK ||
			    event->state == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
				gboolean const is_array = (event->state & GDK_SHIFT_MASK);
				const char *text = gtk_entry_get_text (workbook_get_entry (wbcg));
				Sheet *sheet = wbcg->editing_sheet;
				EvalPos pos;

				/* Be careful to use the editing sheet */
				gboolean const trouble =
					cmd_area_set_text (WORKBOOK_CONTROL (wbcg),
						eval_pos_init (&pos, sheet, &sheet->cursor.edit_pos),
						text, is_array);

				/*
				 * If the assignment was successful finish
				 * editing but do NOT store the results
				 */
				if (!trouble)
					workbook_finish_editing (wbcg, FALSE);
				return TRUE;
			}

			/* Is this the right way to append a newline ?? */
			if (event->state == GDK_MOD1_MASK)
				gtk_entry_append_text (workbook_get_entry (wbcg), "\n");
		}
	default:
		return FALSE;
	}
}

static void
wb_jump_to_cell (GtkEntry *entry, WorkbookControlGUI *wbcg)
{
	const char *text = gtk_entry_get_text (entry);

	workbook_parse_and_jump (WORKBOOK_CONTROL (wbcg), text);
	wb_control_gui_focus_cur_sheet (wbcg);
}

static void
cb_workbook_debug_info (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	Range const * selection;

	if (gnumeric_debugging > 3) {
		summary_info_dump (wb->summary_info);

		if ((selection = selection_first_range (sheet, FALSE)) == NULL) {
			gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				_("Selection must be a single range"));
			return;
		}
	}

	if (style_debugging > 0) {
		printf ("Style list\n");
		sheet_styles_dump (sheet);
	}

	if (dependency_debugging > 0) {
		printf ("Dependencies\n");
		sheet_dump_dependencies (sheet);
	}
}

static void
cb_autofunction (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	GtkEntry *entry;
	gchar const *txt;

	if (wbcg->editing)
		return;

	entry = workbook_get_entry (wbcg);
	txt = gtk_entry_get_text (entry);
	if (strncmp (txt, "=", 1)) {
		workbook_start_editing_at_cursor (wbcg, TRUE, TRUE);
		gtk_entry_set_text (entry, "=");
		gtk_entry_set_position (entry, 1);
	} else {
		workbook_start_editing_at_cursor (wbcg, FALSE, TRUE);

		/* FIXME : This is crap!
		 * When the function druid is more complete use that.
		 */
		gtk_entry_set_position (entry, entry->text_length-1);
	}
}

static GtkWidget *
edit_area_button (WorkbookControlGUI *wbcg, gboolean sensitive,
		  GtkSignalFunc func, char const *pixmap)
{
	GtkWidget *button = gtk_button_new ();
	GtkWidget *pix = gnome_stock_pixmap_widget_new (
		GTK_WIDGET (wbcg->toplevel), pixmap);
	gtk_container_add (GTK_CONTAINER (button), pix);
	if (!sensitive)
		gtk_widget_set_sensitive (button, FALSE);
	GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (func), wbcg);

	return button;
}

static void
workbook_setup_edit_area (WorkbookControlGUI *wbcg)
{
	GtkWidget *pix, *box, *box2;
	GtkEntry *entry;

	wbcg->selection_descriptor     = gtk_entry_new ();

	workbook_edit_init (wbcg);
	entry = workbook_get_entry (wbcg);

	box           = gtk_hbox_new (0, 0);
	box2          = gtk_hbox_new (0, 0);

	gtk_widget_set_usize (wbcg->selection_descriptor, 100, 0);

	wbcg->cancel_button = edit_area_button (wbcg, FALSE,
		GTK_SIGNAL_FUNC (cb_cancel_input), GNOME_STOCK_BUTTON_CANCEL);
	wbcg->ok_button = edit_area_button (wbcg, FALSE,
		GTK_SIGNAL_FUNC (cb_accept_input), GNOME_STOCK_BUTTON_OK);

	/* Auto function */
	wbcg->func_button	= gtk_button_new ();
	pix = gnome_pixmap_new_from_xpm_d (equal_sign_xpm);
	gtk_container_add (GTK_CONTAINER (wbcg->func_button), pix);
	GTK_WIDGET_UNSET_FLAGS (wbcg->func_button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (wbcg->func_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_autofunction), wbcg);

	gtk_box_pack_start (GTK_BOX (box2), wbcg->selection_descriptor, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wbcg->cancel_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wbcg->ok_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wbcg->func_button, 0, 0, 0);

	/* Dependency + Style debugger */
	if (gnumeric_debugging > 9 ||
	    style_debugging > 0 || dependency_debugging > 0) {
		GtkWidget *deps_button = edit_area_button (wbcg, TRUE,
			GTK_SIGNAL_FUNC (cb_workbook_debug_info),
			GNOME_STOCK_PIXMAP_BOOK_RED);
		gtk_box_pack_start (GTK_BOX (box), deps_button, 0, 0, 0);
	}

	gtk_box_pack_start (GTK_BOX (box2), box, 0, 0, 0);
	gtk_box_pack_end   (GTK_BOX (box2), GTK_WIDGET (entry), 1, 1, 0);

	gtk_table_attach (GTK_TABLE (wbcg->table), box2,
			  0, 1, 0, 1,
			  GTK_FILL | GTK_EXPAND, 0, 0, 0);

	/* Do signal setup for the editing input line */
	gtk_signal_connect (GTK_OBJECT (entry), "focus-in-event",
			    GTK_SIGNAL_FUNC (cb_editline_focus_in),
			    wbcg);
	gtk_signal_connect (GTK_OBJECT (entry), "activate",
			    GTK_SIGNAL_FUNC (cb_accept_input),
			    wbcg);
	gtk_signal_connect (GTK_OBJECT (entry), "key_press_event",
			    GTK_SIGNAL_FUNC (wb_edit_key_pressed),
			    wbcg);

	/* Do signal setup for the status input line */
	gtk_signal_connect (GTK_OBJECT (wbcg->selection_descriptor), "activate",
			    GTK_SIGNAL_FUNC (wb_jump_to_cell), wbcg);
}

static int
wbcg_delete_event (GtkWidget *widget, GdkEvent *event, WorkbookControlGUI *wbcg)
{
	return wbcg_close_control (wbcg);
}

/*
 * We must not crash on focus=NULL. We're called like that as a result of
 * gtk_window_set_focus (toplevel, NULL) if the first sheet view is destroyed
 * just after being created. This happens e.g when we cancel a file import or
 * the import fails.
 */
static void
wbcg_set_focus (GtkWindow *window, GtkWidget *focus, WorkbookControlGUI *wbcg)
{
	if (focus && !window->focus_widget)
		wb_control_gui_focus_cur_sheet (wbcg);
}

static void
cb_notebook_switch_page (GtkNotebook *notebook, GtkNotebookPage *page,
			 guint page_num, WorkbookControlGUI *wbcg)
{
	/* Hang on to old sheet */
	Sheet *old_sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));

	/* Lookup sheet associated with the current notebook tab */
	Sheet *sheet;
	gboolean accept = TRUE;

	sheet = wb_control_gui_focus_cur_sheet (wbcg);

	/* While initializing adding the sheets will trigger page changes, but
	 * we do not actually want to change the focus sheet for the view
	 */
	if (wbcg->updating_ui)
		return;

	/* Remove the cell seletion cursor if it exists */
	if (old_sheet != NULL)
		sheet_destroy_cell_select_cursor (old_sheet, TRUE);

	if (wbcg->editing) {
		/* If we are not at a subexpression boundary then finish editing */
		accept = !workbook_editing_expr (wbcg);

		if (accept)
			workbook_finish_editing (wbcg, TRUE);
	}
	if (accept && wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)) != NULL) {
		/* force an update of the status and edit regions */
		sheet_flag_status_update_range (sheet, NULL);

		sheet_update (sheet);
	}
	wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);
}

static GtkObjectClass *parent_class;
static void
wbcg_destroy (GtkObject *obj)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (obj);

	/* Disconnect signals that would attempt to change things during
	 * destruction.
	 */
	if (wbcg->notebook != NULL)
		gtk_signal_disconnect_by_func (
			GTK_OBJECT (wbcg->notebook),
			GTK_SIGNAL_FUNC (cb_notebook_switch_page), wbcg);
	gtk_signal_disconnect_by_func (
		GTK_OBJECT (wbcg->toplevel),
		GTK_SIGNAL_FUNC (wbcg_set_focus), wbcg);

	workbook_auto_complete_destroy (wbcg);

	gtk_window_set_focus (GTK_WINDOW (wbcg->toplevel), NULL);

	if (!GTK_OBJECT_DESTROYED (GTK_OBJECT (wbcg->toplevel)))
		gtk_object_destroy (GTK_OBJECT (wbcg->toplevel));

	GTK_OBJECT_CLASS (parent_class)->destroy (obj);
}

static gboolean
cb_scroll_wheel_support (GtkWidget *w, GdkEventButton *event,
			 WorkbookControlGUI *wb)
{
	/* FIXME : now that we have made the split this is no longer true. */
	/* This is a stub routine to handle scroll wheel events
	 * Unfortunately the toplevel window is currently owned by the workbook
	 * rather than a workbook-view so we cannot really scroll things
	 * unless we scrolled all the views at once which is ugly.
	 */
#if 0
	if (event->button == 4)
	    puts("up");
	else if (event->button == 5)
	    puts("down");
#endif
	return FALSE;
}

static void
workbook_setup_sheets (WorkbookControlGUI *wbcg)
{
	GtkWidget *w = gtk_notebook_new ();
	wbcg->notebook = GTK_NOTEBOOK (w);
	gtk_signal_connect_after (GTK_OBJECT (wbcg->notebook), "switch_page",
		GTK_SIGNAL_FUNC (cb_notebook_switch_page), wbcg);

	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (wbcg->notebook), GTK_POS_BOTTOM);
	gtk_notebook_set_tab_border (GTK_NOTEBOOK (wbcg->notebook), 0);

	gtk_table_attach (GTK_TABLE (wbcg->table), GTK_WIDGET (wbcg->notebook),
			  0, 1, 1, 2,
			  GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
			  0, 0);
	gtk_widget_show (w);
}

#ifdef ENABLE_BONOBO
static void
setup_progress_bar (WorkbookControlGUI *wbcg)
{
	GtkProgressBar *progress_bar;
	BonoboControl  *control;

	progress_bar = (GTK_PROGRESS_BAR (gtk_progress_bar_new ()));

	gtk_progress_bar_set_orientation (
		progress_bar, GTK_PROGRESS_LEFT_TO_RIGHT);
	gtk_progress_bar_set_bar_style (
		progress_bar, GTK_PROGRESS_CONTINUOUS);

	wbcg->progress_bar = GTK_WIDGET (progress_bar);
	gtk_widget_show (wbcg->progress_bar);

	control = bonobo_control_new (wbcg->progress_bar);
	g_return_if_fail (control != NULL);

	bonobo_ui_component_object_set (
		wbcg->uic,
		"/status/Progress",
		bonobo_object_corba_objref (BONOBO_OBJECT (control)),
		NULL);
}
#endif

static void
cb_auto_expr_changed (GtkWidget *item, WorkbookControlGUI *wbcg)
{
	if (wbcg->updating_ui)
		return;

	wb_view_auto_expr (
		wb_control_view (WORKBOOK_CONTROL (wbcg)),
		gtk_object_get_data (GTK_OBJECT (item), "name"),
		gtk_object_get_data (GTK_OBJECT (item), "expr"));
}

static void
cb_select_auto_expr (GtkWidget *widget, GdkEventButton *event, Workbook *wbcg)
{
	/*
	 * WARNING * WARNING * WARNING
	 *
	 * Keep the functions in lower case.
	 * We currently register the functions in lower case and some locales
	 * (notably tr_TR) do not have the same encoding for tolower that
	 * locale C does.
	 *
	 * eg tolower ('I') != 'i'
	 * Which would break function lookup when looking up for funtion 'selectIon'
	 * when it wa sregistered as 'selection'
	 *
	 * WARNING * WARNING * WARNING
	 */
	static struct {
		char *displayed_name;
		char *function;
	} const quick_compute_routines [] = {
		{ N_("Sum"),   	       "sum(selection(0))" },
		{ N_("Min"),   	       "min(selection(0))" },
		{ N_("Max"),   	       "max(selection(0))" },
		{ N_("Average"),       "average(selection(0))" },
		{ N_("Count"),         "count(selection(0))" },
		{ NULL, NULL }
	};

	GtkWidget *menu;
	GtkWidget *item;
	int i;

	menu = gtk_menu_new ();

	for (i = 0; quick_compute_routines [i].displayed_name; i++) {
		item = gtk_menu_item_new_with_label (
			_(quick_compute_routines [i].displayed_name));
		gtk_object_set_data (GTK_OBJECT (item), "expr",
				     quick_compute_routines [i].function);
		gtk_object_set_data (GTK_OBJECT (item), "name",
				     _(quick_compute_routines [i].displayed_name));
		gtk_signal_connect (GTK_OBJECT (item), "activate",
				    GTK_SIGNAL_FUNC (cb_auto_expr_changed), wbcg);
		gtk_menu_append (GTK_MENU (menu), item);
		gtk_widget_show (item);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
}

/*
 * Sets up the autocalc label on the workbook.
 *
 * This code is more complex than it should for a number of
 * reasons:
 *
 *    1. GtkLabels flicker a lot, so we use a GnomeCanvas to
 *       avoid the unnecessary flicker
 *
 *    2. Using a Canvas to display a label is tricky, there
 *       are a number of ugly hacks here to do what we want
 *       to do.
 *
 * When GTK+ gets a flicker free label (Owen mentions that the
 * new repaint engine in GTK+ he is working on will be flicker free)
 * we can remove most of these hacks
 */
static void
workbook_setup_auto_calc (WorkbookControlGUI *wbcg)
{
	GtkWidget *canvas;
	GnomeCanvasGroup *root;
	GtkWidget *l, *frame;

	canvas = gnome_canvas_new ();

	l = gtk_label_new ("Info");
	gtk_widget_ensure_style (l);

	/* The canvas that displays text */
	root = GNOME_CANVAS_GROUP (GNOME_CANVAS (canvas)->root);
	wbcg->auto_expr_label = GNOME_CANVAS_ITEM (gnome_canvas_item_new (
		root, gnome_canvas_text_get_type (),
		"text",     "x",
		"x",        (double) 0,
		"y",        (double) 0,	/* FIXME :-) */
		"font_gdk", l->style->font,
		"anchor",   GTK_ANCHOR_NW,
		"fill_color", "black",
		NULL));
	gtk_widget_set_usize (
		GTK_WIDGET (canvas),
		gdk_text_measure (l->style->font, "W", 1) * 15, -1);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (frame), canvas);
#ifdef ENABLE_BONOBO
	{
		BonoboControl *control;

		control = bonobo_control_new (frame);
		g_return_if_fail (control != NULL);

		bonobo_ui_component_object_set (
			wbcg->uic,
			"/status/AutoExpr",
			bonobo_object_corba_objref (BONOBO_OBJECT (control)),
			NULL);
	}
#else
	gtk_box_pack_start (GTK_BOX (wbcg->appbar), frame, FALSE, TRUE, 0);
#endif
	gtk_signal_connect (GTK_OBJECT (canvas), "button_press_event",
			    GTK_SIGNAL_FUNC (cb_select_auto_expr), wbcg);

	gtk_object_unref (GTK_OBJECT (l));
	gtk_widget_show_all (frame);
}

/*
 * Sets up the status display area
 */
static void
workbook_setup_status_area (WorkbookControlGUI *wbcg)
{
	/*
	 * Create the GnomeAppBar
	 */
#ifdef ENABLE_BONOBO
	setup_progress_bar (wbcg);
#else
	wbcg->appbar = GNOME_APPBAR (
		gnome_appbar_new (TRUE, TRUE,
				  GNOME_PREFERENCES_USER));

	gnome_app_set_statusbar (GNOME_APP (wbcg->toplevel),
				 GTK_WIDGET (wbcg->appbar));
#endif

	/*
	 * Add the auto calc widgets.
	 */
	workbook_setup_auto_calc (wbcg);
}

void
workbook_control_gui_init (WorkbookControlGUI *wbcg,
			   WorkbookView *optional_view, Workbook *optional_wb)
{
	int sx, sy;

#ifdef ENABLE_BONOBO
	BonoboUIContainer *ui_container;
#endif
	GtkWidget *tmp;

#ifdef ENABLE_BONOBO
	tmp  = bonobo_window_new ("Gnumeric", "Gnumeric");
#else
	tmp  = gnome_app_new ("Gnumeric", "Gnumeric");
#endif
	wbcg->toplevel = GTK_WINDOW (tmp);
	wbcg->table    = gtk_table_new (0, 0, 0);
	wbcg->notebook = NULL;

	workbook_control_set_view (&wbcg->wb_control, optional_view, optional_wb);

	workbook_setup_edit_area (wbcg);

#ifndef ENABLE_BONOBO
	/* Do BEFORE setting up UI bits in the non-bonobo case */
	workbook_setup_status_area (wbcg);

	gnome_app_set_contents (GNOME_APP (wbcg->toplevel), wbcg->table);
	gnome_app_create_menus_with_data (GNOME_APP (wbcg->toplevel), workbook_menu, wbcg);
	gnome_app_install_menu_hints (GNOME_APP (wbcg->toplevel), workbook_menu);

	/* Get the menu items that will be enabled disabled based on
	 * workbook state.
	 */
	wbcg->menu_item_undo	  = workbook_menu_edit[0].widget;
	wbcg->menu_item_redo	  = workbook_menu_edit[1].widget;
	wbcg->menu_item_paste_special = workbook_menu_edit[6].widget;
#else
	bonobo_window_set_contents (BONOBO_WINDOW (wbcg->toplevel), wbcg->table);

	wbcg->uic = bonobo_ui_component_new_default ();

	ui_container = bonobo_ui_container_new ();
	bonobo_ui_container_set_win (ui_container, BONOBO_WINDOW (wbcg->toplevel));
	bonobo_ui_component_set_container (
		wbcg->uic, bonobo_object_corba_objref (BONOBO_OBJECT (ui_container)));

	bonobo_ui_component_add_verb_list_with_data (wbcg->uic, verbs, wbcg);

	bonobo_ui_util_set_ui (wbcg->uic, GNOME_DATADIR, "gnumeric.xml", "gnumeric");

	/* Do after setting up UI bits in the bonobo case */
	workbook_setup_status_area (wbcg);
#endif
	/* Create before registering verbs so that we can merge some extra. */
 	workbook_create_toolbars (wbcg);

	x_clipboard_bind_workbook (wbcg);	/* clipboard setup */
	wbcg_history_setup (wbcg);		/* Dynamic history menu items. */

	/* There is nothing to undo/redo yet */
	wbcg_undo_redo_labels (WORKBOOK_CONTROL (wbcg), NULL, NULL);

	/*
	 * Enable paste special, this assumes that the default is to
	 * paste from the X clipboard.  It will be disabled when
	 * something is cut.
	 */
	wbcg_paste_special_enable (WORKBOOK_CONTROL (wbcg), TRUE);

	/* We are not in edit mode */
	wbcg->select_abs_col = wbcg->select_abs_row = FALSE;
	wbcg->select_full_col = wbcg->select_full_row = FALSE;
	wbcg->select_single_cell = FALSE;
	wbcg->editing = FALSE;
	wbcg->editing_sheet = NULL;
	wbcg->editing_cell = NULL;

	gtk_signal_connect_after (
		GTK_OBJECT (wbcg->toplevel), "delete_event",
		GTK_SIGNAL_FUNC (wbcg_delete_event), wbcg);
	gtk_signal_connect_after (
		GTK_OBJECT (wbcg->toplevel), "set_focus",
		GTK_SIGNAL_FUNC (wbcg_set_focus), wbcg);
	gtk_signal_connect (GTK_OBJECT (wbcg->toplevel),
			    "button-release-event",
			    GTK_SIGNAL_FUNC (cb_scroll_wheel_support),
			    wbcg);
#if 0
	/* Enable toplevel as a drop target */

	gtk_drag_dest_set (wbcg->toplevel,
			   GTK_DEST_DEFAULT_ALL,
			   drag_types, n_drag_types,
			   GDK_ACTION_COPY);

	gtk_signal_connect (GTK_OBJECT (wb->toplevel),
			    "drag_data_received",
			    GTK_SIGNAL_FUNC (filenames_dropped), wb);
#endif

	/* Now that everything is initialized set the size */
	/* TODO : use gnome-config ? */
	gtk_window_set_policy (wbcg->toplevel, TRUE, TRUE, FALSE);
	sx = MAX (gdk_screen_width  () - 64, 600);
	sy = MAX (gdk_screen_height () - 64, 200);
	sx = (sx * 3) / 4;
	sy = (sy * 3) / 4;
	wbcg_size_pixels_set (WORKBOOK_CONTROL (wbcg), sx, sy);

	/* Init autosave */
	wbcg->autosave_timer = 0;
	wbcg->autosave_minutes = 0;
	wbcg->autosave_prompt = FALSE;

	gtk_widget_show_all (GTK_WIDGET (wbcg->toplevel));
}

static void
workbook_control_gui_ctor_class (GtkObjectClass *object_class)
{
	WorkbookControlClass *wbc_class = WORKBOOK_CONTROL_CLASS (object_class);

	g_return_if_fail (wbc_class != NULL);

	parent_class = gtk_type_class (workbook_control_get_type ());

	object_class->destroy		= wbcg_destroy;

	wbc_class->context_class.progress_set	= wbcg_progress_set;
	wbc_class->context_class.error.system	= wbcg_error_system;
	wbc_class->context_class.error.plugin	= wbcg_error_plugin;
	wbc_class->context_class.error.read	= wbcg_error_read;
	wbc_class->context_class.error.save	= wbcg_error_save;
	wbc_class->context_class.error.invalid	= wbcg_error_invalid;
	wbc_class->context_class.error.splits_array  = wbcg_error_splits_array;

	wbc_class->control_new		= wbcg_control_new;
	wbc_class->title_set		= wbcg_title_set;
	wbc_class->prefs_update		= wbcg_prefs_update;
	wbc_class->format_feedback	= wbcg_format_feedback;
	wbc_class->zoom_feedback	= wbcg_zoom_feedback;
	wbc_class->edit_line_set	= wbcg_edit_line_set;
	wbc_class->selection_descr_set	= wbcg_edit_selection_descr_set;
	wbc_class->auto_expr_value	= wbcg_auto_expr_value;

	wbc_class->sheet.add        = wbcg_sheet_add;
	wbc_class->sheet.remove	    = wbcg_sheet_remove;
	wbc_class->sheet.rename	    = wbcg_sheet_rename;
	wbc_class->sheet.focus	    = wbcg_sheet_focus;
	wbc_class->sheet.move	    = wbcg_sheet_move;
	wbc_class->sheet.remove_all = wbcg_sheet_remove_all;

	wbc_class->undo_redo.clear    = wbcg_undo_redo_clear;
	wbc_class->undo_redo.truncate = wbcg_undo_redo_truncate;
	wbc_class->undo_redo.pop      = wbcg_undo_redo_pop;
	wbc_class->undo_redo.push     = wbcg_undo_redo_push;
	wbc_class->undo_redo.labels   = wbcg_undo_redo_labels;

	wbc_class->paste.special_enable = wbcg_paste_special_enable;
	wbc_class->paste.from_selection = wbcg_paste_from_selection;
	wbc_class->claim_selection	= wbcg_claim_selection;
}

GNUMERIC_MAKE_TYPE(workbook_control_gui,
		   "WorkbookControlGUI",
		   WorkbookControlGUI,
		   workbook_control_gui_ctor_class, NULL,
		   workbook_control_get_type ());

WorkbookControl *
workbook_control_gui_new (WorkbookView *optional_view, Workbook *wb)
{
	WorkbookControlGUI *wbcg;
	WorkbookControl    *wbc;

	wbcg = gtk_type_new (workbook_control_gui_get_type ());
	workbook_control_gui_init (wbcg, optional_view, wb);

	wbc = WORKBOOK_CONTROL (wbcg);
	g_return_val_if_fail (!wbcg->updating_ui, wbc);

	wbcg->updating_ui = TRUE;
	workbook_control_sheets_init (wbc);
	wbcg->updating_ui = FALSE;

	return wbc;
}
