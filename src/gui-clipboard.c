/*
 * gui-clipboard.c: Implements the X11 based copy/paste operations
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include "gui-clipboard.h"
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "clipboard.h"
#include "dependent.h"
#include "eval.h"
#include "selection.h"
#include "application.h"
#include "render-ascii.h"
#include "workbook-control-gui-priv.h"
#include "workbook.h"
#include "ranges.h"
#include "cell-comment.h"
#include "commands.h"
#include "xml-io.h"
#include "value.h"
#include "dialog-stf.h"
#include "stf-parse.h"

#include <gnome.h>
#include <locale.h>
#include <string.h>
#include <ctype.h>

/* The name of our clipboard atom and the 'magic' info number */
#define GNUMERIC_ATOM_NAME "GNUMERIC_CLIPBOARD_XML"
#define GNUMERIC_ATOM_INFO 2000

/* The name of the TARGETS atom (don't change unless you know what you are doing!) */
#define TARGETS_ATOM_NAME "TARGETS"

static CellRegion *
x_selection_to_cell_region (WorkbookControlGUI *wbcg, const char *src, int len)
{
	DialogStfResult_t *dialogresult;
	CellRegion *cr = NULL;
	CellRegion *crerr;
	char *data;
	char *c;

	data = g_new (char, len + 1);
	memcpy (data, src, len);
	data[len] = 0;

	crerr         = g_new (CellRegion, 1);
	crerr->list   = NULL;
	crerr->cols   = -1;
	crerr->rows   = -1;
	crerr->styles = NULL;

	if (!stf_parse_convert_to_unix (data)) {
		g_free (data);
		g_warning (_("Error while trying to pre-convert clipboard data"));
		return crerr;
	}

	if ((c = stf_parse_is_valid_data (data)) != NULL) {
		char *message;
		
		message = g_strdup_printf (_("The data on the clipboard does not seem to be valid text.\nThe character '%c' (ASCII decimal %d) was encountered"),
					   *c, (int) *c);
		g_warning (message);
		g_free (message);

		g_free (data);
		
		return crerr;
	}

	dialogresult = stf_dialog (wbcg, "clipboard", data);

	if (dialogresult != NULL) {
		GSList *iterator;
		int col, rowcount;

		cr = stf_parse_region (dialogresult->parseoptions, dialogresult->newstart);

		if (cr == NULL) {
			g_free (data);
			g_warning (_("Parse error while trying to parse data into cellregion"));
			return crerr;
		}

		iterator = dialogresult->formats;
		col = 0;
		rowcount = stf_parse_get_rowcount (dialogresult->parseoptions, dialogresult->newstart);
		while (iterator) {
			StyleRegion *content = g_new (StyleRegion, 1);
			MStyle *style = mstyle_new ();
			Range range;

			mstyle_set_format (style, iterator->data);

			range.start.col = col;
			range.start.row = 0;
			range.end.col   = col;
			range.end.row   = rowcount;

			content->style = style;
			content->range  = range;

			cr->styles = g_list_prepend (cr->styles, content);

			iterator = g_slist_next (iterator);

			col++;
		}

		stf_dialog_result_free (dialogresult);
	} else {
		return crerr;
	}

	g_free (crerr);
	g_free (data);

	return cr;
}

/**
 * x_selection_received:
 *
 * Invoked when the selection has been received by our application.
 * This is triggered by a call we do to gtk_selection_convert.
 */
