/**
 * boot.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 **/
#include <stdio.h>
#include <sys/stat.h>

#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "file.h"
#include "main.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

#include "excel.h"
#include "ms-summary.h"
#include "boot.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/* Used to toggle debug messages on & off */
/*
 * As a convention
 * 0 = quiet, no experimental features.
 * 1 = enable experimental features
 * >1 increasing levels of detail.
 */
/* Enables debugging mesgs while reading excel workbooks */
gint ms_excel_read_debug = 0;
/* Enables debugging mesgs while reading excel functions */
gint ms_excel_formula_debug = 0;
/* Enables debugging mesgs while reading excel colors & patterns */
gint ms_excel_color_debug = 0;
/* Enables debugging mesgs while reading excel charts */
gint ms_excel_chart_debug = 0;
/* Enables debugging mesgs while writing excel workbooks */
gint ms_excel_write_debug = 0;
/* Enables debugging mesgs while reading excel objects */
gint ms_excel_object_debug = 0;

MsExcelReadGbFn ms_excel_read_gb = NULL;

gboolean excel_file_probe (FileOpener const *fo, const char *filename);
void excel_file_open (FileOpener const *fo, IOContext *context, WorkbookView *new_wb_view, const char *filename);
void excel98_file_save (FileSaver const *fs, IOContext *context, WorkbookView *wb_view, const char *filename);
void excel95_file_save (FileSaver const *fs, IOContext *context, WorkbookView *wb_view, const char *filename);

gboolean
excel_file_probe (FileOpener const *fo, const char *filename)
{
	MsOle    *file;

	if (ms_ole_open (&file, filename) == MS_OLE_ERR_OK) {
		MsOleErr     result;
		MsOleStream *stream;

		result = ms_ole_stream_open (&stream, file, "/", "workbook", 'r');
		ms_ole_stream_close (&stream);
		if (result == MS_OLE_ERR_OK) {
			ms_ole_destroy (&file);
			return TRUE;
		}

		result = ms_ole_stream_open (&stream, file, "/", "book", 'r');
		ms_ole_stream_close (&stream);
		ms_ole_destroy (&file);
		if (result == MS_OLE_ERR_OK)
			return TRUE;
	}
	return FALSE;
}

/*
 * excel_file_open
 * @context:   	Command context
 * @wb:    	    Workbook
 * @filename:  	File name
 * @user_data:  ignored
 *
 * Load en excel workbook.
 */
void
excel_file_open (FileOpener const *fo, IOContext *context,
                 WorkbookView *new_wb_view, const char *filename)
{
	MsOleErr  ole_error;
	MsOle	 *f;

	ole_error = ms_ole_open (&f, filename);
	if (ole_error != MS_OLE_ERR_OK) {
		ms_ole_destroy (&f);
		/* FIXME : We need a more detailed message from
		 * ole_open */
		gnumeric_io_error_read (context, "");
		return;
	}

	puts (filename);
	ms_excel_read_workbook (context, new_wb_view, f);
	if (!gnumeric_io_error_occurred (context)) {
		Workbook *wb = wb_view_workbook (new_wb_view);
		ms_summary_read (f, wb->summary_info);

		if (ms_excel_read_debug > 0)
			summary_info_dump (wb->summary_info);

		if (ms_excel_read_gb) {
			if (!ms_excel_read_gb (context, wb, f))
				g_warning ("Failed to read Basic scripts");
		}
	}

	ms_ole_destroy (&f);
}

/*
 * Here's why the state which is carried from excel_check_write to
 * ms_excel_write_workbook is void *: The state is actually an
 * ExcelWorkbook * as defined in ms-excel-write.h. But we can't
 * import that definition here: There's a different definition of
 * ExcelWorkbook in ms-excel-read.h.
 */
static void
excel_save (IOContext *context, WorkbookView *wb_view, const char *filename,
            MsBiffVersion ver)
{
	Workbook *wb = wb_view_workbook (wb_view);
	MsOle *f;
	MsOleErr result;
	void *state = NULL;

	if (g_file_exists (filename)) {
		gnumeric_io_error_save (context,
			 _("Saving over old files disabled for safety"));
		return;
	}

	if (ms_excel_check_write (context, &state, wb_view, ver) != 0) {
		gnumeric_io_error_unknown (context);
		return;
	}

	result = ms_ole_create (&f, filename);

	if (result != MS_OLE_ERR_OK) {
		char *str = g_strdup_printf ("%s %s",
					     _("Can't open"),
					     filename);
		gnumeric_io_error_save (context, str);

		ms_ole_destroy (&f);
		ms_excel_write_free_state (state);
		g_free (str);
		return;
	}

	ms_excel_write_workbook (context, f, state, ver);

	ms_summary_write (f, wb->summary_info);

	ms_ole_destroy (&f);
}

void
excel98_file_save (FileSaver const *fs, IOContext *context,
                   WorkbookView *wb_view, const char *filename)
{
	excel_save (context, wb_view, filename, MS_BIFF_V8);
}

void
excel95_file_save (FileSaver const *fs, IOContext *context,
                   WorkbookView *wb_view, const char *filename)
{
	excel_save (context, wb_view, filename, MS_BIFF_V7);
}
