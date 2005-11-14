/* vim: set sw=8: */
/*
 * GUI-file.c:
 *
 * Authors:
 *    Jon K Hellan (hellan@acm.org)
 *    Zbigniew Chyla (cyba@gnome.pl)
 *    Andreas J. Guelzow (aguelzow@taliesin.ca)
 *
 * Port to Maemo:
 * 	Eduardo Lima  (eduardo.lima@indt.org.br)
 * 	Renato Araujo (renato.filho@indt.org.br) 
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "gui-file.h"

#include "gui-util.h"
#include "dialogs.h"
#include "sheet.h"
#include "application.h"
#include "command-context.h"
#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include "workbook-priv.h"
#include "gnumeric-gconf.h"

#include <goffice/gtk/go-charmap-sel.h>
#include <goffice/app/io-context.h>
#include <goffice/utils/go-file.h>

#include <gtk/gtkcombobox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktable.h>
#include <gtk/gtkcombo.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkfilechooserdialog.h>
#include <glade/glade.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#ifdef USE_HILDON
	#include <hildon-lgpl/hildon-widgets/hildon-app.h>
	#include <hildon-lgpl/hildon-widgets/hildon-appview.h>
	#include <hildon-fm/hildon-widgets/hildon-file-chooser-dialog.h>
#endif

typedef struct {
	GOCharmapSel *go_charmap_sel;
	GtkWidget	*charmap_label;
	GList *openers;
} file_format_changed_cb_data;

 

static gint
file_opener_description_cmp (gconstpointer a, gconstpointer b)
{
	GOFileOpener const *fo_a = a, *fo_b = b;

	return g_utf8_collate (go_file_opener_get_description (fo_a),
			       go_file_opener_get_description (fo_b));
}

static gint
file_saver_description_cmp (gconstpointer a, gconstpointer b)
{
	GOFileSaver const *fs_a = a, *fs_b = b;

	return g_utf8_collate (go_file_saver_get_description (fs_a),
			       go_file_saver_get_description (fs_b));
}

static void
make_format_chooser (GList *list, GtkComboBox *combo)
{
	GList *l;

	/* Make format chooser */
	for (l = list; l != NULL; l = l->next) {
		gchar const *descr;

		if (!l->data)
			descr = _("Automatically detected");
		else if (IS_GO_FILE_OPENER (l->data))
			descr = go_file_opener_get_description (
						GO_FILE_OPENER (l->data));
		else
			descr = go_file_saver_get_description (
				                GO_FILE_SAVER (l->data));

		gtk_combo_box_append_text (combo, descr);
	}
}

/* Show view in a wbcg. Use current or new wbcg according to policy */
void
gui_wb_view_show (WorkbookControlGUI *wbcg, WorkbookView *wbv)
{
	WorkbookControlGUI *new_wbcg = NULL;
	Workbook *tmp_wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));

#ifdef USE_HILDON
	if (workbook_is_dirty (tmp_wb)) {
		switch (wbcg_show_save_dialog (wbcg, tmp_wb, FALSE)) {

			case GTK_RESPONSE_YES:
			case GNM_RESPONSE_SAVE_ALL:
				gui_file_save (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)));
				break;

			case GTK_RESPONSE_NO:
			case GNM_RESPONSE_DISCARD_ALL:
				/* Do nothing */
				break;
			
			default:  /* CANCEL */
				return;
		}
	}

	g_object_ref (wbcg);
	g_object_unref (tmp_wb);
	wb_control_set_view (WORKBOOK_CONTROL (wbcg), wbv, NULL);
	wb_control_init_state (WORKBOOK_CONTROL (wbcg));
