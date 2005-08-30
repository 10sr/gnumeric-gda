/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Gnumeric, the GNOME spreadsheet.
 *
 * Main file, startup code.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "libgnumeric.h"

#include "application.h"
#include "stf.h"
#include "gnm-format.h"
#include "command-context.h"
#include "command-context-stderr.h"
#include "workbook.h"
#include "sheet-object.h"
#include "number-match.h"
#include "expr-name.h"
#include "func.h"
#include "print-info.h"
#include "style.h"
#include "mstyle.h"
#include "style-color.h"
#include "str.h"
#include "dependent.h"
#include "sheet-autofill.h"
#include "xml-io.h"
#include "cell.h"
#include "clipboard.h"
#include "value.h"
#include "expr.h"
#include "parse-util.h"
#include "rendered-value.h"
#include "gnumeric-gconf.h"
#include "gnm-plugin.h"
#include "mathfunc.h"
#include "hlink.h"
#include <goffice/goffice.h>
#include <goffice/app/go-plugin.h>
#include <goffice/app/go-plugin-service.h>
#include <goffice/app/go-cmd-context.h>
#include <goffice/app/go-plugin-loader-module.h>

#include <locale.h>
#include <glade/glade.h>

/* The debugging level */
int gnumeric_debugging = 0;
int dependency_debugging = 0;
int expression_sharing_debugging = 0;
int immediate_exit_flag = 0;
int print_debugging = 0;
gboolean initial_workbook_open_complete = FALSE;
char *x_geometry;

/**
 * gnm_pre_parse_init :
 * @gnumeric_binary : argv[0]
 *
 * Initialization to be done before cmd line arguments are handled.
 **/
void
gnm_pre_parse_init (char const* gnumeric_binary)
{
	g_set_prgname (gnumeric_binary);

	/* Make stdout line buffered - we only use it for debug info */
	setvbuf (stdout, NULL, _IOLBF, 0);

	gutils_init ();

	bindtextdomain (GETTEXT_PACKAGE, gnm_locale_dir ());
	bindtextdomain (GETTEXT_PACKAGE "-functions", gnm_locale_dir ());
	textdomain (GETTEXT_PACKAGE);

	/* Force all of the locale segments to update from the environment.
	 * Unless we do this they will default to C
	 */
	setlocale (LC_ALL, "");
}

#if 0
static void
gnumeric_check_for_components (void)
{
	OAF_ServerInfoList *result;

	result = oaf_query ("repo_ids.has('IDL::1.0')", NULL, NULL);

	g_return_if_fail (result != NULL);

	CORBA_free (info_list);
}
#endif

extern void libgoffice_init (void);
/*
 * FIXME: We hardcode the GUI command context. Change once we are able
 * to tell whether we are in GUI or not.
 */
void
gnm_common_init (gboolean fast)
{
	libgoffice_init ();
	plugin_service_define ("function_group",
		&plugin_service_function_group_get_type);
	plugin_service_define ("ui",
		&plugin_service_ui_get_type);
	go_plugin_loader_module_register_version ("gnumeric", GNUMERIC_VERSION);

	g_object_new (GNM_APP_TYPE, NULL);
	mathfunc_init ();
	gnm_string_init ();
	gnm_style_init ();
	gnm_conf_init (fast);
	value_init ();
	parse_util_init ();
	expr_init ();
	cell_init ();
	clipboard_init ();
	dependent_types_init ();
	rendered_value_init ();
	gnumeric_color_init ();
	style_init ();
	format_match_init ();
	/* e_cursors_init (); */
	functions_init ();
	print_init ();
	autofill_init ();
	sheet_objects_init ();

	/* make sure that all hlink types are registered */
	gnm_hlink_cur_wb_get_type ();
	gnm_hlink_url_get_type ();
	gnm_hlink_email_get_type ();
	gnm_hlink_external_get_type ();

	/* The statically linked in file formats */
	xml_init ();
	stf_init ();

	glade_init ();
}

/**
 * TODO : do we really want this here ?
 * seems like a better fit in main-application.c
 **/
int
gnm_dump_func_defs (char const* filename, int dump_type)
{
	int retval;
	GOCmdContext *cc = cmd_context_stderr_new ();

	gnm_plugins_init (cc);
	if ((retval = cmd_context_stderr_get_status (COMMAND_CONTEXT_STDERR (cc))) == 0)
		function_dump_defs (filename, dump_type);

	return retval;
}

void
gnm_shutdown (void)
{
	GSList *plugin_states;

	gnm_app_release_pref_dialog ();
	gnm_app_clipboard_clear (TRUE);

	plugin_states = go_plugins_shutdown ();
	if (NULL != plugin_states) {
		gnm_gconf_set_plugin_file_states (plugin_states);
		go_conf_sync (NULL);
	}

	print_shutdown ();
	functions_shutdown ();
	/* e_cursors_shutdown (); */
	format_match_finish ();
	style_shutdown ();
	gnumeric_color_shutdown ();
	rendered_value_shutdown ();
	dependent_types_shutdown ();
	clipboard_shutdown ();
	cell_shutdown ();
	expr_shutdown ();
	parse_util_shutdown ();
	value_shutdown ();
	gnm_conf_shutdown ();
	gnm_style_shutdown ();
	gnm_string_shutdown ();
	libgoffice_shutdown ();
	plugin_services_shutdown ();
	g_object_unref (gnm_app_get_app ());
	gutils_shutdown ();
}
