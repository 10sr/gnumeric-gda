/*
 * cell.c: Cell management of the Gnumeric spreadsheet.
 *
 * Author:
 *    Jdy Goldberg 2000 (jgoldberg@home.com)
 *    Miguel de Icaza 1998, 1999 (miguel@kernel.org)
 */
#include "config.h"
#include "gnumeric.h"
#include "cell.h"
#include "cellspan.h"
#include "expr.h"
#include "eval.h"
#include "value.h"
#include "style.h"
#include "sheet.h"
#include "value.h"
#include "rendered-value.h"
#include "gnumeric-util.h"
#include "parse-util.h"
#include "format.h"

extern int dependency_debugging;

/**
 * cell_dirty : Mark the sheet containing the cell as being dirty.
 * @cell : the dirty cell.
 *
 * INTERNAL.
 */
static inline void
cell_dirty (Cell *cell)
{
	Sheet *sheet = cell->base.sheet;

	/* Cells from the clipboard do not have a sheet attached */
	if (sheet)
		sheet_set_dirty(sheet, TRUE);
}

/**
 * cell_cleanout :
 *      Empty a cell's
 *      	- value.
 *      	- rendered_value.
 *      	- expression.
 *      	- parse format.
 *
 *      Clears the flags to
 *      	- not queued for recalc.
 *      	- has no expression.
 *
 *      Does NOT change
 *      	- Comments.
 *      	- Spans.
 *      	- unqueue a previously queued recalc.
 *      	- Mark sheet as dirty.
 */
static void
cell_cleanout (Cell *cell)
{
	/* A cell can have either an expression or entered text */
	if (cell_has_expr (cell)) {
		/* Clipboard cells, e.g., are not attached to a sheet.  */
		if (cell_expr_is_linked (cell))
			dependent_unlink (CELL_TO_DEP (cell), &cell->pos);
		expr_tree_unref (cell->base.expression);
		cell->base.expression = NULL;
	}

	if (cell->value) {
		value_release (cell->value);
		cell->value = NULL;
	}
	if (cell->rendered_value) {
		rendered_value_destroy (cell->rendered_value);
		cell->rendered_value = NULL;
	}
	if (cell->format) {
		style_format_unref (cell->format);
		cell->format = NULL;
	}

	cell->base.flags &= ~(CELL_HAS_EXPRESSION|DEPENDENT_QUEUED_FOR_RECALC);
}

/**
 * cell_copy:
 * @cell: existing cell to duplicate
 *
 * Makes a copy of a Cell.
 *
 * Returns a copy of the cell.
 */
Cell *
cell_copy (Cell const *cell)
{
	Cell *new_cell;

	g_return_val_if_fail (cell != NULL, NULL);

	new_cell = g_new (Cell, 1);

	/* bitmap copy first */
	*new_cell = *cell;

	/* The new cell is not linked into any of the major management structures */
	new_cell->base.sheet = NULL;
	new_cell->base.flags &= ~(DEPENDENT_QUEUED_FOR_RECALC|CELL_IN_SHEET_LIST|DEPENDENT_IN_EXPR_LIST);

	/* now copy properly the rest */
	if (cell_has_expr (new_cell))
		expr_tree_ref (new_cell->base.expression);

	new_cell->rendered_value = NULL;

	new_cell->value = (new_cell->value)
	    ? value_duplicate (new_cell->value)
	    : value_new_empty ();

	if (cell->format)
		style_format_ref (cell->format);

	return new_cell;
}

/**
 * cell_destroy: Frees all resources allocated to the cell's content and marks the
 *     Cell's container as dirty.
 *
 * @cell : The cell to destroy
 */
void
cell_destroy (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	cell_dirty (cell);
	cell_cleanout (cell);
	g_free (cell);
}

/**
 * cell_eval_content:
 * @cell: the cell to evaluate.
 *
 * This function evaluates the contents of the cell,
 * it should not be used by anyone. It is an internal
 * function.
 **/
void
cell_eval_content (Cell *cell)
{
	Value   *v;
	EvalPos	 pos;

	if (!cell_has_expr (cell))
		return;

#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 1) {
		ParsePos pp;

		char *exprtxt = expr_decode_tree
			(cell->base.expression, parse_pos_init_cell (&pp, cell));
		printf ("Evaluating %s: %s ->\n", cell_name (cell), exprtxt);
		g_free (exprtxt);
	}
