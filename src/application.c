/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * application.c: Manage the data common to all workbooks
 *
 * Author:
 *     Jody Goldberg <jody@gnome.org>
 */
#include <gnumeric-config.h>
#include <string.h>
#include "gnumeric.h"
#include "application.h"

#include "clipboard.h"
#include "selection.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook-priv.h" /* For Workbook::name */
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-private.h"
#include "sheet-object.h"
#include "auto-correct.h"
#include "gutils.h"
#include "ranges.h"
#include "sheet-object.h"
#include "pixmaps/gnumeric-stock-pixbufs.h"
#include "commands.h"

#include <gnumeric-gconf.h>
#include <goffice/app/go-doc.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-file.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkselection.h>
#include <glib/gi18n-lib.h>

#define GNM_APP(o)		(G_TYPE_CHECK_INSTANCE_CAST((o), GNM_APP_TYPE, GnmApp))
#define GNM_APP_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k),	 GNM_APP_TYPE, GnmAppClass))
#define IS_GNM_APP(o)		(G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_APP_TYPE))
#define IS_GNM_APP_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE((k),	 GNM_APP_TYPE))

enum {
	APPLICATION_PROP_0,
	APPLICATION_PROP_FILE_HISTORY_LIST
};
/* Signals */
enum {
	WORKBOOK_ADDED,
	WORKBOOK_REMOVED,
	WINDOW_LIST_CHANGED,
	CUSTOM_UI_ADDED,
	CUSTOM_UI_REMOVED,
	CLIPBOARD_MODIFIED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

struct _GnmApp {
	GObject  base;

	/* Clipboard */
	SheetView	*clipboard_sheet_view;
	GnmCellRegion	*clipboard_copied_contents;
	GnmRange	*clipboard_cut_range;

	/* History for file menu */
	GSList           *history_list;

	/* Others */
	GtkWidget       *pref_dialog;

