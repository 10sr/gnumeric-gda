/* vim: set sw=8: */

/*
 * cmd-edit.c: Various commands to be used by the edit menu.
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
#include "config.h"
#include "cmd-edit.h"
#include "application.h"
#include "workbook.h"
#include "sheet.h"
#include "cell.h"
#include "expr.h"
#include "eval.h"
#include "selection.h"
#include "parse-util.h"
#include "ranges.h"
#include "command-context.h"
#include "commands.h"
#include "clipboard.h"

#include <gnome.h>

/**
 * cmd_select_all:
 * @sheet: The sheet
 *
 * Selects all of the cells in the sheet
 */
void
cmd_select_all (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_selection_reset_only (sheet);
	sheet_selection_add_range (sheet, 0, 0, 0, 0,
				   SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);

	sheet_update (sheet);
}

/**
 * cmd_select_cur_row:
 * @sheet: The sheet
 *
 * Selects an entire row
 */
void
cmd_select_cur_row (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_selection_reset_only (sheet);
	sheet_selection_add_range (sheet,
		sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row,
		0, sheet->cursor.edit_pos.row,
		SHEET_MAX_COLS-1, sheet->cursor.edit_pos.row);
	sheet_update (sheet);
}

/**
 * cmd_select_cur_col:
 * @sheet: The sheet
 *
 * Selects an entire column
 */
void
cmd_select_cur_col (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_selection_reset_only (sheet);
	sheet_selection_add_range (sheet,
		sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row,
		sheet->cursor.edit_pos.col, 0,
		sheet->cursor.edit_pos.col, SHEET_MAX_ROWS-1);
	sheet_update (sheet);
}

/**
 * cmd_select_cur_array :
 * @sheet: The sheet
 *
 * if the current cell is part of an array select
 * the entire array.
 */
void
cmd_select_cur_array (Sheet *sheet)
{
	ExprArray const *array;
	int col, row;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	col = sheet->cursor.edit_pos.col;
	row = sheet->cursor.edit_pos.row;
	array = cell_is_array (sheet_cell_get (sheet, col, row));

	if (array == NULL)
		return;

	sheet_selection_reset_only (sheet);
	/*
	 * leave the edit cell where it is,
	 * select the entire array.
	 */
	sheet_selection_add_range (sheet, col, row,
				   col - array->x, row - array->y,
				   col - array->x + array->cols,
				   row - array->y + array->rows);

	sheet_update (sheet);
}

static gint
cb_compare_deps (gconstpointer a, gconstpointer b)
{
	Cell const *cell_a = a;
	Cell const *cell_b = b;
	int tmp;

	tmp = cell_a->pos.row - cell_b->pos.row;
	if (tmp != 0)
		return tmp;
	return cell_a->pos.col - cell_b->pos.col;
}

/**
 * cmd_select_cur_depends :
 * @sheet: The sheet
 *
 * Select all cells that depend on the expression in the current cell.
 */
void
cmd_select_cur_depends (Sheet *sheet)
{
	Cell  *cur_cell;
	GList *deps, *ptr = NULL;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	cur_cell = sheet_cell_get (sheet, 
				   sheet->cursor.edit_pos.col,
				   sheet->cursor.edit_pos.row);
	if (cur_cell == NULL)
		return;

	deps = cell_get_dependencies (cur_cell);
	deps = dependent_list_filter (deps, DEPENDENT_CELL);
	if (deps == NULL)
		return;

	sheet_selection_reset_only (sheet);

	/* Short circuit */
	if (g_list_length (deps) == 1) {
		Cell *cell = deps->data;
		sheet_selection_add (sheet, cell->pos.col, cell->pos.row);
	} else {
		Range *cur = NULL;
		ptr = NULL;

		/* Merge the sorted list of cells into rows */
		for (deps = g_list_sort (deps, &cb_compare_deps) ; deps ; ) {
			Cell *cell = deps->data;

			if (cur == NULL ||
			    cur->end.row != cell->pos.row ||
			    cur->end.col+1 != cell->pos.col) {
				if (cur)
					ptr = g_list_prepend (ptr, cur);
				cur = g_new (Range, 1);
				cur->start.row = cur->end.row = cell->pos.row;
				cur->start.col = cur->end.col = cell->pos.col;
			} else
				cur->end.col = cell->pos.col;

			deps = g_list_remove (deps, cell);
		}
		if (cur)
			ptr = g_list_prepend (ptr, cur);

		/* Merge the coalesced rows into ranges */
		deps = ptr;
		for (ptr = NULL ; deps ; ) {
			Range *r1 = deps->data;
			GList *fwd;

			for (fwd = deps->next ; fwd ; ) {
				Range *r2 = fwd->data;

				if (r1->start.col == r2->start.col &&
				    r1->end.col == r2->end.col &&
				    r1->start.row-1 == r2->end.row) {
					r1->start.row = r2->start.row;
					g_free (fwd->data);
					fwd = g_list_remove (fwd, r2);
				} else
					fwd = fwd->next;
			}

			ptr = g_list_prepend (ptr, r1);
			deps = g_list_remove (deps, r1);
		}

		/* now select the ranges */
		while (ptr) {
			Range *r = ptr->data;
			sheet_selection_add_range (sheet,
						   r->start.col, r->start.row,
						   r->start.col, r->start.row,
						   r->end.col, r->end.row);
			g_free (ptr->data);
			ptr = g_list_remove (ptr, r);
		}
	}
	sheet_update (sheet);
}

/**
 * cmd_select_cur_inputs :
 * @sheet: The sheet
 *
 * Select all cells that are direct potential inputs to the
 * current cell.
 */
