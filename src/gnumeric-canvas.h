#ifndef GNUMERIC_GNUMERIC_SHEET_H
#define GNUMERIC_GNUMERIC_SHEET_H

#include "gui-gnumeric.h"

#define GNUMERIC_TYPE_SHEET     (gnumeric_sheet_get_type ())
#define GNUMERIC_SHEET(obj)     (GTK_CHECK_CAST((obj), GNUMERIC_TYPE_SHEET, GnumericSheet))
#define GNUMERIC_SHEET_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), GNUMERIC_TYPE_SHEET))
#define GNUMERIC_IS_SHEET(o)    (GTK_CHECK_TYPE((o), GNUMERIC_TYPE_SHEET))

#define GNUMERIC_SHEET_FACTOR_X 1000000
#define GNUMERIC_SHEET_FACTOR_Y 2000000

struct _GnumericSheet {
	GnomeCanvas   canvas;

	SheetControlGUI *scg;

	struct {
		int first, last_full, last_visible;
	} row, col, row_offset, col_offset;

	ItemGrid      *item_grid;
	ItemCursor    *item_cursor;
	ItemEdit      *item_editor;

	/*
	 * This flag keeps track of a cell selector
	 * (ie, when the user uses the cursor keys
	 * to select a cell for an expression).
	 */
	gboolean	selecting_cell;
	int		sel_cursor_pos;
	int             sel_text_len;
	ItemCursor     *sel_cursor;

	/* Input context for dead key support */
	GdkIC     *ic;
	GdkICAttr *ic_attr;

	/*
	 * The column and row bars
	 */
	ItemBar       *colbar;
	ItemBar       *rowbar;
};

GtkType    gnumeric_sheet_get_type               (void);

GtkWidget *gnumeric_sheet_new            	 (SheetControlGUI *sheet, ItemBar *colbar, ItemBar *rowbar);
void       gnumeric_sheet_set_top_row            (GnumericSheet *gsheet, int new_first_row);
void       gnumeric_sheet_set_left_col           (GnumericSheet *gsheet, int new_first_col);
gboolean   gnumeric_sheet_can_select_expr_range  (GnumericSheet *gsheet);
void       gnumeric_sheet_set_cursor_bounds      (GnumericSheet *gsheet,
						  int start_col, int start_row,
						  int end_col,   int end_row);
void       gnumeric_sheet_compute_visible_ranges (GnumericSheet *gsheet,
						  gboolean const full_recompute);
void       gnumeric_sheet_make_cell_visible      (GnumericSheet *gsheet,
						  int col, int row,
						  gboolean const force_scroll);
void       gnumeric_sheet_create_editing_cursor  (GnumericSheet *gsheet);
void       gnumeric_sheet_stop_editing           (GnumericSheet *gsheet);

/* Managing the selection of cell ranges when editing a formula */

void       gnumeric_sheet_start_cell_selection   (GnumericSheet *gsheet,
						  int col, int row);
void       gnumeric_sheet_stop_cell_selection    (GnumericSheet *gsheet,
						  gboolean const clear_string);
void       gnumeric_sheet_selection_cursor_place (GnumericSheet *gsheet,
						  int col, int row);
void       gnumeric_sheet_selection_cursor_base  (GnumericSheet *gsheet,
						  int col, int row);
void       gnumeric_sheet_selection_extend       (GnumericSheet *gsheet,
						  int col, int row);

int	   gnumeric_sheet_find_col		 (GnumericSheet *item_grid,
						  int x, int *col_origin);
int	   gnumeric_sheet_find_row		 (GnumericSheet *item_grid,
						  int y, int *row_origin);

typedef struct {
	GnomeCanvasClass parent_class;
} GnumericSheetClass;

#endif /* GNUMERIC_GNUMERIC_SHEET_H */