static void
x_selection_received (GtkWidget *widget, GtkSelectionData *sel, guint time,
		      WorkbookControlGUI *wbcg)
{
	GdkAtom atom_targets  = gdk_atom_intern (TARGETS_ATOM_NAME, FALSE);
	GdkAtom atom_gnumeric = gdk_atom_intern (GNUMERIC_ATOM_NAME, FALSE);
	PasteTarget *pt = wbcg->clipboard_paste_callback_data;
	CellRegion *content = NULL;
	gboolean region_pastable = FALSE;
	gboolean free_closure = FALSE;
	WorkbookControl	*wbc = WORKBOOK_CONTROL (wbcg);

	if (sel->target == atom_targets) { /* The data is a list of atoms */
		GdkAtom *atoms = (GdkAtom *) sel->data;
		gboolean gnumeric_format;
		int atom_count = (sel->length / sizeof (GdkAtom));
		int i;

		/* Nothing on clipboard? */
		if (sel->length < 0) {

			if (wbcg->clipboard_paste_callback_data != NULL) {
				g_free (wbcg->clipboard_paste_callback_data);
				wbcg->clipboard_paste_callback_data = NULL;
			}
			return;
		}

		/*
		 * Iterate over the atoms and try to find the gnumeric atom
		 */
		gnumeric_format = FALSE;
		for (i = 0; i < atom_count; i++) {
			if (atoms[i] == atom_gnumeric) {
				/* Hooya! other app == gnumeric */
				gnumeric_format = TRUE;
				break;
			}
		}

		/* NOTE : We don't release the date resources
		 * (wbcg->clipboard_paste_callback_data), the
		 * reason for this is that we will actually call ourself
		 * again (indirectly trough the gtk_selection_convert
		 * and that call _will_ free the data (and also needs it).
		 * So we won't release anything.
		 */

		/* If another instance of gnumeric put this data on the clipboard
		 * request the data in gnumeric XML format. If not, just
		 * request it in string format
		 */
		if (gnumeric_format)
			gtk_selection_convert (GTK_WIDGET (wbcg->toplevel),
					       GDK_SELECTION_PRIMARY,
					       atom_gnumeric, time);
		else
			gtk_selection_convert (GTK_WIDGET (wbcg->toplevel),
					       GDK_SELECTION_PRIMARY,
					       GDK_SELECTION_TYPE_STRING, time);

	} else if (sel->target == atom_gnumeric) { /* The data is the gnumeric specific XML interchange format */

		if (gnumeric_xml_read_selection_clipboard (wbc, &content, sel->data) == 0)
			region_pastable = TRUE;

	} else {  /* The data is probably in String format */
		region_pastable = TRUE;

		/* Did X provide any selection? */
		if (sel->length < 0) {
			region_pastable = FALSE;
			free_closure = TRUE;
		} else
			content = x_selection_to_cell_region (wbcg, sel->data, sel->length);
	}

	if (region_pastable) {
		/*
		 * if the conversion from the X selection -> a cellregion
		 * was canceled this may have content sized -1,-1
		 */
		if (content->cols > 0 && content->rows > 0)
			cmd_paste_copy (wbc, pt, content);

		/* Release the resources we used */
		if (sel->length >= 0)
			clipboard_release (content);
	}

	if (region_pastable || free_closure) {
		/* Remove our used resources */
		if (wbcg->clipboard_paste_callback_data != NULL) {
			g_free (wbcg->clipboard_paste_callback_data);
			wbcg->clipboard_paste_callback_data = NULL;
		}
	}
}

/**
 * x_selection_handler:
 *
 * Callback invoked when another application requests we render the selection.
 */