	GList		*workbook_list;
};

typedef struct {
	GObjectClass     parent;

	void (*workbook_added)      (GnmApp *gnm_app, Workbook *wb);
	void (*workbook_removed)    (GnmApp *gnm_app, Workbook *wb);
	void (*window_list_changed) (GnmApp *gnm_app);
	void (*custom_ui_added)	    (GnmApp *gnm_app, GnmAppExtraUI *ui);
	void (*custom_ui_removed)   (GnmApp *gnm_app, GnmAppExtraUI *ui);
	void (*clipboard_modified)  (GnmApp *gnm_app);
} GnmAppClass;

static GObjectClass *parent_klass;
static GnmApp *app;

GObject *
gnm_app_get_app (void)
{
	return G_OBJECT (app);
}

/**
 * gnm_app_workbook_list_add :
 * @wb :
 *
 * Add @wb to the application's list of workbooks.
 **/
void
gnm_app_workbook_list_add (Workbook *wb)
{
	g_return_if_fail (IS_WORKBOOK (wb));
	g_return_if_fail (app != NULL);

	app->workbook_list = g_list_prepend (app->workbook_list, wb);
	g_signal_connect (G_OBJECT (wb),
		"notify::uri",
		G_CALLBACK (_gnm_app_flag_windows_changed), NULL);
	_gnm_app_flag_windows_changed ();
	g_signal_emit (G_OBJECT (app), signals [WORKBOOK_ADDED], 0, wb);
}

/**
 * gnm_app_workbook_list_remove :
 * @wb :
 *
 * Remove @wb from the application's list of workbooks.
 **/
void
gnm_app_workbook_list_remove (Workbook *wb)
{
	g_return_if_fail (wb != NULL);
	g_return_if_fail (app != NULL);

	app->workbook_list = g_list_remove (app->workbook_list, wb);
	g_signal_handlers_disconnect_by_func (G_OBJECT (wb),
		G_CALLBACK (_gnm_app_flag_windows_changed), NULL);
	_gnm_app_flag_windows_changed ();
	g_signal_emit (G_OBJECT (app), signals [WORKBOOK_REMOVED], 0, wb);
}

GList *
gnm_app_workbook_list (void)
{
	g_return_val_if_fail (app != NULL, NULL);

	return app->workbook_list;
}

/**
 * gnm_app_clipboard_clear:
 *
 * Clear and free the contents of the clipboard if it is
 * not empty.
 */
void
gnm_app_clipboard_clear (gboolean drop_selection)
{
	g_return_if_fail (app != NULL);

	if (app->clipboard_copied_contents) {
		cellregion_unref (app->clipboard_copied_contents);
		app->clipboard_copied_contents = NULL;
	}
	if (app->clipboard_sheet_view != NULL) {
		sv_unant (app->clipboard_sheet_view);

		g_signal_emit (G_OBJECT (app), signals [CLIPBOARD_MODIFIED], 0);

		sv_weak_unref (&(app->clipboard_sheet_view));

		/* Release the selection */
		if (drop_selection) {
#warning "FIXME: 1: this doesn't belong here.  2: it is not multihead safe."
			gtk_selection_owner_set (NULL,
						 GDK_SELECTION_PRIMARY,
						 GDK_CURRENT_TIME);
			gtk_selection_owner_set (NULL,
						 GDK_SELECTION_CLIPBOARD,
						 GDK_CURRENT_TIME);
		}
	}
}

void
gnm_app_clipboard_invalidate_sheet (Sheet *sheet)
{
	/* Clear the cliboard to avoid dangling references to the deleted sheet */
	if (sheet == gnm_app_clipboard_sheet_get ())
		gnm_app_clipboard_clear (TRUE);
	else if (app->clipboard_copied_contents)
		cellregion_invalidate_sheet (app->clipboard_copied_contents, sheet);
}

void
gnm_app_clipboard_unant (void)
{
	g_return_if_fail (app != NULL);

	if (app->clipboard_sheet_view != NULL)
		sv_unant (app->clipboard_sheet_view);
}

/**
 * gnm_app_clipboard_cut_copy:
 *
 * @wbc   : the workbook control that requested the operation.
 * @is_cut: is this a cut or a copy.
 * @sheet : The source sheet for the copy.
 * @area  : A single rectangular range to be copied.
 * @animate_cursor : Do we want ot add an animated cursor around things.
 *
 * When Cutting we
 *   Clear and free the contents of the clipboard and save the sheet and area
 *   to be cut.  DO NOT ACTUALLY CUT!  Paste will move the region if this was a
 *   cut operation.
 *
 * When Copying we
 *   Clear and free the contents of the clipboard and COPY the designated region
 *   into the clipboard.
 *
 * we need to pass @wbc as a control rather than a simple command-context so
 * that the control can claim the selection.
 **/
void
gnm_app_clipboard_cut_copy (WorkbookControl *wbc, gboolean is_cut,
			    SheetView *sv, GnmRange const *area,
			    gboolean animate_cursor)
{
	Sheet *sheet;

	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (area != NULL);
	g_return_if_fail (app != NULL);

	gnm_app_clipboard_clear (FALSE);
	sheet = sv_sheet (sv);
	g_free (app->clipboard_cut_range);
	app->clipboard_cut_range = range_dup (area);
	sv_weak_ref (sv, &(app->clipboard_sheet_view));

	if (!is_cut)
		app->clipboard_copied_contents =
			clipboard_copy_range (sheet, area);
	if (animate_cursor) {
		GList *l = g_list_append (NULL, (gpointer)area);
		sv_ant (sv, l);
		g_list_free (l);
	}

	if (wb_control_claim_selection (wbc)) {
		g_signal_emit (G_OBJECT (app), signals [CLIPBOARD_MODIFIED], 0);
	} else {
		gnm_app_clipboard_clear (FALSE);
		g_warning ("Unable to set selection ?");
	}
}

/** gnm_app_clipboard_cut_copy_obj :
 * @wbc : #WorkbookControl
 * @is_cut : 
 * @sv : #SheetView
 * @objects : a list of #SheetObject which is freed
 *
 * Different than copying/cutting a region, this can actually cuts an object
 **/
void
gnm_app_clipboard_cut_copy_obj (WorkbookControl *wbc, gboolean is_cut,
				SheetView *sv, GSList *objects)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (objects != NULL);
	g_return_if_fail (app != NULL);

	gnm_app_clipboard_clear (FALSE);
	g_free (app->clipboard_cut_range);
	app->clipboard_cut_range = NULL;
	sv_weak_ref (sv, &(app->clipboard_sheet_view));
	app->clipboard_copied_contents 
		= clipboard_copy_obj (sv_sheet (sv), objects);
	if (is_cut) {
		cmd_objects_delete (wbc, objects, _("Cut Object"));
		objects = NULL;
	}
	if (wb_control_claim_selection (wbc)) {
		g_signal_emit (G_OBJECT (app), signals [CLIPBOARD_MODIFIED], 0);
	} else {
		gnm_app_clipboard_clear (FALSE);
		g_warning ("Unable to set selection ?");
	}
	g_slist_free (objects);
}

