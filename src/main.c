/*
 * Gnumeric, the GNOME spreadsheet.
 *
 * Main file, startup code.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gnome.h>
#include "gnumeric.h"
#include "xml-io.h"
#ifdef ENABLE_BONOBO
#include "bonobo-io.h"
/* DO NOT include embeddable-grid.h.  It causes odd depends in the non-bonobo
 * case */
extern gboolean EmbeddableGridFactory_init (void);
#endif
#include "stf.h"
#include "main.h"
#include "plugin.h"
#include "format.h"
#include "formats.h"
#include "command-context.h"
#include "workbook.h"
#include "workbook-control-gui.h"
#include "sheet-object.h"
#include "number-match.h"
#include "main.h"
#include "expr-name.h"
#include "func.h"
#include "application.h"
#include "print-info.h"
#include "global-gnome-font.h"
#include "auto-format.h"
#include "style.h"
#include "style-color.h"

#include <gal/widgets/e-cursors.h>
#include <glade/glade.h>
#include <glade/glade-xml.h>

#ifdef HAVE_GUILE
#include <libguile.h>
#endif

#ifdef USE_WM_ICONS
#include <libgnomeui/gnome-window-icon.h>
#endif

#ifdef ENABLE_BONOBO
#include <bonobo.h>
#endif

/* The debugging level */
int gnumeric_debugging = 0;
int style_debugging = 0;
int dependency_debugging = 0;
int immediate_exit_flag = 0;
int print_debugging = 0;
gboolean initial_workbook_open_complete = FALSE;
extern gboolean libole2_debug;

static char *dump_file_name = NULL;
static char *startup_glade_file = NULL;
static const char **startup_files = NULL;
static int gnumeric_show_version = FALSE;
char *gnumeric_lib_dir = GNUMERIC_LIBDIR;
char *gnumeric_data_dir = GNUMERIC_DATADIR;

poptContext ctx;

const struct poptOption gnumeric_popt_options [] = {
	{ "version", 'v', POPT_ARG_NONE, &gnumeric_show_version, 0,
	  N_("Display Gnumeric's version"), NULL  },
	{ "lib-dir", 'L', POPT_ARG_STRING, &gnumeric_lib_dir, 0,
	  N_("Set the root library directory"), NULL  },
	{ "data-dir", 'D', POPT_ARG_STRING, &gnumeric_data_dir, 0,
	  N_("Adjust the root data directory"), NULL  },

	{ "dump-func-defs", '\0', POPT_ARG_STRING, &dump_file_name, 0,
	  N_("Dumps the function definitions"),   N_("FILE") },

	{ "debug", '\0', POPT_ARG_INT, &gnumeric_debugging, 0,
	  N_("Enables some debugging functions"), N_("LEVEL") },

	{ "debug_styles", '\0', POPT_ARG_INT, &style_debugging, 0,
	  N_("Enables some style related debugging functions"), N_("LEVEL") },
	{ "debug_deps", '\0', POPT_ARG_INT, &dependency_debugging, 0,
	  N_("Enables some dependency related debugging functions"), N_("LEVEL") },
	{ "debug_print", '\0', POPT_ARG_INT, &print_debugging, 0,
	  N_("Enables some print debugging behavior"), N_("LEVEL") },

	{ "quit", '\0', POPT_ARG_NONE, &immediate_exit_flag, 0,
	  N_("Exit immediately after loading the selected books (useful for testing)."), NULL },

	{ "debug_ole", '\0', POPT_ARG_NONE,
	    &libole2_debug, 0,
	  N_("Enables extra consistency checking while reading ole files"),
	  NULL  },

	{ NULL, '\0', 0, NULL, 0 }
};

#include "ranges.h"

/*
 * FIXME: We hardcode the GUI command context. Change once we are able
 * to tell whether we are in GUI or not.
 */