#else
	if (workbook_is_pristine (tmp_wb)) {
		g_object_ref (wbcg);
		g_object_unref (tmp_wb);
		wb_control_set_view (WORKBOOK_CONTROL (wbcg), wbv, NULL);
		wb_control_init_state (WORKBOOK_CONTROL (wbcg));
	} else {
		GdkScreen *screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
		WorkbookControl *new_wbc =
			wb_control_wrapper_new (WORKBOOK_CONTROL (wbcg),
						wbv, NULL, screen);
		new_wbcg = WORKBOOK_CONTROL_GUI (new_wbc);

		wbcg_copy_toolbar_visibility (new_wbcg, wbcg);
	}
#endif

	sheet_update (wb_view_cur_sheet	(wbv));
}

gboolean
gui_file_read (WorkbookControlGUI *wbcg, char const *uri,
	       GOFileOpener const *optional_format, gchar const *optional_encoding)
{
	IOContext *io_context;
	WorkbookView *wbv;

	go_cmd_context_set_sensitive (GO_CMD_CONTEXT (wbcg), FALSE);
	io_context = gnumeric_io_context_new (GO_CMD_CONTEXT (wbcg));
	wbv = wb_view_new_from_uri (uri, optional_format, io_context, 
				    optional_encoding);

	if (gnumeric_io_error_occurred (io_context) ||
	    gnumeric_io_warning_occurred (io_context))
		gnumeric_io_error_display (io_context);

	g_object_unref (G_OBJECT (io_context));
	go_cmd_context_set_sensitive (GO_CMD_CONTEXT (wbcg), TRUE);

	if (wbv != NULL) {
		gui_wb_view_show (wbcg, wbv);
		return TRUE;
	}
	return FALSE;
}

static void
file_format_changed_cb (GtkComboBox *format_combo,
			file_format_changed_cb_data *data)
{
	GOFileOpener *fo = g_list_nth_data (data->openers,
		gtk_combo_box_get_active (format_combo));
	gboolean is_sensitive = fo != NULL && go_file_opener_is_encoding_dependent (fo);

	gtk_widget_set_sensitive (GTK_WIDGET (data->go_charmap_sel), is_sensitive);
	gtk_widget_set_sensitive (data->charmap_label, is_sensitive);
}


static gint
file_opener_find_by_id (GList *openers, char const *id)
{
	GList *l;
	gint i = 0;

	if (id == NULL)
		return 0;
	
	for (l = openers; l != NULL; l = l->next, i++) {
		if (IS_GO_FILE_OPENER (l->data) &&
		    strcmp (id, go_file_opener_get_id(l->data)) == 0)
			return i;
	}

	return 0;
}

/*
 * Suggests automatic file type recognition, but lets the user choose an
 * import filter for selected file.
 */