#endif

	v = eval_expr (eval_pos_init_cell (&pos, cell),
		       cell->base.expression, EVAL_STRICT);

#ifdef DEBUG_EVALUATION
	if (dependency_debugging > 1) {
		char *valtxt = v
			? value_get_as_string (v)
			: g_strdup ("NULL");
		printf ("Evaluating %s: -> %s\n", cell_name (cell), valtxt);
		g_free (valtxt);
	}
#endif

	if (v == NULL)
		v = value_new_error (&pos, "Internal error");

	cell_assign_value (cell, v, NULL);

	/* TODO : Can we Use spancalc without too much performance overhead
	 * Seems like a better approach may be to do this at display time */
	cell_render_value (cell, TRUE);
	sheet_redraw_cell (cell);
}

/**
 * cell_content_changed: Queues recalc of all of the cells depends.
 */
void
cell_content_changed (Cell *cell)
{
	GList   *deps;

	g_return_if_fail (cell != NULL);

	/* Queue all of the dependencies for this cell */
	deps = cell_get_dependencies (cell);
	if (deps)
		dependent_queue_recalc_list (deps, TRUE);
}

/*
 * cell_relocate:
 * @cell   : The cell that is changing position
 * @rwinfo : An OPTIONAL pointer to allow for bounds checking and relocation
 *
 * This routine is used to move a cell to a different location:
 *
 * Auxiliary items canvas items attached to the cell are moved.
 */
void
cell_relocate (Cell *cell, ExprRewriteInfo *rwinfo)
{
	g_return_if_fail (cell != NULL);

	/* 1. Tag the cell as dirty */
	cell_dirty (cell);

	/* 2. If the cell contains a formula, relocate the formula */
	if (cell_has_expr (cell)) {
		ExprTree *expr = cell->base.expression;

		if (cell_expr_is_linked (cell))
			dependent_unlink (CELL_TO_DEP (cell), &cell->pos);

		/*
		 * WARNING WARNING WARNING
		 *
		 * This will only work if the new array cell has already
		 * been inserted.
		 *
		 * WARNING WARNING WARNING
		 */
		/* If cell was part of an array, reset the corner pointer */
		if (expr->any.oper == OPER_ARRAY) {
			int const x = expr->array.x;
			int const y = expr->array.y;
			if (x != 0 || y != 0)
				expr->array.corner.cell =
					sheet_cell_get (cell->base.sheet,
							cell->pos.col - x,
							cell->pos.row - y);
		}

		/* bounds check, and adjust local references from the cell */
		if (rwinfo != NULL) {
			expr = expr_rewrite (expr, rwinfo);

			if (expr != NULL) {
				/* expression was unlinked above */
				expr_tree_unref (cell->base.expression);
				cell->base.expression = expr;
			}
		}

		/* Relink the expression.  */
		dependent_changed (CELL_TO_DEP (cell), &cell->pos, TRUE);
	}

#warning move objects
}

/****************************************************************************/

/*
 * cell_set_text : Parses the supplied text for storage as a value or
 * 		expression.  It marks the sheet as dirty.
 *
 * If the text is an expression it IS queued for recalc.
 *        the format prefered by the expression is stored for later use.
 * If the text is a value it is NOT rendered and spans are NOT calculated.
 *        the format that matched the text is stored for later use.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 *
 * NOTE : This DOES check for array partitioning.
 */
void
cell_set_text (Cell *cell, char const *text)
{
	StyleFormat *format;
	Value *val;
	ExprTree *expr;
	EvalPos pos;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);
	g_return_if_fail (!cell_is_partial_array (cell));

	format = parse_text_value_or_expr (eval_pos_init_cell (&pos, cell),
					   text, &val, &expr,
					   NULL /* TODO : Use assigned format ? */);

	if (val != NULL) {	/* String was a value */
		cell_cleanout (cell);

		cell->base.flags &= ~CELL_HAS_EXPRESSION;
		cell->value = val;
		cell->format = format;
		cell_render_value (cell, TRUE);
	} else {		/* String was an expression */
		cell_set_expr (cell, expr, format);
		if (format) style_format_unref (format);
		expr_tree_unref (expr);
	}
	cell_dirty (cell);
}

/*
 * cell_assign_value : Stores (WITHOUT COPYING) the supplied value.
 *    no changes are made to the expression or entered text.  This
 *    is for use by routines that wish to store values directly such
 *    as expression calculation or import for array formulas.
 *
 * The value is rendered but spans are not calculated.
 *
 * If an optional format is supplied it is stored for later use.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, does not calculate spans, does
 *           not mark anything as dirty.
 *
 * NOTE : This DOES NOT check for array partitioning.
 */
