/*
 * complete-sheet.c: Auto completes values from a sheet.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * This is a pretty simple implementation, should be helpful
 * to find performance hot-spots (if you bump SEARCH_STEPS to SHEET_MAX_ROWS/2)
 *
 * (C) 2000 Helix Code, Inc.
 */
#include <config.h>
#include <string.h>
#include "sheet.h"
#include "cell.h"
#include "str.h"
#include "value.h"
#include "complete-sheet.h"
#include <gal/util/e-util.h>

#define SEARCH_STEPS 50
#define PARENT_TYPE (complete_get_type ())

static GtkObjectClass *parent_class;

static void
free_search (CompleteSheet *cs)
{
	/* Nothing, for now.  */
}

static void
complete_sheet_finalize (GtkObject *object)
{
	CompleteSheet *cs = COMPLETE_SHEET (object);
	g_free (cs->current);
	parent_class->finalize (object);
}

static void
complete_sheet_destroy (GtkObject *object)
{
	CompleteSheet *cs = COMPLETE_SHEET (object);

	free_search (cs);

	if (parent_class->destroy)
		(parent_class->destroy) (object);
}

#define MAX_SCAN_SPACE 1024

static gboolean
search_space_complete (CompleteSheet *cs)
{
	/*
	 * We could do a better job by looking at the ColRow segments.
	 * but for now this would do it
	 */
	if ((cs->sup - cs->inf) > MAX_SCAN_SPACE)
		return TRUE;

	return FALSE;
}

static void
reset_search (CompleteSheet *cs)
{
	free_search (cs);

	cs->inf = MAX (cs->row - 1, 0);
	cs->sup = MIN (cs->row + 1, SHEET_MAX_ROWS);
}

static gboolean
text_matches (CompleteSheet const *cs)
{
	char const *text;
	Complete const *complete = &cs->parent;
	Cell *cell = sheet_cell_get (cs->sheet, cs->col, cs->inf);

	if (cell == NULL || cell->value == NULL ||
	    cell->value->type != VALUE_STRING || cell_has_expr (cell))
		return FALSE;

	text = cell->value->v_str.val->str;
	if (strncmp (text, complete->text, strlen (complete->text)) != 0)
		return FALSE;

	(*complete->notify)(text, complete->notify_closure);
	return TRUE;
}

static gboolean
complete_sheet_search_iteration (Complete *complete)
{
	CompleteSheet *cs = COMPLETE_SHEET (complete);
	ColRowInfo *ci;
	int i;

	if (strncmp (cs->current, complete->text, strlen (cs->current)) != 0)
		reset_search (cs);

	/*
	 * Optimization:
	 * Load the column, if empty, then return, we wont auto-complete here.
	 */
	ci = sheet_col_get_info (cs->sheet, cs->col);
	if (ci == &cs->sheet->cols.default_style)
		return FALSE;

	for (i = 0; (i < SEARCH_STEPS) && (cs->inf >= 0); i++, cs->inf--)
		if (text_matches (cs))
			return FALSE;

	for (i = 0; (i < SEARCH_STEPS) && (cs->sup < SHEET_MAX_ROWS); i++, cs->sup++)
		if (text_matches (cs))
			return FALSE;

	if (search_space_complete (cs))
		return FALSE;

	return TRUE;
}

static void
complete_sheet_class_init (GtkObjectClass *object_class)
{
	CompleteClass *auto_complete_class = (CompleteClass *) object_class;

	object_class->destroy = complete_sheet_destroy;
	object_class->finalize = complete_sheet_finalize;
	auto_complete_class->search_iteration = complete_sheet_search_iteration;

	parent_class = gtk_type_class (PARENT_TYPE);
}

Complete *
complete_sheet_new (Sheet *sheet, int col, int row, CompleteMatchNotifyFn notify, void *notify_closure)
{
	/*
	 * Somehow every time I pronounce this, I feel like something is not quite right.
	 */
	CompleteSheet *cs;

	cs = gtk_type_new (complete_sheet_get_type ());
	complete_construct (COMPLETE (cs), notify, notify_closure);

	cs->sheet = sheet;
	cs->col = col;
	cs->row = row;
	cs->current = g_strdup ("");
	reset_search (cs);

	return COMPLETE (cs);
}

E_MAKE_TYPE(complete_sheet, "CompleteSheet", CompleteSheet, complete_sheet_class_init, NULL, PARENT_TYPE);
