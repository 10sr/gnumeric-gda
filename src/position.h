#ifndef GNUMERIC_POSITION_H
#define GNUMERIC_POSITION_H

#include "gnumeric.h"
#include "position.h"

struct _EvalPos {
	Sheet   *sheet;
	CellPos  eval;
};

struct _ParsePos {
	Workbook *wb;
	Sheet    *sheet;
	CellPos   eval;
};

/**
 * Used for getting a valid Sheet *from a CellRef
 * Syntax is CellRef, valid Sheet *
 */
#define eval_sheet(a,b)     (a?a:b)

/* Initialization routines for Evaluation Positions */
EvalPos  *eval_pos_init          (EvalPos *pp, Sheet *s, CellPos const *pos);
EvalPos  *eval_pos_init_cell     (EvalPos *pp, Cell const *cell);
EvalPos  *eval_pos_init_cellref  (EvalPos *dest, EvalPos const *src,
				  CellRef const *);

/* Initialization routines for Parse Positions */
ParsePos *parse_pos_init         (ParsePos *pp, Workbook *wb,
				  Sheet *sheet, int col, int row);
ParsePos *parse_pos_init_cell    (ParsePos *pp, Cell const *cell);
ParsePos *parse_pos_init_evalpos (ParsePos *pp, EvalPos const *pos);

/*****************************************************************************/

struct _CellRef {
	Sheet *sheet;
	int   col, row;

	unsigned char col_relative;
	unsigned char row_relative;
};
struct _RangeRef {
	CellRef a, b;
};

/* Normalize CellRefs */
void      cell_ref_make_abs     (CellRef *dest,
				 CellRef const *src,
				 EvalPos const *ep);
int       cell_ref_get_abs_col  (CellRef const *ref,
				 EvalPos const *pos);
int       cell_ref_get_abs_row  (CellRef const *cell_ref,
				 EvalPos const *src_fp);
void      cell_get_abs_col_row  (CellRef const *cell_ref,
				 CellPos const *pos,
				 int *col, int *row);

void      range_ref_normalize  (Range *dest,
				Sheet **start_sheet,
				Sheet **end_sheet,
				Value const *ref,
				EvalPos const *ep);

#endif /* GNUMERIC_POSITION_H */