void
cell_assign_value (Cell *cell, Value *v, StyleFormat *opt_fmt)
{
	g_return_if_fail (cell);
	g_return_if_fail (v);

	if (cell->format)
		style_format_unref (cell->format);
	if (opt_fmt)
		style_format_ref (opt_fmt);
	cell->format = opt_fmt;

	if (cell->value != NULL)
		value_release (cell->value);
	cell->value = v;
	cell_render_value (cell, TRUE);
}

/*
 * cell_set_value : Stores (WITHOUT COPYING) the supplied value.  It marks the
 *          sheet as dirty.
 *
 * The value is rendered but spans are not calculated, then the rendered string
 * is stored as if that is what the user had entered.
 *
 * If an optional format is supplied it is stored for later use.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 *
 * FIXME FIXME FIXME : The current format code only checks against the list of
 * canned formats.  Therefore the rendered string MAY NOT BE PARSEABLE!! if the
 * user has assigned a non-std format.  We need to improve the parser to handle
 * all formats that exist within the workbook.
 *
 * NOTE : This DOES check for array partitioning.
 */
void
cell_set_value (Cell *cell, Value *v, StyleFormat *opt_fmt)
{
	g_return_if_fail (cell);
	g_return_if_fail (v);
	g_return_if_fail (!cell_is_partial_array (cell));

	if (opt_fmt)
		style_format_ref (opt_fmt);

	cell_dirty (cell);
	cell_cleanout (cell);

	/* TODO : It would be nice to standardize on NULL == General */
	cell->format = (opt_fmt == NULL || style_format_is_general (opt_fmt))
		? NULL : opt_fmt;
	cell->value = v;
}

/*
 * cell_set_expr_and_value : Stores (WITHOUT COPYING) the supplied value, and
 *        references the supplied expression and links it into the expression
 *        list.  It marks the sheet as dirty. It is intended for use by import
 *        routines or operations that do bulk assignment.
 *
 * If an optional format is supplied it is stored for later use.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, does not calculate spans, and does
 *           not render the value.
 *
 * NOTE : This DOES check for array partitioning.
 */
void
cell_set_expr_and_value (Cell *cell, ExprTree *expr, Value *v,
			 StyleFormat *opt_fmt)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (expr != NULL);
	g_return_if_fail (!cell_is_partial_array (cell));

	/* Repeat after me.  Ref before unref. */
	expr_tree_ref (expr);
	if (opt_fmt != NULL)
		style_format_ref (opt_fmt);

	cell_dirty (cell);
	cell_cleanout (cell);

	cell->format = opt_fmt;
	cell->base.expression = expr;
	cell->base.flags |= CELL_HAS_EXPRESSION;
	dependent_link (CELL_TO_DEP (cell), &cell->pos);
	cell->value = v;
}

/**
 * cell_set_expr_internal:
 * @cell: the cell to set the formula to
 * @expr: an expression tree with the formula
 * opt_fmt: an optional format to apply to the cell.
 *
 * A private internal utility to store an expression.
 * Does NOT
 * 	- check for array subdivision
 * 	- queue recalcs.
 * 	- render value, calc dimension, compute spans
 * 	- link the expression into the master list.
 */
static void
cell_set_expr_internal (Cell *cell, ExprTree *expr, StyleFormat *opt_fmt)
{
	expr_tree_ref (expr);
	if (opt_fmt != NULL)
		style_format_ref (opt_fmt);

	cell_dirty (cell);
	cell_cleanout (cell);

	cell->format = opt_fmt;
	cell->base.expression = expr;
	cell->base.flags |= CELL_HAS_EXPRESSION;

	/* Until the value is recomputed, we put in this value.  */
	cell->value = value_new_error (NULL, gnumeric_err_RECALC);
}

/*
 * cell_set_expr_unsafe : Stores and references the supplied expression.  It
 *         marks the sheet as dirty.  Intented for use by import routines that
 *         do bulk assignment.
 *
 * The cell IS marked for recalc.
 *
 * If an optional format is supplied it is stored for later use.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 *           It also DOES NOT CHECK FOR ARRAY DIVISION.  Be very careful
 *           using this.
 */