gboolean
gnm_app_clipboard_is_empty (void)
{
	g_return_val_if_fail (app != NULL, TRUE);

	return app->clipboard_sheet_view == NULL;
}

gboolean
gnm_app_clipboard_is_cut (void)
{
	g_return_val_if_fail (app != NULL, FALSE);

	if (app->clipboard_sheet_view != NULL)
		return app->clipboard_copied_contents ? FALSE : TRUE;
	return FALSE;
}

Sheet *
gnm_app_clipboard_sheet_get (void)
{
	g_return_val_if_fail (app != NULL, NULL);

	if (app->clipboard_sheet_view == NULL)
		return NULL;
	return sv_sheet (app->clipboard_sheet_view);
}

SheetView *
gnm_app_clipboard_sheet_view_get (void)
{
	g_return_val_if_fail (app != NULL, NULL);
	return app->clipboard_sheet_view;
}

GnmCellRegion *
gnm_app_clipboard_contents_get (void)
{
	g_return_val_if_fail (app != NULL, NULL);
	return app->clipboard_copied_contents;
}

GnmRange const *
gnm_app_clipboard_area_get (void)
{
	g_return_val_if_fail (app != NULL, NULL);
	/*
	 * Only return the range if the sheet has been set.
	 * The range will still contain data even after
	 * the clipboard has been cleared so we need to be extra
	 * safe and only return a range if there is a valid selection
	 */
	if (app->clipboard_sheet_view != NULL)
		return app->clipboard_cut_range;
	return NULL;
}

Workbook *
gnm_app_workbook_get_by_name (char const *name,
			      char const *ref_uri)
{
	Workbook *wb;
	char *filename = NULL;

	if (name == NULL || *name == 0)
		return NULL;

	/* Try as URI.  */
	wb = gnm_app_workbook_get_by_uri (name);
	if (wb)
		goto out;

	filename = g_filename_from_utf8 (name, -1, NULL, NULL, NULL);

	/* Try as absolute filename.  */
	if (filename && g_path_is_absolute (filename)) {
		char *uri = go_filename_to_uri (filename);
		if (uri) {
			wb = gnm_app_workbook_get_by_uri (uri);
			g_free (uri);
		}
		if (wb)
			goto out;
	}

	if (filename && ref_uri) {
		char *rel_uri = go_url_encode (filename, 1);
		char *uri = go_url_resolve_relative (ref_uri, rel_uri);
		g_free (rel_uri);
		if (uri) {
			wb = gnm_app_workbook_get_by_uri (uri);
			g_free (uri);
		}
		if (wb)
			goto out;
	}

 out:
	g_free (filename);
	return wb;
}

struct wb_uri_closure {
	Workbook *wb;
	char const *uri;
};

static gboolean
cb_workbook_uri (Workbook * wb, gpointer closure)
{
	struct wb_uri_closure *dat = closure;
	char const *wb_uri = go_doc_get_uri (GO_DOC (wb));

	if (wb_uri && strcmp (wb_uri, dat->uri) == 0) {
		dat->wb = wb;
		return FALSE;
	}
	return TRUE;
}

Workbook *
gnm_app_workbook_get_by_uri (char const *uri)
{
	struct wb_uri_closure closure;

	g_return_val_if_fail (uri != NULL, NULL);

	closure.wb = NULL;
	closure.uri = uri;
	gnm_app_workbook_foreach (&cb_workbook_uri, &closure);

	return closure.wb;
}

gboolean
gnm_app_workbook_foreach (GnmWbIterFunc cback, gpointer data)
{
	GList *l;

	g_return_val_if_fail (app != NULL, FALSE);

	for (l = app->workbook_list; l; l = l->next){
		Workbook *wb = l->data;

		if (!(*cback)(wb, data))
			return FALSE;
	}
	return TRUE;
}

struct wb_index_closure
{
	Workbook *wb;
	int index;	/* 1 based */
};

static gboolean
cb_workbook_index (Workbook *wb, gpointer closure)
{
	struct wb_index_closure *dat = closure;
	if (--(dat->index) == 0) {
		dat->wb = wb;
		return FALSE;
	}
	return TRUE;
}