void
gui_file_open (WorkbookControlGUI *wbcg, char const *default_format)
{
	GList *openers;
	GtkFileChooser *fsel;
	GtkComboBox *format_combo;
	GtkWidget *go_charmap_sel;
	file_format_changed_cb_data data;
	gint opener_default;
	char const *title;
	char *uri = NULL;
	const char *encoding = NULL;
	GOFileOpener *fo = NULL;
	Workbook *workbook = wb_control_workbook (WORKBOOK_CONTROL (wbcg));

	openers = g_list_sort (g_list_copy (get_file_openers ()),
			       file_opener_description_cmp);
	/* NULL represents automatic file type recognition */
	openers = g_list_prepend (openers, NULL);
	opener_default = file_opener_find_by_id (openers, default_format);
	title = (opener_default == 0)
		? _("Load file") 
		: (go_file_opener_get_description 
		   (g_list_nth_data (openers, opener_default)));
	data.openers = openers;

	/* Make charmap chooser */
	go_charmap_sel = go_charmap_sel_new (GO_CHARMAP_SEL_TO_UTF8);
	data.go_charmap_sel = GO_CHARMAP_SEL(go_charmap_sel);
	data.charmap_label = gtk_label_new_with_mnemonic (_("Character _encoding:"));

	/* Make format chooser */
	format_combo = GTK_COMBO_BOX (gtk_combo_box_new_text ());
	make_format_chooser (openers, format_combo);
	g_signal_connect (G_OBJECT (format_combo), "changed",
                          G_CALLBACK (file_format_changed_cb), &data);
	gtk_combo_box_set_active (format_combo, opener_default);
	gtk_widget_set_sensitive (GTK_WIDGET (format_combo), opener_default == 0);
	file_format_changed_cb (format_combo, &data);

#ifdef USE_HILDON
	fsel = GTK_FILE_CHOOSER (hildon_file_chooser_dialog_new (wbcg_toplevel (wbcg), GTK_FILE_CHOOSER_ACTION_OPEN));
#else
	fsel = GTK_FILE_CHOOSER
		(g_object_new (GTK_TYPE_FILE_CHOOSER_DIALOG,
			       "action", GTK_FILE_CHOOSER_ACTION_OPEN,
			       "local-only", FALSE,
			       "title", _("Select a file"),
			       NULL));
	gtk_dialog_add_buttons (GTK_DIALOG (fsel),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OPEN, GTK_RESPONSE_OK,
				NULL);
#endif
	gtk_dialog_set_default_response (GTK_DIALOG (fsel), GTK_RESPONSE_OK);

	/* Add Templates bookmark */
	{
		char *templates = g_build_filename (gnm_sys_data_dir (), "templates", NULL);
		gtk_file_chooser_add_shortcut_folder (fsel, templates, NULL);
		g_free (templates);
	}

	/* Start in the same directory as the current workbook.  */
	gtk_file_chooser_select_uri (fsel, workbook_get_uri (workbook));
	gtk_file_chooser_unselect_all (fsel);

	/* Filters */
	{	
		GtkFileFilter *filter;
		GList *l;

		filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (filter, _("All Files"));
		gtk_file_filter_add_pattern (filter, "*");
		gtk_file_chooser_add_filter (fsel, filter);

		filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (filter, _("Spreadsheets"));
		for (l = openers->next; l; l = l->next) {
			GOFileOpener const *o = l->data;
			GSList const *ptr;
			for (ptr = go_file_opener_get_suffixes	(o); ptr != NULL ; ptr = ptr->next) {
				char *pattern = g_strconcat ("*.", ptr->data, NULL);
				gtk_file_filter_add_pattern (filter, pattern);
				g_free (pattern);
			}
			for (ptr = go_file_opener_get_mimes	(o); ptr != NULL ; ptr = ptr->next)
				gtk_file_filter_add_mime_type (filter, ptr->data);

		}

		gtk_file_chooser_add_filter (fsel, filter);
		/* Make this filter the default */
		gtk_file_chooser_set_filter (fsel, filter);
	}

	{
		GtkWidget *label;
		GtkWidget *box = gtk_table_new (2, 2, FALSE);

		gtk_table_attach (GTK_TABLE (box),
				  GTK_WIDGET (format_combo),
				  1, 2, 0, 1, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 5, 2);
		label = gtk_label_new_with_mnemonic (_("File _type:"));
		gtk_table_attach (GTK_TABLE (box), label,
				  0, 1, 0, 1, GTK_SHRINK | GTK_FILL, GTK_SHRINK, 5, 2);
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (format_combo));

		gtk_table_attach (GTK_TABLE (box),
				  go_charmap_sel,
				  1, 2, 1, 2, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 5, 2);
		gtk_table_attach (GTK_TABLE (box), data.charmap_label,
				  0, 1, 1, 2, GTK_SHRINK | GTK_FILL, GTK_SHRINK, 5, 2);
		gtk_label_set_mnemonic_widget (GTK_LABEL (data.charmap_label),
					       go_charmap_sel);

		gtk_file_chooser_set_extra_widget (fsel, box);
	}

	/* Show file selector */
	if (!go_gtk_file_sel_dialog (wbcg_toplevel (wbcg), GTK_WIDGET (fsel)))
		goto out;

	uri = gtk_file_chooser_get_uri (fsel);
	encoding = go_charmap_sel_get_encoding (GO_CHARMAP_SEL (go_charmap_sel));
	fo = g_list_nth_data (openers, gtk_combo_box_get_active (format_combo));

 out:
	gtk_widget_destroy (GTK_WIDGET (fsel));
	g_list_free (openers);

	if (uri) {
		/* Make sure dialog goes away right now.  */
		while (g_main_context_iteration (NULL, FALSE));

		gui_file_read (wbcg, uri, fo, encoding);
		g_free (uri);
	}
}