void
cell_set_expr_unsafe (Cell *cell, ExprTree *expr, StyleFormat *opt_fmt)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (expr != NULL);

	cell_set_expr_internal (cell, expr, opt_fmt);
	dependent_changed (CELL_TO_DEP (cell), &cell->pos, TRUE);
}

/**
 * cell_set_expr : A utility wrapper for cell_set_expr_unsafe.  That adds
 *      checks for array subdivision.
 */
void
cell_set_expr (Cell *cell, ExprTree *expr, StyleFormat *opt_fmt)
{
	g_return_if_fail (!cell_is_partial_array (cell));

	cell_set_expr_unsafe (cell, expr, opt_fmt);
}

/**
 * cell_set_array_formula:
 * @sheet:   The sheet to set the formula to.
 * @row_a:   The top row in the destination region.
 * @col_a:   The left column in the destination region.
 * @row_b:   The bottom row in the destination region.
 * @col_b:   The right column in the destination region.
 * @formula: an expression tree with the formula
 * @queue_recalc : A flag that if true indicates that the cells should be
 *                 queued for recalc.
 *
 * Uses cell_set_expr_internal to store the formula as an
 * 'array-formula'.  The supplied expression is wrapped in an array
 * operator for each cell in the range and scheduled for recalc.  The
 * upper left corner is handled as a special case and care is taken to
 * put it at the head of the recalc queue if recalcs are requested.
 *
 * NOTE : Does not add a reference to the expression.  It takes over the callers
 *        reference.
 *
 * Does not regenerate spans, dimensions or autosize cols/rows.
 *
 * DOES NOT CHECK for array partitioning.
 */
void
cell_set_array_formula (Sheet *sheet,
			int row_a, int col_a, int row_b, int col_b,
			ExprTree *formula,
			gboolean queue_recalc)
{
	int const num_rows = 1 + row_b - row_a;
	int const num_cols = 1 + col_b - col_a;
	int x, y;
	Cell * const corner = sheet_cell_fetch (sheet, col_a, row_a);
	ExprTree *wrapper;

	g_return_if_fail (num_cols > 0);
	g_return_if_fail (num_rows > 0);
	g_return_if_fail (formula != NULL);
	g_return_if_fail (corner != NULL);
	g_return_if_fail (col_a <= col_b);
	g_return_if_fail (row_a <= row_b);

	wrapper = expr_tree_new_array (0, 0, num_rows, num_cols);
	wrapper->array.corner.func.value = NULL;
	wrapper->array.corner.func.expr = formula;
	cell_set_expr_internal (corner, wrapper, NULL);
	expr_tree_unref (wrapper);

	for (x = 0; x < num_cols; ++x)
		for (y = 0; y < num_rows; ++y) {
			Cell *cell;

			if (x == 0 && y == 0)
				continue;

			cell = sheet_cell_fetch (sheet, col_a + x, row_a + y);
			wrapper = expr_tree_new_array (x, y, num_rows, num_cols);
			wrapper->array.corner.cell = corner;
			cell_set_expr_internal (cell, wrapper, NULL);
			dependent_changed (CELL_TO_DEP (cell),
					   &cell->pos, queue_recalc);
			expr_tree_unref (wrapper);
		}

	/* Put the corner at the head of the recalc list */
	dependent_changed (CELL_TO_DEP (corner), &corner->pos, queue_recalc);
}

/***************************************************************************/

gboolean
cell_is_blank (Cell const * cell)
{
	return (cell == NULL || cell->value == NULL ||
		cell->value->type == VALUE_EMPTY);
}

Value *
cell_is_error (Cell const * cell)
{
	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->value != NULL, NULL);

	if (cell->value->type == VALUE_ERROR)
		return cell->value;
	return NULL;
}

gboolean
cell_is_number (Cell const *cell)
{
	/* FIXME : This does not handle arrays or ranges */
	return (cell->value && VALUE_IS_NUMBER (cell->value));
}

gboolean
cell_is_zero (Cell const *cell)
{
	Value const * const v = cell->value;
	if (v == NULL)
		return FALSE;
	switch (v->type) {
	case VALUE_BOOLEAN : return !v->v_bool.val;
	case VALUE_INTEGER : return v->v_int.val == 0;
	case VALUE_FLOAT :
	{
		double const res = v->v_float.val;
		return (-1e-10 < res && res < 1e-10);
	}

	default :
		return FALSE;
	}
}

ExprArray const *
cell_is_array (Cell const *cell)
{
	if (cell != NULL && cell_has_expr (cell) &&
	    cell->base.expression->any.oper == OPER_ARRAY)
		return &cell->base.expression->array;
	return NULL;
}

