/* vim: set sw=8: */

/*
 * parse-util.c: Various utility routines to parse or produce
 *     string representations of common reference types.
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
#include "parse-util.h"
#include "workbook.h"
#include "sheet.h"
#include "value.h"
#include "ranges.h"
#include "cell.h"
#include "expr.h"
#include "number-match.h"

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <gnome.h>

/* Can remove sheet since local references have NULL sheet */
char *
cellref_name (CellRef const *cell_ref, ParsePos const *pp, gboolean no_sheetname)
{
	static char buffer [sizeof (long) * 4 + 4];
	char *p = buffer;
	int col, row;
	Sheet *sheet = cell_ref->sheet;

	if (cell_ref->col_relative)
		col = pp->eval.col + cell_ref->col;
	else {
		*p++ = '$';
		col = cell_ref->col;
	}

	if (col <= 'Z'-'A'){
		*p++ = col + 'A';
	} else {
		int a = col / ('Z'-'A'+1);
		int b = col % ('Z'-'A'+1);

		*p++ = a + 'A' - 1;
		*p++ = b + 'A';
	}
	if (cell_ref->row_relative)
		row = pp->eval.row + cell_ref->row;
	else {
		*p++ = '$';
		row = cell_ref->row;
	}

	sprintf (p, "%d", row+1);

	/* If it is a non-local reference, add the path to the external sheet */
	if (sheet != NULL && !no_sheetname) {
		char *s;

		s = g_strconcat (sheet->name_quoted, "!", buffer, NULL);

		if (sheet->workbook != pp->wb) {
			char *n;

			n = g_strconcat ("[", sheet->workbook->filename, "]", s, NULL);
			g_free (s);
			s = n;
		}
		return s;
	} else
		return g_strdup (buffer);
}