static void
gnumeric_main (void *closure, int argc, char *argv [])
{
	gboolean opened_workbook = FALSE;
	int i;
	WorkbookControl *context;

	/* Make stdout line buffered - we only use it for debug info */
	setvbuf (stdout, NULL, _IOLBF, 0);

	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnumeric_arg_parse (argc, argv);

	if (gnumeric_show_version) {
		printf (_("gnumeric version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			GNUMERIC_VERSION, GNUMERIC_DATADIR, GNUMERIC_LIBDIR);
		return;
	}
#ifdef USE_WM_ICONS
	gnome_window_icon_set_default_from_file (GNOME_ICONDIR"/gnome-gnumeric.png");
#endif

	application_init ();
	string_init ();
	style_init ();
	gnumeric_color_init ();
	format_match_init ();
	format_color_init ();
	e_cursors_init ();
	auto_format_init ();
	functions_init ();
	expr_name_init ();
	print_init ();
	sheet_object_register ();

	/* The statically linked in file formats */
	xml_init ();
	stf_init ();
#ifdef ENABLE_BONOBO
	gnumeric_bonobo_io_init ();
#endif

	global_gnome_font_init ();

	/* Ignore Shift for accelerators to avoid problems with different keyboard layouts
	 * that change the shift state of various keys.
	 */
	gtk_accelerator_set_default_mod_mask (gtk_accelerator_get_default_mod_mask() & ~GDK_SHIFT_MASK);

	/* Glade */
	glade_gnome_init ();
	if (startup_glade_file)
		glade_xml_new (startup_glade_file, NULL);

	if (dump_file_name) {
		function_dump_defs (dump_file_name);
		exit (0);
	}

#ifdef ENABLE_BONOBO
#if 0
	/* Activate object factories and init connections to POA */
	if (!WorkbookFactory_init ())
		g_warning (_("Could not initialize Workbook factory"));
#endif

	if (!EmbeddableGridFactory_init ())
		g_warning (_("Could not initialize EmbeddableGrid factory"));
#endif

	/* Load selected files */
	if (ctx)
		startup_files = poptGetArgs (ctx);
	else
		startup_files = NULL;

#ifdef ENABLE_BONOBO
	bonobo_activate ();
#endif
	context = workbook_control_gui_new (NULL, NULL);
	plugins_init (COMMAND_CONTEXT (context));
	if (startup_files)
		for (i = 0; startup_files [i]  && !initial_workbook_open_complete ; i++) {
			if (workbook_read (context, startup_files [i]) != NULL)
				opened_workbook = TRUE;

			/* FIXME: we need to mask input events correctly here */
			/* Show something coherent */
			while (gtk_events_pending () &&
			       !initial_workbook_open_complete)
				gtk_main_iteration ();
		}
	if (ctx)
		poptFreeContext (ctx);

	/* If we were intentionally short circuited exit now */
	if (!initial_workbook_open_complete && !immediate_exit_flag) {
		initial_workbook_open_complete = TRUE;
		if (!opened_workbook)
			workbook_sheet_add (wb_control_workbook (context),
					    NULL, FALSE);
		gtk_main ();
	}

	print_shutdown ();
	auto_format_shutdown ();
	e_cursors_shutdown ();
	format_match_finish ();
	format_color_shutdown ();
	gnumeric_color_shutdown ();
	style_shutdown ();

	global_gnome_font_shutdown ();
	plugins_shutdown ();

	gnome_config_drop_all ();
}

#ifdef HAVE_GUILE
gboolean
has_gnumeric_been_compiled_with_guile_support (void)
{
	return TRUE;
}

int
main (int argc, char *argv [])
{
#if 0
	int fd;

	/* FIXME:
	 *
	 * We segfault inside scm_boot_guile if any of stdin, stdout or stderr
	 * is missing. Up to gnome-libs 1.0.56, libgnorba closes stdin, so the
	 * segfault *will* happen when gnumeric is activated that way. This fix
	 * will make sure fd 0, 1 and 2 are valid. Enable when we know where
	 * guile init ends up. */

	fd = open("/dev/null", O_RDONLY);
	if (fd == 0)
		fdopen (fd, "r");
	else
		close (fd);
	if (fd <= 2) {
		for (;;) {
			fd = open("/dev/null", O_WRONLY);
			if (fd <= 2)
				fdopen (fd, "w");
			else {
				close (fd);
				break;
			}
		}
	}
#endif

	scm_boot_guile (argc, argv, gnumeric_main, 0);
	return 0;
}
#else
gboolean
has_gnumeric_been_compiled_with_guile_support (void)
{
	return FALSE;
}

int
main (int argc, char *argv [])
{
	gnumeric_main (0, argc, argv);
	return 0;
}
#endif