gboolean
cell_is_partial_array (Cell const *cell)
{
	ExprArray const *ref = cell_is_array (cell);
	return ref != NULL && (ref->cols > 1 || ref->rows > 1);
}

StyleHAlignFlags
cell_default_halign (Cell const *c, MStyle const *mstyle)
{
	StyleHAlignFlags align = mstyle_get_align_h (mstyle);

	if (align == HALIGN_GENERAL) {
		g_return_val_if_fail (c != NULL, HALIGN_RIGHT);

		if (cell_is_number (c) &&
		    c->base.sheet && !c->base.sheet->display_formulas)
			return HALIGN_RIGHT;
		return HALIGN_LEFT;
	}

	return align;
}

/***************************************************************************/

/**
 * cell_render_value :
 * @cell: The cell whose value needs to be rendered
 * @dynamic_width : Allow format to depend on column width.
 *
 * TODO :
 * There is no reason currently for this to allocate the rendered value as
 * seperate entity.  However, in the future I'm thinking of referencing them
 * akin to strings.  We need to do some profiling of how frequently things
 * are shared.
 */
void
cell_render_value (Cell *cell, gboolean dynamic_width)
{
	RenderedValue *rv;
	MStyle *mstyle;

	g_return_if_fail (cell != NULL);

	mstyle = cell_get_mstyle (cell);

	rv = rendered_value_new (cell, mstyle, dynamic_width);
	if (cell->rendered_value)
		rendered_value_destroy (cell->rendered_value);
	cell->rendered_value = rv;

	rendered_value_calc_size_ext (cell, mstyle);
	mstyle_unref (mstyle);
}


MStyle *
cell_get_mstyle (Cell const *cell)
{
	return sheet_style_compute (cell->base.sheet,
				    cell->pos.col,
				    cell->pos.row);
}

void
cell_set_mstyle (Cell const *cell, MStyle *mstyle)
{
	Range         range;

	range.start = cell->pos;
	range.end   = range.start;

	sheet_style_attach (cell->base.sheet, range, mstyle);
}

char *
cell_get_format (Cell const *cell)
{
	char   *result = NULL;
	MStyle *mstyle;

	g_return_val_if_fail (cell != NULL, g_strdup ("General"));

	mstyle = cell_get_mstyle (cell);

	if (mstyle_is_element_set (mstyle, MSTYLE_FORMAT)) {
		StyleFormat const *format = mstyle_get_format (mstyle);

		/* FIXME: we really should distinguish between "not assigned"
		 * and "assigned General".
		 *
		 * 8/20/00 JEG : Do we still need this test ?
		 */
		if (format) {
			/* If the format is General it may have been over
			 * ridden by the format used to parse the input text.
			 */
			result = style_format_as_XL (format, FALSE);
			if (!strcmp (result, "General") != 0 && cell->format != NULL) {
				g_free (result);
				result = style_format_as_XL (cell->format, FALSE);
			}
		}
	}

	mstyle_unref (mstyle);
	return result;
}

/*
 * cell_set_format:
 *
 * Changes the format for CELL to be FORMAT.  FORMAT should be
 * a number display format as specified on the manual
 *
 * Does not render, redraw, or respan.
 */
void
cell_set_format (Cell *cell, char const *format)
{
	MStyle *mstyle = mstyle_new ();

	g_return_if_fail (mstyle != NULL);

	mstyle_set_format_text (mstyle, format);
	cell_set_mstyle (cell, mstyle);
	cell_dirty (cell);
}

/**
 * cell_make_value : drops the expression keeps its value.  Then uses the formatted
 *      result as if that had been entered.
 *
 * NOTE : the cell's expression cannot be linked into the expression * list.
 *
 * The cell is rendered but spans are not calculated,  the cell is NOT marked for
 * recalc.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 *
 * NOTE : This DOES NOT check for array partitioning.
 */
void
cell_make_value (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell_has_expr(cell));

	/* Clipboard cells, e.g., are not attached to a sheet.  */
	if (cell_expr_is_linked (cell))
		dependent_unlink (CELL_TO_DEP (cell), &cell->pos);

	expr_tree_unref (cell->base.expression);
	cell->base.expression = NULL;
	cell->base.flags &= ~CELL_HAS_EXPRESSION;

	if (cell->rendered_value == NULL)
		cell_render_value (cell, TRUE);

	cell_dirty (cell);
}