void
cmd_select_cur_inputs (Sheet *sheet)
{
	Cell *cell;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	cell = sheet_cell_get (sheet, 
			       sheet->cursor.edit_pos.col,
			       sheet->cursor.edit_pos.row);

	if (cell == NULL || !cell_has_expr (cell))
		return;

	/* TODO : finish this */
	sheet_update (sheet);
}

/**
 * cmd_paste :
 * @sheet: The destination sheet
 * @range : The range to paste to within the destination sheet.
 * @flags: Any paste special flags.
 *
 * Pastes the current cut buffer, copy buffer, or X selection to
 * the destination sheet range.
 *
 * When pasting a cut the destination MUST be the same size as the src.
 *
 * When pasting a copy the destination can be a singleton, or an integer
 * multiple of the size of the source.  This is not tested here.
 * Full undo support.
 */
void
cmd_paste (CommandContext *context, PasteTarget const *pt, guint32 time)
{
	CellRegion  *content;
	Range const *src_range;

	g_return_if_fail (pt->sheet != NULL);
	g_return_if_fail (IS_SHEET (pt->sheet));

	src_range = application_clipboard_area_get ();
	content = application_clipboard_contents_get ();

	if (content == NULL && src_range != NULL) {
		/* Pasting a Cut */
		ExprRelocateInfo rinfo;
		Sheet *src_sheet = application_clipboard_sheet_get ();

		/* Validate the size & shape of the target here. */
		int const cols = (src_range->end.col - src_range->start.col);
		int const rows = (src_range->end.row - src_range->start.row);

		Range dst = pt->range;

		if (range_is_singleton (&dst)) {
			dst.end.col = dst.start.col + cols;
			dst.end.row = dst.start.row + rows;
		} else if ((dst.end.col - dst.start.col) != cols ||
			   (dst.end.row - dst.start.row) != rows) {

			char *msg = g_strdup_printf (
				_("destination has a different shape (%dRx%dC) than the original (%dRx%dC)\n\n"
				  "Try selecting a single cell or an area of the same shape and size."),
				(dst.end.row - dst.start.row)+1,
				(dst.end.col - dst.start.col)+1,
				rows+1, cols+1);
			gnumeric_error_invalid (context, _("Unable to paste into selection"), msg);
			g_free (msg);
			return;
		}

		rinfo.origin = *src_range;
		rinfo.col_offset = dst.start.col - rinfo.origin.start.col;
		rinfo.row_offset = dst.start.row - rinfo.origin.start.row;
		rinfo.origin_sheet = src_sheet;
		rinfo.target_sheet = pt->sheet;

		cmd_paste_cut (context, &rinfo);
		application_clipboard_clear (TRUE);
	} else
		/*
		 * Pasting a Copy or from the X selection
		 * We can not check the size of the range here.  The source may
		 * be an X selection whose size is not known until later.
		 * Check it then.
		 */
		clipboard_paste (context, pt, time);
}

/**
 * cmd_paste_to_selection :
 * @dest_sheet: The sheet into which things should be pasted
 * @flags: special paste flags (eg transpose)
 *
 * Using the current selection as a target
 * Full undo support.
 */
void
cmd_paste_to_selection (CommandContext *context, Sheet *dest_sheet, int paste_flags)
{
	Range const *dest_range;
	PasteTarget pt;

	if (!selection_is_simple (context, dest_sheet, _("Paste")))
		return;

	dest_range = selection_first_range (dest_sheet, FALSE);
	g_return_if_fail (dest_range !=NULL);

	pt.sheet = dest_sheet;
	pt.range = *dest_range;
	pt.paste_flags = paste_flags;
	cmd_paste (context, &pt, GDK_CURRENT_TIME);
}

/**
 * cmd_shift_rows:
 * @context	The error context.
 * @sheet	the sheet
 * @col		column marking the start of the shift
 * @start_row	first row
 * @end_row	end row
 * @count	numbers of columns to shift.  negative numbers will
 *		delete count columns, positive number will insert
 *		count columns.
 *
 * Takes the cells in the region (col,start_row):(MAX_COL,end_row)
 * and copies them @count units (possibly negative) to the right.
 */

void
cmd_shift_rows (CommandContext *context, Sheet *sheet,
		int col, int start_row, int end_row, int count)
{
	ExprRelocateInfo rinfo;

	rinfo.col_offset = count;
	rinfo.row_offset = 0;
	rinfo.origin_sheet = rinfo.target_sheet = sheet;
	rinfo.origin.start.row = start_row;
	rinfo.origin.start.col = col;
	rinfo.origin.end.row = end_row;
	rinfo.origin.end.col = SHEET_MAX_COLS-1;

	cmd_paste_cut (context, &rinfo);
}

/**
 * cmd_shift_cols:
 * @context	The error context.
 * @sheet	the sheet
 * @start_col	first column
 * @end_col	end column
 * @row		row marking the start of the shift
 * @count	numbers of rows to shift.  a negative numbers will
 *		delete count rows, positive number will insert
 *		count rows.
 *
 * Takes the cells in the region (start_col,row):(end_col,MAX_ROW)
 * and copies them @count units (possibly negative) downwards.
 */
void
cmd_shift_cols (CommandContext *context, Sheet *sheet,
		int start_col, int end_col, int row, int count)
{
	ExprRelocateInfo rinfo;

	rinfo.col_offset = 0;
	rinfo.row_offset = count;
	rinfo.origin_sheet = rinfo.target_sheet = sheet;
	rinfo.origin.start.col = start_col;
	rinfo.origin.start.row = row;
	rinfo.origin.end.col = end_col;
	rinfo.origin.end.row = SHEET_MAX_ROWS-1;

	cmd_paste_cut (context, &rinfo);
}