Workbook *
gnm_app_workbook_get_by_index (int i)
{
	struct wb_index_closure closure;
	closure.wb = NULL;
	closure.index = i;
	gnm_app_workbook_foreach (&cb_workbook_index, &closure);

	return closure.wb;
}

double
gnm_app_display_dpi_get (gboolean horizontal)
{
	return horizontal ? gnm_app_prefs->horizontal_dpi : gnm_app_prefs->vertical_dpi;
}

double
gnm_app_dpi_to_pixels (void)
{
	return MIN (gnm_app_prefs->horizontal_dpi,
		    gnm_app_prefs->vertical_dpi) / 72.;
}

/**
 * gnm_app_history_get_list:
 *
 * creating it if necessary.
 *
 * Return value: the list./
 **/
GSList const *
gnm_app_history_get_list (gboolean force_reload)
{
        gint max_entries;
	GSList const *ptr;
	GSList *res = NULL;

	g_return_val_if_fail (app != NULL, NULL);

	if (app->history_list != NULL) {
		if (force_reload) {
			GSList *tmp = app->history_list;
			app->history_list = NULL;
			go_slist_free_custom (tmp, g_free);
		} else
			return app->history_list;
	}

	max_entries = gnm_app_prefs->file_history_max;
	for (ptr = gnm_app_prefs->file_history_files;
	     ptr != NULL && max_entries-- > 0 ; ptr = ptr->next)
		res = g_slist_prepend (res, g_strdup (ptr->data));
	return app->history_list = g_slist_reverse (res);;
}

/**
 * application_history_update_list:
 * @uri:
 *
 * Adds @uri to the application's history of files.
 **/
void
gnm_app_history_add (char const *uri)
{
        gint max_entries;
	GSList *exists;
	GSList **ptr;

	g_return_if_fail (uri != NULL);
	g_return_if_fail (app != NULL);

	/* return if file history max length is 0, avoids a critical */
	if (gnm_app_prefs->file_history_max == 0)
		return; 
	/* force a reload in case max_entries has changed */
	gnm_app_history_get_list (TRUE);
	exists = g_slist_find_custom (app->history_list,
				      uri, go_str_compare);

	if (exists != NULL) {
		/* its already the top of the stack no need to do anything */
		if (exists == app->history_list)
			return;

		/* remove the other instance */
		g_free (exists->data);
		app->history_list = g_slist_delete_link (app->history_list, exists);
	}

	app->history_list = g_slist_prepend (app->history_list, g_strdup (uri));

	/* clip the list if it is too long */
	max_entries = gnm_app_prefs->file_history_max;
	ptr = &(app->history_list);
	while (*ptr != NULL && max_entries-- > 0)
		ptr = &((*ptr)->next);
	if (*ptr != NULL) {
		go_slist_free_custom (*ptr, g_free);
		*ptr = NULL;
	}

	g_object_notify (G_OBJECT (app), "file-history-list");
	gnm_gconf_set_file_history_files (
		go_string_slist_copy (app->history_list));
	go_conf_sync (NULL);
}

gboolean gnm_app_use_auto_complete	  (void) { return gnm_app_prefs->auto_complete; }
gboolean gnm_app_live_scrolling	  (void) { return gnm_app_prefs->live_scrolling; }
int	 gnm_app_auto_expr_recalc_lag (void) { return gnm_app_prefs->recalc_lag; }
gboolean gnm_app_use_transition_keys  (void) { return gnm_app_prefs->transition_keys; }
void     gnm_app_set_transition_keys  (gboolean state)
{
	((GnmAppPrefs *)gnm_app_prefs)->transition_keys = state;
}

gpointer
gnm_app_get_pref_dialog (void)
{
	g_return_val_if_fail (app != NULL, NULL);
	return app->pref_dialog;
}

void
gnm_app_set_pref_dialog (gpointer dialog)
{
	g_return_if_fail (app != NULL);
	app->pref_dialog = dialog;
}

void
gnm_app_release_pref_dialog (void)
{
	g_return_if_fail (app != NULL);
	if (app->pref_dialog)
		gtk_widget_destroy (app->pref_dialog);
}

static void
gnumeric_application_finalize (GObject *obj)
{
	GnmApp *application = GNM_APP (obj);

	g_slist_foreach (application->history_list, (GFunc)g_free, NULL);
	g_slist_free (application->history_list);
	application->history_list = NULL;

	g_free (application->clipboard_cut_range);
	application->clipboard_cut_range = NULL;

	app = NULL;
	G_OBJECT_CLASS (parent_klass)->finalize (obj);
}