static void
x_selection_handler (GtkWidget *widget, GtkSelectionData *selection_data,
		     guint info, guint time, WorkbookControl *wbc)
{
	gboolean content_needs_free = FALSE;
	CellRegion *clipboard = application_clipboard_contents_get ();
	GdkAtom atom_gnumeric = gdk_atom_intern (GNUMERIC_ATOM_NAME, FALSE);
	Sheet *sheet = application_clipboard_sheet_get ();
	Range const *a = application_clipboard_area_get ();

	/*
	 * Not sure how to handle this, not sure what purpose this has has
	 * (sheet being NULL). I think it is here to indicate that the selection
	 * just has been cut.
	 */
	if (!sheet)
		return;

	/*
	 * If the content was marked for a cut we need to copy it for pasting
	 * we clear it later on, because if the other application (the one that
	 * requested we render the data) is another instance of gnumeric
	 * we need the selection to remain "intact" (not cleared) so we can
	 * render it to the Gnumeric XML clipboard format
	 */
	if (clipboard == NULL) {

		g_return_if_fail (sheet != NULL);
		g_return_if_fail (a != NULL);

		content_needs_free = TRUE;
		clipboard = clipboard_copy_range (sheet, a);
	}

	g_return_if_fail (clipboard != NULL);

	/*
	 * Check whether the other application wants gnumeric XML format
	 * in fact we only have to check the 'info' variable, however
	 * to be absolutely sure I check if the atom checks out too
	 */
	if (selection_data->target == atom_gnumeric && info == 2000) {
		xmlChar *buffer;
		int buffer_size;

		gnumeric_xml_write_selection_clipboard (wbc, sheet, &buffer, &buffer_size);

		gtk_selection_data_set (selection_data, GDK_SELECTION_TYPE_STRING, 8,
					(char *) buffer, buffer_size);

		g_free (buffer);
	} else {
		char *rendered_selection = cell_region_render_ascii (clipboard);

		gtk_selection_data_set (selection_data, GDK_SELECTION_TYPE_STRING, 8,
					rendered_selection, strlen (rendered_selection));

		g_free (rendered_selection);
	}

	/*
	 * If this was a CUT operation we need to clear the content that was pasted
	 * into another application and release the stuff on the clipboard
	 */
	if (content_needs_free) {

		sheet_clear_region (wbc, sheet,
				    a->start.col, a->start.row,
				    a->end.col,   a->end.row,
				    CLEAR_VALUES|CLEAR_COMMENTS);

		clipboard_release (clipboard);
		application_clipboard_clear (TRUE);
	}
}

/**
 * x_selection_clear:
 *
 * Callback for the "we lost the X selection" signal
 */
static gint
x_selection_clear (GtkWidget *widget, GdkEventSelection *event,
		   WorkbookControl *wbc)
{
	/* we have already lost the selection, no need to clear it */
	application_clipboard_clear (FALSE);

	return TRUE;
}

void
x_request_clipboard (WorkbookControlGUI *wbcg, PasteTarget const *pt, guint32 time)
{
	PasteTarget *new_pt;

	if (wbcg->clipboard_paste_callback_data != NULL)
		g_free (wbcg->clipboard_paste_callback_data);

	new_pt = g_new (PasteTarget, 1);
	*new_pt = *pt;
	wbcg->clipboard_paste_callback_data = new_pt;

	/* Query the formats, This will callback x_selection_received */
	gtk_selection_convert (GTK_WIDGET (wbcg->toplevel), GDK_SELECTION_PRIMARY,
			       gdk_atom_intern (TARGETS_ATOM_NAME, FALSE), time);
}
/**
 * x_clipboard_bind_workbook:
 *
 * Binds the signals related to the X selection to the Workbook
 * and initialized the clipboard data structures for the Workbook.
 */
void
x_clipboard_bind_workbook (WorkbookControlGUI *wbcg)
{
	GtkWindow *toplevel = wbcg->toplevel;
	GtkTargetEntry targets;

	wbcg->clipboard_paste_callback_data = NULL;

	gtk_signal_connect (
		GTK_OBJECT (toplevel), "selection_clear_event",
		GTK_SIGNAL_FUNC (x_selection_clear), wbcg);

	gtk_signal_connect (
		GTK_OBJECT (toplevel), "selection_received",
		GTK_SIGNAL_FUNC (x_selection_received), wbcg);

	gtk_signal_connect (
		GTK_OBJECT (toplevel), "selection_get",
		GTK_SIGNAL_FUNC (x_selection_handler), wbcg);

	gtk_selection_add_target (GTK_WIDGET (toplevel),
		GDK_SELECTION_PRIMARY, GDK_SELECTION_TYPE_STRING, 0);

	/*
	 * Our specific Gnumeric XML clipboard interchange type
	 */
	targets.target = GNUMERIC_ATOM_NAME;

	/* This is not useful, but we have to set it to something: */
	targets.flags  = GTK_TARGET_SAME_WIDGET;
	targets.info   = GNUMERIC_ATOM_INFO;

	gtk_selection_add_targets (GTK_WIDGET (toplevel),
				   GDK_SELECTION_PRIMARY, &targets, 1);
}
