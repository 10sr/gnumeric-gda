/* vim: set sw=8: */

/*
 * sheet-merge.c: merged cell support
 *
 * Copyright (C) 2000, 2001 Jody Goldberg (jgoldberg@home.com)
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
#include "sheet-merge.h"
#include "sheet.h"
#include "sheet-private.h"
#include "ranges.h"
#include "cell.h"
#include "sheet-style.h"
#include "mstyle.h"
#include "expr.h"
#include "command-context.h"

#include <gnome.h> /*for translation */

static gint
range_row_cmp (Range const *a, Range const *b)
{
	int tmp = b->start.row - a->start.row;
	if (tmp == 0)
		tmp = a->start.col - b->start.col; /* YES I DO MEAN a - b */
	return tmp;
}

/**
 * sheet_merge_add :
 *
 * @wbc : workbook control
 * @sheet : the sheet which will contain the region
 * @src : The region to merge
 * @clear : should the non-corner content of the region be cleared and the
 *          style from the corner applied.
 *
 * Add a range to the list of merge targets.  Checks for array spliting returns
 * TRUE if there was an error.  Does not regen spans, redraw or render.
 */
gboolean
sheet_merge_add (WorkbookControl *wbc,
		 Sheet *sheet, Range const *r, gboolean clear)
{
	GSList *test;
	Range  *r_copy;
	Cell   *cell;
	MStyle *style;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (r != NULL, TRUE);

	if (sheet_range_splits_array (sheet, r, wbc, _("Merge")))
		return TRUE;

	test = sheet_merge_get_overlap (sheet, r);
	if (test != NULL) {
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc),
			_("There is already a merged region that intersects"),
			range_name (r));
		g_slist_free (test);
		return TRUE;
	}

	if (clear) {
		/* Clear the non-corner content */
		if (r->start.col != r->end.col)
			sheet_clear_region (wbc, sheet,
					    r->start.col+1, r->start.row,
					    r->end.col, r->end.row,
					    CLEAR_VALUES | CLEAR_COMMENTS |
					    CLEAR_NOCHECKARRAY);
		if (r->start.row != r->end.row)
			sheet_clear_region (wbc, sheet,
					    r->start.col, r->start.row+1,
	    /* yes I mean start.col */	    r->start.col, r->end.row,
					    CLEAR_VALUES | CLEAR_COMMENTS |
					    CLEAR_NOCHECKARRAY);

		/* Apply the corner style to the entire region */
		style = sheet_style_get (sheet, r->start.col, r->start.row);
		mstyle_ref (style);
		sheet_style_set_range (sheet, r, style);
	}

	r_copy = range_copy (r);
	g_hash_table_insert (sheet->hash_merged, &r_copy->start, r_copy);

	/* Store in order from bottom to top then LEFT TO RIGHT (by start coord) */
	sheet->list_merged = g_slist_insert_sorted (sheet->list_merged, r_copy,
						    (GCompareFunc)range_row_cmp);

	cell = sheet_cell_get (sheet, r->start.col, r->start.row);
	if (cell != NULL)
		cell->base.flags |= CELL_IS_MERGED;

	sheet->priv->reposition_selection = TRUE;
	return FALSE;
}

/**
 * sheet_merge_remove :
 *
 * @wbc   : workbook control
 * @sheet : the sheet which will contain the region
 * @range : The region
 *
 * Remove a merged range.
 * returns TRUE if there was an error.
 */
gboolean
sheet_merge_remove (WorkbookControl *wbc, Sheet *sheet, Range const *range)
{
	Range *r_copy;
	Cell *cell;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (range != NULL, TRUE);

	r_copy = g_hash_table_lookup (sheet->hash_merged, &range->start);

	g_return_val_if_fail (r_copy != NULL, TRUE);
	g_return_val_if_fail (range_equal (range, r_copy), TRUE);

	g_hash_table_remove (sheet->hash_merged, &r_copy->start);
	sheet->list_merged = g_slist_remove (sheet->list_merged, r_copy);

	cell = sheet_cell_get (sheet, range->start.col, range->start.row);
	if (cell != NULL)
		cell->base.flags &= ~CELL_IS_MERGED;

	g_free (r_copy);
	sheet->priv->reposition_selection = TRUE;
	return FALSE;
}

/**
 * sheet_merge_get_overlap :
 *
 * Returns a list of the merged regions that overlap the target region.
 * The list is ordered from top to bottom and RIGHT TO LEFT (by start coord).
 * Caller is responsible for freeing the list, but not the content.
 */