gboolean
cellref_a1_get (CellRef *out, const char *in, CellPos const *pos)
{
	int col = 0;
	int row = 0;

	g_return_val_if_fail (in != NULL, FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	/* Try to parse a column */
	if (*in == '$'){
		out->col_relative = FALSE;
		in++;
	} else
		out->col_relative = TRUE;

	if (!(toupper (*in) >= 'A' && toupper (*in) <= 'Z'))
		return FALSE;

	col = toupper (*in++) - 'A';

	if (toupper (*in) >= 'A' && toupper (*in) <= 'Z')
		col = (col+1) * ('Z'-'A'+1) + toupper (*in++) - 'A';

	/* Try to parse a row */
	if (*in == '$'){
		out->row_relative = FALSE;
		in++;
	} else
		out->row_relative = TRUE;

	if (!(*in >= '1' && *in <= '9'))
		return FALSE;

	while (isdigit ((unsigned char)*in)){
		row = row * 10 + *in - '0';
		in++;
	}
	if (row > SHEET_MAX_ROWS)
		return FALSE;
	row--;

	if (*in) /* We havn't hit the end yet */
		return FALSE;

	/* Setup the cell reference information */
	if (out->row_relative)
		out->row = row - pos->row;
	else
		out->row = row;

	if (out->col_relative)
		out->col = col - pos->col;
	else
		out->col = col;

	out->sheet = NULL;

	return TRUE;
}

static gboolean
r1c1_get_item (int *num, unsigned char *rel, char const * *in)
{
	gboolean neg = FALSE;

	if (**in == '\0')
		return FALSE;

	if (**in == '[') {
		(*in)++;
		*rel = TRUE;
		if (!**in)
			return FALSE;

		if (**in == '+')
			(*in)++;
		else if (**in == '-') {
			neg = TRUE;
			(*in)++;
		}
	}
	*num = 0;

	while (**in && isdigit ((unsigned char)**in)) {
		*num = *num * 10 + **in - '0';
		(*in)++;
	}

	if (neg)
		*num = -*num;

	if (**in == ']')
		(*in)++;

	return TRUE;
}

gboolean
cellref_r1c1_get (CellRef *out, const char *in, CellPos const *pos)
{
	g_return_val_if_fail (in != NULL, FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	out->row_relative = FALSE;
	out->col_relative = FALSE;
	out->col = pos->col;
	out->row = pos->row;
	out->sheet = NULL;

	if (*in == 'R') {
		in++;
		if (!r1c1_get_item (&out->row, &out->row_relative, &in))
			return FALSE;
	} else
		return FALSE;

	if (*in == 'C') {
		in++;
		if (!r1c1_get_item (&out->col, &out->col_relative, &in))
			return FALSE;
	} else
		return FALSE;

	out->col--;
	out->row--;
	return TRUE;
}

/**
 * cellref_get:
 * @out: destination CellRef
 * @in: reference description text, no leading or trailing
 *      whitespace allowed.
 *
 * Converts the char * representation of a Cell reference into
 * an internal representation.
 *
 * Return value: TRUE if no format errors found.
 **/
gboolean
cellref_get (CellRef *out, const char *in, CellPos const *pos)
{
	return  cellref_a1_get (out, in, pos) ||
		cellref_r1c1_get (out, in, pos);
}

/****************************************************************************/

char const *
gnumeric_char_start_expr_p (char const * c)
{
	if ((*c == '=') || (*c == '@') || (*c == '+'))
		return c+1;
	return NULL;
}

const char *
cell_coord_name (int col, int row)
{
	static char buffer [2 + 4 * sizeof (long)];
	char *p = buffer;

	if (col <= 'Z'-'A'){
		*p++ = col + 'A';
	} else {
		int a = col / ('Z'-'A'+1);
		int b = col % ('Z'-'A'+1);

		*p++ = a + 'A' - 1;
		*p++ = b + 'A';
	}
	sprintf (p, "%d", row+1);

	return buffer;
}
const char *
cell_pos_name (CellPos const *pos)
{
	g_return_val_if_fail (pos != NULL, "ERROR");

	return cell_coord_name (pos->col, pos->row);
}

const char *
cell_name (Cell const *cell)
{
	g_return_val_if_fail (cell != NULL, "ERROR");

	return cell_coord_name (cell->pos.col, cell->pos.row);
}

const char *
col_name (int col)
{
	static char buffer [3];
	char *p = buffer;

	g_assert (col < SHEET_MAX_COLS);
	g_assert (col >= 0);

	if (col <= 'Z'-'A'){
		*p++ = col + 'A';
	} else {
		int a = col / ('Z'-'A'+1);
		int b = col % ('Z'-'A'+1);

		*p++ = a + 'A' - 1;
		*p++ = b + 'A';
	}
	*p = 0;

	return buffer;
}

/**
 * Converts a column name into an integer
 **/
int
parse_col_name (const char *cell_str, const char **endptr)
{
	char c;
	int col = 0;

	if (endptr)
		*endptr = cell_str;

	c = toupper ((unsigned char)*cell_str++);
	if (c < 'A' || c > 'Z')
		return 0;

	col = c - 'A';
	c = toupper ((unsigned char)*cell_str);
	if (c >= 'A' && c <= 'Z') {
		col = ((col + 1) * ('Z' - 'A' + 1)) + (c - 'A');
		cell_str++;
	}

	if (col >= SHEET_MAX_COLS)
		return 0;

	if (endptr)
		*endptr = cell_str;

	return col;
}

/*
 * parse_cell_name
 * @cell_name:   a string representation of a cell name.
 * @col:         result col
 * @row:         result row
 * @strict:      if this is TRUE, then parsing stops at possible errors,
 *               otherwise an attempt is made to return cell names with trailing garbage.
 *
 * Return value: true if the cell_name could be successfully parsed
 */
gboolean
parse_cell_name (const char *cell_str, int *col, int *row, gboolean strict, int *chars_read)
{
	char const * const original = cell_str;
	unsigned char c;
	gboolean found_digits = FALSE;

	if (*cell_str == '$')
		cell_str++;

	/* Parse column name: one or two letters.  */
	c = toupper ((unsigned char) *cell_str);
	cell_str++;
	if (c < 'A' || c > 'Z')
		return FALSE;

	*col = c - 'A';
	c = toupper ((unsigned char)*cell_str);
	if (c >= 'A' && c <= 'Z') {
		*col = ((*col + 1) * ('Z' - 'A' + 1)) + (c - 'A');
		cell_str++;
	}
	if (*col >= SHEET_MAX_COLS)
		return FALSE;

	if (*cell_str == '$')
		cell_str++;

	/* Parse row number: a sequence of digits.  */
	for (*row = 0; *cell_str; cell_str++) {
		if (*cell_str < '0' || *cell_str > '9'){
			if (found_digits && strict == FALSE){
				break;
			} else
				return FALSE;
		}
		found_digits = TRUE;
		*row = *row * 10 + (*cell_str - '0');
		if (*row > SHEET_MAX_ROWS) /* Note: ">" is deliberate.  */
			return FALSE;
	}
	if (*row == 0)
		return FALSE;

	/* Internal row numbers are one less than the displayed.  */
	(*row)--;

	if (chars_read)
		*chars_read = cell_str - original;
	return TRUE;
}

gboolean
parse_cell_name_or_range (const char *cell_str, int *col, int *row, int *cols, int *rows, gboolean strict)
{
        int e_col, e_row;

	*cols = *rows = 1;
	if (!parse_cell_name (cell_str, col, row, strict, NULL)) {
	        if (!parse_range (cell_str, col, row, &e_col, &e_row))
		        return FALSE;
		else {
		        *cols = e_col - *col + 1;
			*rows = e_row - *row + 1;
		}
	}

	return TRUE;
}


/*
 * Returns a list of cells in a string.  If the named cells do not
 * exist, they are created.  If the input string is not valid,
 * the error_flag is set.
 */
GSList *
parse_cell_name_list (Sheet *sheet,
		      const char *cell_name_str,
		      int *error_flag,
		      gboolean strict)
{
        char     *buf, *tmp = NULL;
	GSList   *cells = NULL;
	Cell     *cell;
	int      i, n, j, k, col, row;
	gboolean range_flag = 0;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (cell_name_str != NULL, NULL);
	g_return_val_if_fail (error_flag != NULL, NULL);

	buf = g_malloc (strlen (cell_name_str) + 1);
	for (i = n = 0; ; i++) {

	        if ((cell_name_str [i] == ',') ||
		    (cell_name_str [i] == ':') ||
		    (cell_name_str [i] == '\0')){
		        buf [n] = '\0';

			if (!parse_cell_name (buf, &col, &row, strict, NULL)){
			error:
			        *error_flag = 1;
				free (buf);
				g_slist_free (cells);
				g_free (tmp);
				return NULL;
			}

			if (cell_name_str [i] == ':')
			        if (range_flag) {
				        goto error;
				} else {
					tmp = g_strdup (buf);
				        range_flag = 1;
				}
			else if (range_flag) {
			        int x1, x2, y1, y2;

				parse_cell_name (tmp, &x1, &y1, strict, NULL);
				parse_cell_name (buf, &x2, &y2, strict, NULL);
			        for (j = x1; j <= x2; j++)
				        for (k = y1; k <= y2; k++) {
					        cell = sheet_cell_fetch
						  (sheet, j, k);
						cells = g_slist_append
						  (cells, (gpointer) cell);
					}
			} else {
			        cell = sheet_cell_fetch (sheet, col, row);
			        cells = g_slist_append(cells, (gpointer) cell);
			}
			n = 0;
		} else
		        buf [n++] = cell_name_str [i];
		if (! cell_name_str [i])
		        break;
	}

	*error_flag = 0;
	free (buf);
	g_free (tmp);
	return cells;
}

/**
 * parse_text_value_or_expr : Utility routine to parse a string and convert it
 *     into an expression or value.
 *
 * @pos : If the string looks like an expression parse it at this location.
 * @text: The text to be parsed.
 * @val : Returns a Value * if the text was a value, otherwise NULL.
 * @expr: Returns an ExprTree * if the text was an expression, otherwise NULL.
 * @current_format : Optional, current number format.
 *
 * Returns : A pointer to the optimal format to display the value or
 *     expression result, possibly NULL if there is no preferred format.
 *
 * If there is a parse failure for an expression an error Value with the syntax
 * error is returned.
 */
StyleFormat *
parse_text_value_or_expr (EvalPos const *pos, char const *text,
			  Value **val, ExprTree **expr,
			  StyleFormat const *current_format /* can be NULL */)
{
	StyleFormat *desired_format = NULL;
	char const * const expr_start = gnumeric_char_start_expr_p (text);

	if (NULL != expr_start) {
		if (*expr_start) {
			char *error_msg = _("ERROR");
			ParsePos pp;

			/* Parse in the supplied eval context */
			*expr = expr_parse_string (expr_start,
				parse_pos_init_evalpos (&pp, pos),
				&desired_format,
				&error_msg);

			/* If the parse fails set the value to be the syntax error */
			if (*expr == NULL)
				*val = value_new_error (pos, error_msg);
			else
				*val = NULL;
		} else {
			*val = value_new_string (text);
			*expr = NULL;
		}
	} else {
		/* Does it match any formats?  */
		*val = format_match (text, current_format, &desired_format);

		/* If it does not match known formats, assume it is text.  */
		if (*val == NULL)
			*val = value_new_string (text);

		*expr = NULL;
	}

	return desired_format;
}