static void
gnumeric_application_get_property (GObject *obj, guint param_id,
				   GValue *value, GParamSpec *pspec)
{
	GnmApp *application = GNM_APP (obj);
	switch (param_id) {
	case APPLICATION_PROP_FILE_HISTORY_LIST:
		g_value_set_pointer (value, application->history_list);
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		 break;
	}
}

static void
gnm_app_class_init (GObjectClass *gobject_klass)
{
	parent_klass = g_type_class_peek_parent (gobject_klass);

	/* Object class method overrides */
	gobject_klass->finalize = gnumeric_application_finalize;
	gobject_klass->get_property = gnumeric_application_get_property;
	g_object_class_install_property (gobject_klass, APPLICATION_PROP_FILE_HISTORY_LIST,
		g_param_spec_pointer ("file-history-list", "File History List",
			"A GSlist of filenames that have been read recently",
			GSF_PARAM_STATIC | G_PARAM_READABLE));

	signals [WORKBOOK_ADDED] = g_signal_new ("workbook_added",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, workbook_added),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1, WORKBOOK_TYPE);
	signals [WORKBOOK_REMOVED] = g_signal_new ("workbook_removed",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, workbook_removed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [WINDOW_LIST_CHANGED] = g_signal_new ("window-list-changed",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, window_list_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	signals [CUSTOM_UI_ADDED] = g_signal_new ("custom-ui-added",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, custom_ui_added),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [CUSTOM_UI_REMOVED] = g_signal_new ("custom-ui-removed",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, custom_ui_removed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [CLIPBOARD_MODIFIED] = g_signal_new ("clipboard_modified",
		GNM_APP_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmAppClass, clipboard_modified),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

static void
gnm_app_init (GObject *obj)
{
	GnmApp *gnm_app = GNM_APP (obj);

	gnm_app->clipboard_copied_contents = NULL;
	gnm_app->clipboard_sheet_view = NULL;

	gnm_app->workbook_list = NULL;

	app = gnm_app;
}

GSF_CLASS (GnmApp, gnm_app,
	   gnm_app_class_init, gnm_app_init,
	   G_TYPE_OBJECT);
 
/**********************************************************************/
static GSList *extra_uis = NULL;

GnmAction *
gnm_action_new (char const *id, char const *label,
		char const *icon_name, gboolean always_available,
		GnmActionHandler handler)
{
	GnmAction *res = g_new0 (GnmAction, 1);
	res->id		= g_strdup (id);
	res->label	= g_strdup (label);
	res->icon_name	= g_strdup (icon_name);
	res->always_available = always_available;
	res->handler	= handler;
	return res;
}

void
gnm_action_free (GnmAction *action)
{
	if (NULL != action) {
		g_free (action->id);
		g_free (action->label);
		g_free (action->icon_name);
		g_free (action);
	}
}

GnmAppExtraUI *
gnm_app_add_extra_ui (GSList *actions, char *layout,
		      char const *domain,
		      gpointer user_data)
{
	GnmAppExtraUI *extra_ui = g_new0 (GnmAppExtraUI, 1);
	extra_uis = g_slist_prepend (extra_uis, extra_ui);
	extra_ui->actions = actions;
	extra_ui->layout = layout;
	extra_ui->user_data = user_data;
	g_signal_emit (G_OBJECT (app), signals [CUSTOM_UI_ADDED], 0, extra_ui);
	return extra_ui;
}

void
gnm_app_remove_extra_ui (GnmAppExtraUI *extra_ui)
{
	g_signal_emit (G_OBJECT (app), signals [CUSTOM_UI_REMOVED], 0, extra_ui);
}

void
gnm_app_foreach_extra_ui (GFunc func, gpointer data)
{
	g_slist_foreach (extra_uis, func, data);
}

/**********************************************************************/

static gint windows_update_timer = -1;
static gboolean
cb_flag_windows_changed (void)
{
	g_signal_emit (G_OBJECT (app), signals [WINDOW_LIST_CHANGED], 0);
	windows_update_timer = -1;
	return FALSE;
}

/**
 * _gnm_app_flag_windows_changed :
 *
 * An internal utility routine to flag a regeneration of the window lists
 **/
void
_gnm_app_flag_windows_changed (void)
{
	if (windows_update_timer < 0)
		windows_update_timer = g_timeout_add (100,
			(GSourceFunc)cb_flag_windows_changed, NULL);
}