GSList *
sheet_merge_get_overlap (Sheet const *sheet, Range const *range)
{
	GSList *ptr, *res = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (range != NULL, NULL);

	for (ptr = sheet->list_merged ; ptr != NULL ; ptr = ptr->next) {
		Range * const test = ptr->data;

		if (range_overlap (range, test))
			res = g_slist_prepend (res, test);
	}

	return res;
}

/**
 * sheet_merge_contains_pos :
 *
 * If the CellPos is contained in the a merged region return the range.
 * The Range should NOT be freed.
 */
Range const *
sheet_merge_contains_pos (Sheet const *sheet, CellPos const *pos)
{
	GSList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	for (ptr = sheet->list_merged ; ptr != NULL ; ptr = ptr->next) {
		Range const * const range = ptr->data;
		if (range_contains (range, pos->col, pos->row))
			return range;
	}
	return NULL;
}

/**
 * sheet_merge_get_adjacent
 * @sheet : The sheet to look in.
 * @pos : the cell to test for adjacent regions.
 * @left : the return for a region on the left
 * @right : the return for a region on the right
 *
 * Returns the nearest regions to either side of @pos.
 */
void
sheet_merge_get_adjacent (Sheet const *sheet, CellPos const *pos,
			  Range const **left, Range const **right)
{
	GSList *ptr;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (pos != NULL);

	*left = *right = NULL;
	for (ptr = sheet->list_merged ; ptr != NULL ; ptr = ptr->next) {
		Range const * const test = ptr->data;
		if (test->start.row <= pos->row && pos->row <= test->end.row) {
			int const diff = test->end.col - pos->col;

			g_return_if_fail (diff != 0);

			if (diff < 0) {
				if (*left == NULL || (*left)->end.col < test->end.col)
					*left = test;
			} else {
				if (*right == NULL || (*right)->start.col > test->start.col)
					*right = test;
			}
		}
	}
}

/**
 * sheet_merge_is_corner :
 *
 * @sheet :
 * @pos : cellpos if top left corner
 *
 * Returns a Range pointer if the @pos is the topleft of a merged region.
 * The pointer should NOT be freed by the caller.
 */
Range const *
sheet_merge_is_corner (Sheet const *sheet, CellPos const *pos)
{
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	return g_hash_table_lookup (sheet->hash_merged, pos);
}

/**
 * sheet_merge_relocate :
 *
 * @rinfo : Descriptor of what is moving.
 *
 * Shifts merged regions that need to move.
 */
void
sheet_merge_relocate (ExprRelocateInfo const *ri)
{
	GSList   *ptr, *copy, *to_move = NULL;
	Range	 dest;
	gboolean clear, change_sheets;

	g_return_if_fail (ri != NULL);
	g_return_if_fail (IS_SHEET (ri->origin_sheet));
	g_return_if_fail (IS_SHEET (ri->target_sheet));
	    
	dest = ri->origin;
	clear = range_translate (&dest, ri->col_offset, ri->row_offset);
	change_sheets = (ri->origin_sheet != ri->target_sheet);

	/* Clear the destination range on the target sheet */
	if (change_sheets) {
		copy = g_slist_copy (ri->target_sheet->list_merged);
		for (ptr = copy; ptr != NULL ; ptr = ptr->next) {
			Range const *r = ptr->data;
			if (range_contains (&dest, r->start.col, r->start.row))
				sheet_merge_remove (NULL, ri->target_sheet, r);
		}
		g_slist_free (copy);
	}

	copy = g_slist_copy (ri->origin_sheet->list_merged);
	for (ptr = copy; ptr != NULL ; ptr = ptr->next ) {
		Range const *r = ptr->data;
		if (range_contains (&ri->origin, r->start.col, r->start.row)) {
			Range tmp = *r;

			/* Toss any objects that would be clipped. */
			sheet_merge_remove (NULL, ri->origin_sheet, r);
			if (!range_translate (&tmp, ri->col_offset, ri->row_offset))
				to_move = g_slist_prepend (to_move, range_copy (&tmp));
		} else if (!change_sheets &&
			   range_contains (&dest, r->start.col, r->start.row))
			sheet_merge_remove (NULL, ri->origin_sheet, r);
	}
	g_slist_free (copy);

	/* move the ranges after removing the previous content in case of overlap */
	for (ptr = to_move ; ptr != NULL ; ptr = ptr->next) {
		Range *dest = ptr->data;
		sheet_merge_add (NULL, ri->target_sheet, dest, TRUE);
		g_free (dest);
	}
	g_slist_free (to_move);
}