static gboolean
check_multiple_sheet_support_if_needed (GOFileSaver *fs,
					GtkWindow *parent,
					WorkbookView *wb_view)
{
	gboolean ret_val = TRUE;

	if (go_file_saver_get_save_scope (fs) != FILE_SAVE_WORKBOOK &&
	    gnm_app_prefs->file_ask_single_sheet_save) {
		GList *sheets;
		gchar *msg = _("Selected file format doesn't support "
			       "saving multiple sheets in one file.\n"
			       "If you want to save all sheets, save them "
			       "in separate files or select different file format.\n"
			       "Do you want to save only current sheet?");

		sheets = workbook_sheets (wb_view_workbook (wb_view));
		if (g_list_length (sheets) > 1) {
			ret_val = go_gtk_query_yes_no (parent, TRUE, msg);
		}
		g_list_free (sheets);
	}
	return (ret_val);
}

gboolean
gui_file_save_as (WorkbookControlGUI *wbcg, WorkbookView *wb_view)
{
	GList *savers = NULL, *l;
	GtkFileChooser *fsel;
	GtkComboBox *format_combo;
	GOFileSaver *fs;
	gboolean success  = FALSE;
	gchar const *wb_uri;
	char *uri;

	g_return_val_if_fail (wbcg != NULL, FALSE);

	for (l = get_file_savers (); l; l = l->next) {
		if ((l->data == NULL) || 
		    (go_file_saver_get_save_scope (GO_FILE_SAVER (l->data)) 
		     != FILE_SAVE_RANGE))
			savers = g_list_prepend (savers, l->data);
	}
	savers = g_list_sort (savers, file_saver_description_cmp);

#ifdef USE_HILDON
	fsel = GTK_FILE_CHOOSER (hildon_file_chooser_dialog_new (wbcg_toplevel (wbcg), GTK_FILE_CHOOSER_ACTION_SAVE));
#else
	fsel = GTK_FILE_CHOOSER
		(g_object_new (GTK_TYPE_FILE_CHOOSER_DIALOG,
			       "action", GTK_FILE_CHOOSER_ACTION_SAVE,
			       "local-only", FALSE,
			       "title", _("Select a file"),
			       NULL));
	gtk_dialog_add_buttons (GTK_DIALOG (fsel),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_SAVE, GTK_RESPONSE_OK,
				NULL);
#endif

	gtk_dialog_set_default_response (GTK_DIALOG (fsel), GTK_RESPONSE_OK);

	/* Filters */
	{	
		GtkFileFilter *filter;
		GList *l;

		filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (filter, _("All Files"));
		gtk_file_filter_add_pattern (filter, "*");
		gtk_file_chooser_add_filter (fsel, filter);

		filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (filter, _("Spreadsheets"));
		for (l = savers->next; l; l = l->next) {
			GOFileSaver *fs = l->data;
			const char *ext = go_file_saver_get_extension (fs);
			const char *mime = go_file_saver_get_mime_type (fs);

			if (mime)
				gtk_file_filter_add_mime_type (filter, mime);

#warning "FIXME: do we get all extensions?"
			/* Well, we don't get things we cannot save.  */
			if (ext) {
				char *pattern = g_strconcat ("*.", ext, NULL);
				gtk_file_filter_add_pattern (filter, pattern);
				g_free (pattern);
			}
		}
		gtk_file_chooser_add_filter (fsel, filter);
		/* Make this filter the default */
		gtk_file_chooser_set_filter (fsel, filter);
	}

	{
		GtkWidget *box = gtk_hbox_new (FALSE, 2);
		GtkWidget *label = gtk_label_new_with_mnemonic (_("File _type:"));
		format_combo = GTK_COMBO_BOX (gtk_combo_box_new_text ());
		make_format_chooser (savers, format_combo);

		gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 6);
		gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (format_combo), FALSE, TRUE, 6);
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (format_combo));

		gtk_file_chooser_set_extra_widget (fsel, box);
	}

	/* Set default file saver */
	fs = wbcg->current_saver;
	if (fs == NULL)
		fs = workbook_get_file_saver (wb_view_workbook (wb_view));
	if (fs == NULL || g_list_find (savers, fs) == NULL)
		fs = go_file_saver_get_default ();

	gtk_combo_box_set_active (format_combo, g_list_index (savers, fs));

	/* Set default file name */
	wb_uri = workbook_get_uri (wb_view_workbook (wb_view));
	if (wb_uri != NULL) {
		char *basename = go_basename_from_uri (wb_uri);
		char *dot = basename ? strrchr (basename, '.') : NULL;

		gtk_file_chooser_set_uri (fsel, wb_uri);
		gtk_file_chooser_unselect_all (fsel);

		/* Remove extension.  */
		if (dot && dot != basename)
			*dot = 0;
		gtk_file_chooser_set_current_name (fsel, basename);
		g_free (basename);
	}

	while (1) {
		char *uri2 = NULL;

		/* Show file selector */
		if (!go_gtk_file_sel_dialog (wbcg_toplevel (wbcg), GTK_WIDGET (fsel)))
			goto out;
		fs = g_list_nth_data (savers, gtk_combo_box_get_active (format_combo));
		if (!fs)
			goto out;
		uri = gtk_file_chooser_get_uri (fsel);
		if (!go_url_check_extension (uri,
					     go_file_saver_get_extension (fs), 
					     &uri2) &&
		    !go_gtk_query_yes_no (GTK_WINDOW (fsel),
					  TRUE,
					  _("The given file extension does not match the"
					    " chosen file type. Do you want to use this name"
					    " anyway?"))) {
			g_free (uri);
			g_free (uri2);
			uri = NULL;
			continue;
		}

		g_free (uri);
		uri = uri2;

		if (go_gtk_url_is_writeable (GTK_WINDOW (fsel), uri,
					     gnm_app_prefs->file_overwrite_default_answer))
			break;

		g_free (uri);
	}

	wb_view_preferred_size (wb_view, GTK_WIDGET (wbcg->notebook)->allocation.width,
				GTK_WIDGET (wbcg->notebook)->allocation.height);

	success = check_multiple_sheet_support_if_needed (fs, GTK_WINDOW (fsel), wb_view);
	if (success) {
		/* Destroy early so no-one can repress the Save button.  */
		gtk_widget_destroy (GTK_WIDGET (fsel));
		fsel = NULL;
		success = wb_view_save_as (wb_view, fs, uri, GO_CMD_CONTEXT (wbcg));
		if (success)
			wbcg->current_saver = fs;
	}

	g_free (uri);

 out:
	if (fsel)
		gtk_widget_destroy (GTK_WIDGET (fsel));
	g_list_free (savers);

	return success;
}

gboolean
gui_file_save (WorkbookControlGUI *wbcg, WorkbookView *wb_view)
{
	Workbook *wb;

	wb_view_preferred_size (wb_view,
	                        GTK_WIDGET (wbcg->notebook)->allocation.width,
	                        GTK_WIDGET (wbcg->notebook)->allocation.height);

	wb = wb_view_workbook (wb_view);
	if (wb->file_format_level < FILE_FL_AUTO)
		return gui_file_save_as (wbcg, wb_view);
	else
		return wb_view_save (wb_view, GO_CMD_CONTEXT (wbcg));
}
