/*
 * sheet-control-gui.c: Implements a graphic control for a sheet.
 *
 * Author:
 *    Miguel de Icaza (miguel@kernel.org)
 *    Jody Goldberg (jgoldberg@home.com)
 */
#include <config.h>

#include "sheet-control-gui.h"
#include "item-bar.h"
#include "gnumeric-sheet.h"
#include "sheet.h"
#include "workbook.h"
#include "workbook-view.h"
#include "workbook-edit.h"
#include "workbook-cmd-format.h"
#include "cell.h"
#include "selection.h"
#include "style.h"
#include "sheet-object-impl.h"
#include "sheet-object-cell-comment.h"
#include "item-cursor.h"
#include "gnumeric-util.h"
#include "parse-util.h"
#include "selection.h"
#include "application.h"
#include "cellspan.h"
#include "cmd-edit.h"
#include "commands.h"
#include "clipboard.h"
#include "dialogs.h"

#ifdef ENABLE_BONOBO
#include <bonobo/bonobo-view-frame.h>
#endif
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <string.h>
#include <gal/widgets/e-cursors.h>

static GtkTableClass *sheet_view_parent_class;

#if 0
#ifndef __GNUC__
#define __FUNCTION__ __FILE__
#endif
#define gnome_canvas_item_grab(a,b,c,d) do {		\
	fprintf (stderr, "%s %d: grab OBJ %p\n",	\
		 __FUNCTION__, __LINE__, a);		\
	gnome_canvas_item_grab (a, b, c,d);		\
} while (0)
#define gnome_canvas_item_ungrab(a,b) do {		\
	fprintf (stderr, "%s %d: ungrab OBJ %p\n",	\
		 __FUNCTION__, __LINE__, a);		\
	gnome_canvas_item_ungrab (a, b);		\
} while (0)
#endif

void
sheet_view_redraw_all (SheetControlGUI *scg)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	gnome_canvas_request_redraw (
		GNOME_CANVAS (scg->canvas),
		0, 0, INT_MAX, INT_MAX);
	gnome_canvas_request_redraw (
		GNOME_CANVAS (scg->col_canvas),
		0, 0, INT_MAX, INT_MAX);
	gnome_canvas_request_redraw (
		GNOME_CANVAS (scg->row_canvas),
		0, 0, INT_MAX, INT_MAX);
}

/*
 * Redraw selected range, do not honour spans
 */
void
sheet_view_redraw_cell_region (SheetControlGUI *scg,
			       int start_col, int start_row,
			       int end_col, int end_row)
{
	GnumericSheet *gsheet;
	GnomeCanvas *canvas;
	int x, y, w, h;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	gsheet = GNUMERIC_SHEET (scg->canvas);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	canvas = GNOME_CANVAS (gsheet);

	if ((end_col < gsheet->col.first) ||
	    (end_row < gsheet->row.first) ||
	    (start_col > gsheet->col.last_visible) ||
	    (start_row > gsheet->row.last_visible))
		return;

	/* Only draw those regions that are visible */
	start_col = MAX (gsheet->col.first, start_col);
	start_row = MAX (gsheet->row.first, start_row);
	end_col =  MIN (gsheet->col.last_visible, end_col);
	end_row =  MIN (gsheet->row.last_visible, end_row);

	x = scg_colrow_distance_get (scg, TRUE, gsheet->col.first, start_col);
	y = scg_colrow_distance_get (scg, FALSE, gsheet->row.first, start_row);
	w = scg_colrow_distance_get (scg, TRUE, start_col, end_col+1);
	h = scg_colrow_distance_get (scg, FALSE, start_row, end_row+1);

	x += canvas->layout.xoffset - canvas->zoom_xofs;
	y += canvas->layout.yoffset - canvas->zoom_yofs;

#if 0
	fprintf (stderr, "%s%d:", col_name(min_col), first_row+1);
	fprintf (stderr, "%s%d\n", col_name(max_col), last_row+1);
#endif

	/* redraw a border of 2 pixels around the region to handle thick borders
	 * NOTE the 2nd coordinates are excluded so add 1 extra (+2border +1include) */
	gnome_canvas_request_redraw (GNOME_CANVAS (gsheet),
				     x-2, y-2,
				     x+w+4+1, y+h+4+1);
}

void
sheet_view_redraw_headers (SheetControlGUI *scg,
			   gboolean const col, gboolean const row,
			   Range const * r /* optional == NULL */)
{
	GnumericSheet *gsheet;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	gsheet = GNUMERIC_SHEET (scg->canvas);

	if (col) {
		int left = 0, right = INT_MAX-1;
		if (r != NULL) {
			int const size = r->end.col - r->start.col;
/* A rough heuristic guess of the number of when the
 * trade off point of redrawing all vs calculating the
 * redraw size is crossed */
#define COL_HEURISTIC	20
			if (-COL_HEURISTIC < size && size < COL_HEURISTIC) {
				left = gsheet->col_offset.first +
					scg_colrow_distance_get (scg, TRUE,
							  gsheet->col.first, r->start.col);
				right = left +
					scg_colrow_distance_get (scg, TRUE,
							  r->start.col, r->end.col+1);
			}
		}
		/* Request excludes the far coordinate.  Add 1 to include them */
		gnome_canvas_request_redraw (
			GNOME_CANVAS (scg->col_canvas),
			left, 0, right+1, INT_MAX);
	}

	if (row) {
		int top = 0, bottom = INT_MAX-1;
		if (r != NULL) {
			int const size = r->end.row - r->start.row;
/* A rough heuristic guess of the number of when the
 * trade off point of redrawing all vs calculating the
 * redraw size is crossed */
#define ROW_HEURISTIC	50
			if (-ROW_HEURISTIC < size && size < ROW_HEURISTIC) {
				top = gsheet->row_offset.first +
					scg_colrow_distance_get (scg, FALSE,
							  gsheet->row.first, r->start.row);
				bottom = top +
					scg_colrow_distance_get (scg, FALSE,
							  r->start.row, r->end.row+1);
			}
		}
		/* Request excludes the far coordinate.  Add 1 to include them */
		gnome_canvas_request_redraw (
			GNOME_CANVAS (scg->row_canvas),
			0, top, INT_MAX, bottom+1);
	}
}

void
sheet_view_update_cursor_pos (SheetControlGUI *scg)
{
	GList *l;
	GnumericSheet *gsheet;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	gsheet = GNUMERIC_SHEET (scg->canvas);

	/* Repsition the selection cursor */
	item_cursor_reposition (gsheet->item_cursor);

	/* Reposition the animated cursors */
	for (l = scg->anted_cursors; l; l = l->next)
		item_cursor_reposition (ITEM_CURSOR (l->data));
}

void
sheet_view_set_zoom_factor (SheetControlGUI *scg, double factor)
{
	GnumericSheet *gsheet;
	ItemBar *col_item, *row_item;
	int h, w;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	gsheet = GNUMERIC_SHEET (scg->canvas);
	col_item = ITEM_BAR (scg->col_item);
	row_item = ITEM_BAR (scg->row_item);

	/* Set pixels_per_unit before the font.  The item bars look here for the number */
	gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (gsheet), factor);

	/* resize the header fonts */
	item_bar_fonts_init (col_item);
	item_bar_fonts_init (row_item);

	/*
	 * Use the size of the bold header font to size the free dimensions
	 * No need to zoom, the size of the font takes that into consideration.
	 */

	/* 2 pixels above and below */
	h = 2 + 2 + style_font_get_height (col_item->bold_font);
	/* 5 pixels left and right plus the width of the widest string I can think of */
	w = 5 + 5 + gdk_string_width (style_font_gdk_font (col_item->bold_font), "88888");

	gtk_widget_set_usize (GTK_WIDGET (scg->col_canvas), -1, h);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (scg->col_canvas), 0, 0,
					GNUMERIC_SHEET_FACTOR_X * factor, h);

	gtk_widget_set_usize (GTK_WIDGET (scg->row_canvas), w, -1);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (scg->row_canvas), 0, 0,
					w, GNUMERIC_SHEET_FACTOR_Y * factor);

	/* Recalibrate the starting offsets */
	gsheet->col_offset.first =
		scg_colrow_distance_get (scg, TRUE, 0, gsheet->col.first);
	gsheet->row_offset.first =
		scg_colrow_distance_get (scg, FALSE, 0, gsheet->row.first);

	if (GTK_WIDGET_REALIZED (gsheet))
		/* Ensure that the current cell remains visible when we zoom */
		gnumeric_sheet_make_cell_visible
			(gsheet,
			 scg->sheet->edit_pos.col,
			 scg->sheet->edit_pos.row,
			 TRUE);

	sheet_view_update_cursor_pos (scg);
}

static void
canvas_bar_realized (GtkWidget *widget, gpointer data)
{
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);
}

static GnomeCanvas *
new_canvas_bar (SheetControlGUI *scg, GtkOrientation o, GnomeCanvasItem **itemp)
{
	GtkWidget *canvas =
	    gnome_canvas_new ();
	GnomeCanvasGroup *group =
	    GNOME_CANVAS_GROUP (GNOME_CANVAS (canvas)->root);
	GnomeCanvasItem *item =
	    gnome_canvas_item_new (group,
				   item_bar_get_type (),
				   "ItemBar::SheetControlGUI", scg,
				   "ItemBar::Orientation", o,
				   NULL);

	gtk_signal_connect (GTK_OBJECT (canvas), "realize",
			    (GtkSignalFunc) canvas_bar_realized,
			    NULL);

	*itemp = item;
	gtk_widget_show (canvas);

	return GNOME_CANVAS(canvas);
}

/* Manages the scrollbar dimensions and paging parameters. */
void
scg_scrollbar_config (SheetControlGUI const *scg)
{
	GtkAdjustment *va = GTK_ADJUSTMENT (scg->va);
	GtkAdjustment *ha = GTK_ADJUSTMENT (scg->ha);
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);
	Sheet         *sheet = scg->sheet;
	int const last_col = gsheet->col.last_full;
	int const last_row = gsheet->row.last_full;

	va->upper = MAX (MAX (last_row,
			      scg->sheet->rows.max_used),
			 MAX (sheet->cursor.move_corner.row,
			      sheet->cursor.base_corner.row));
	va->page_size = last_row - gsheet->row.first;
	va->value = gsheet->row.first;
	va->step_increment = va->page_increment =
	    va->page_size / 2;

	ha->upper = MAX (MAX (last_col,
			      scg->sheet->cols.max_used),
			 MAX (sheet->cursor.move_corner.col,
			      sheet->cursor.base_corner.col));
	ha->page_size = last_col - gsheet->col.first;
	ha->value = gsheet->col.first;
	ha->step_increment = ha->page_increment =
	    ha->page_size / 2;

	gtk_adjustment_changed (va);
	gtk_adjustment_changed (ha);
}

#if 0
/*
 * sheet_view_make_edit_pos_visible
 * @scg  Sheet view
 *
 * Make the cell at the edit position visible.
 *
 * To be called from the "size_allocate" signal handler when the geometry of a
 * new sheet view has been configured.
 */
static void
sheet_view_make_edit_pos_visible (SheetControlGUI const *scg)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->scg);

	gnumeric_sheet_make_cell_visible
		(gsheet,
		 scg->sheet->edit_pos.col,
		 scg->sheet->edit_pos.row,
		 TRUE);

}
#endif

static void
sheet_view_size_allocate (GtkWidget *widget, GtkAllocation *alloc, SheetControlGUI *scg)
{
#if 0
	/* FIXME
	 * When a new sheet is added this is called and if the edit cell was not visible
	 * we change the scroll position even though to the user the size did not change
	 * and there is no reason for the scrolling to jump.
	 *
	 * Can we somehow do this only if the edit pos was visible initially ?
	 */
	sheet_view_make_edit_pos_visible (scg);
#endif
	scg_scrollbar_config (scg);
}

void
scg_colrow_size_set (SheetControlGUI *scg,
		     gboolean is_cols, int index, int new_size_pixels)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (scg->wbcg);
	Sheet *sheet = scg->sheet;

	/* If all cols/rows in the selection are completely selected
	 * then resize all of them, otherwise just resize the selected col/row.
	 */
	if (!sheet_selection_full_cols_rows (sheet, is_cols, index)) {
		ColRowIndexList *sel = colrow_get_index_list (index, index, NULL);
		cmd_resize_colrow (wbc, sheet, is_cols, sel, new_size_pixels);
	} else
		workbook_cmd_resize_selected_colrow (wbc, is_cols,
						     sheet, new_size_pixels);
}

void
scg_colrow_select (SheetControlGUI *scg, gboolean is_cols,
		   int index, int modifiers)
{
	Sheet *sheet = scg->sheet;
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);
	gboolean const rangesel = gnumeric_sheet_can_select_expr_range (gsheet);

	if (!rangesel)
		workbook_finish_editing (scg->wbcg, FALSE);

	if (rangesel && !gsheet->selecting_cell)
		gnumeric_sheet_start_cell_selection (gsheet, index, index);

	if (modifiers & GDK_SHIFT_MASK) {
		if (is_cols) {
			if (rangesel)
				gnumeric_sheet_rangesel_cursor_extend (gsheet,
					index, -1);
			else
				sheet_selection_extend_to (sheet, index, -1);
		} else {
			if (rangesel)
				gnumeric_sheet_rangesel_cursor_extend (gsheet,
					-1, index);
			else
				sheet_selection_extend_to (sheet, -1, index);
		}
	} else {
		if (!rangesel && !(modifiers & GDK_CONTROL_MASK))
			sheet_selection_reset_only (sheet);

		if (is_cols) {
			if (rangesel)
				gnumeric_sheet_rangesel_cursor_bounds (gsheet,
					index, 0, index, SHEET_MAX_ROWS-1);
			else
				sheet_selection_add_range (sheet,
					index, gsheet->row.first,
					index, 0,
					index, SHEET_MAX_ROWS-1);
		} else {
			if (rangesel)
				gnumeric_sheet_rangesel_cursor_bounds (gsheet,
					0, index, SHEET_MAX_COLS-1, index);
			else
				sheet_selection_add_range (sheet,
					gsheet->col.first, index,
					0, index,
					SHEET_MAX_COLS-1, index);
		}
	}

	/* The edit pos, and the selection may have changed */
	sheet_update (sheet);
}

/***************************************************************************/


static void
button_select_all (GtkWidget *the_button, SheetControlGUI *scg)
{
	cmd_select_all (scg->sheet);
}

static void
vertical_scroll_change (GtkAdjustment *adj, SheetControlGUI *scg)
{
	if (scg->tip) {
		char *buffer;
		buffer = g_strdup_printf (_("Row: %d"), (int) adj->value + 1);
		gtk_label_set_text (GTK_LABEL (scg->tip), buffer);
		g_free(buffer);
	}
}

static void
horizontal_scroll_change (GtkAdjustment *adj, SheetControlGUI *scg)
{
	if (scg->tip) {
		char *buffer;
		buffer = g_strdup_printf(_("Column: %s"), col_name (adj->value));
		gtk_label_set_text (GTK_LABEL (scg->tip), buffer);
		g_free(buffer);
	}
}

static int
horizontal_scroll_event (GtkScrollbar *scroll, GdkEvent *event, SheetControlGUI *scg)
{
	if (event->type == GDK_BUTTON_PRESS){
		if (!scg->tip)
			scg->tip = gnumeric_create_tooltip ();
		horizontal_scroll_change (GTK_ADJUSTMENT (scg->ha), scg);
		gnumeric_position_tooltip (scg->tip, 1);
		gtk_widget_show_all (gtk_widget_get_toplevel (scg->tip));

	} else if (event->type == GDK_BUTTON_RELEASE) {
		GnumericSheet  *gsheet = GNUMERIC_SHEET (scg->canvas);
		int col;

		/* A button release can be generated without a press by people
		 * with mouse wheels */
		if (scg->tip) {
			gtk_widget_destroy (gtk_widget_get_toplevel (scg->tip));
			scg->tip = NULL;
		}

		col = GTK_ADJUSTMENT (scg->ha)->value;

		gnumeric_sheet_set_left_col (gsheet, col);
		/* NOTE : Excel does not move the cursor, just scrolls the sheet. */
	}

	return FALSE;
}

static int
vertical_scroll_event (GtkScrollbar *scroll, GdkEvent *event, SheetControlGUI *scg)
{
	if (event->type == GDK_BUTTON_PRESS){
		if (!scg->tip)
			scg->tip = gnumeric_create_tooltip ();
		vertical_scroll_change (GTK_ADJUSTMENT (scg->va), scg);
		gnumeric_position_tooltip (scg->tip, 0);
		gtk_widget_show_all (gtk_widget_get_toplevel (scg->tip));

	} else if (event->type == GDK_BUTTON_RELEASE) {
		GnumericSheet  *gsheet = GNUMERIC_SHEET (scg->canvas);
		int row;

		/* A button release can be generated without a press by people
		 * with mouse wheels */
		if (scg->tip) {
			gtk_widget_destroy (gtk_widget_get_toplevel (scg->tip));
			scg->tip = NULL;
		}

		row = GTK_ADJUSTMENT (scg->va)->value;

		gnumeric_sheet_set_top_row (gsheet, row);
		/* NOTE : Excel does not move the cursor, just scrolls the sheet. */
	}

	return FALSE;
}

static void
scg_init (SheetControlGUI *scg)
{
	GtkTable *table = GTK_TABLE (scg);

	scg->sheet = NULL;
	scg->slide_handler = NULL;
	scg->slide_data = NULL;
	scg->sliding = -1;
	table->homogeneous = FALSE;
	gtk_table_resize (table, 4, 4);

	scg->comment.selected = NULL;
	scg->comment.item = NULL;
	scg->comment.timer = -1;

	scg->new_object = NULL;
	scg->current_object = NULL;
	scg->active_object_frame = NULL;
	scg->drag_object = NULL;
}

static void
sheet_view_construct (SheetControlGUI *scg)
{
	GnomeCanvasGroup *root_group;
	GtkTable  *table = GTK_TABLE (scg);
	Sheet *sheet = scg->sheet;
	int i;

	/* Column canvas */
	scg->col_canvas = new_canvas_bar (scg, GTK_ORIENTATION_HORIZONTAL, &scg->col_item);
	gtk_table_attach (table, GTK_WIDGET (scg->col_canvas),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_FILL,
			  0, 0);

	/* Row canvas */
	scg->row_canvas = new_canvas_bar (scg, GTK_ORIENTATION_VERTICAL, &scg->row_item);

	gtk_table_attach (table, GTK_WIDGET (scg->row_canvas),
			  0, 1, 1, 2,
			  GTK_FILL, GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);

	scg->canvas = gnumeric_sheet_new (scg);
	gtk_signal_connect_after (
		GTK_OBJECT (scg), "size_allocate",
		GTK_SIGNAL_FUNC (sheet_view_size_allocate), scg);

	/* Create the object group inside the GnumericSheet */
	root_group = GNOME_CANVAS_GROUP (
		GNOME_CANVAS (scg->canvas)->root);
	scg->object_group = GNOME_CANVAS_GROUP (
		gnome_canvas_item_new (
			root_group,
			gnome_canvas_group_get_type (),
			"x", 0.0,
			"y", 0.0,
			NULL));

	/* Attach the GnumericSheet */
	gtk_table_attach (table, scg->canvas,
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	gtk_widget_show (scg->canvas);

	i = sizeof (scg->control_points)/sizeof(GnomeCanvasItem *);
	while (i-- > 0)
		scg->control_points[i] = NULL;

	/*
	 * The selection group
	 */
	scg->selection_group = GNOME_CANVAS_GROUP (
		gnome_canvas_item_new (
			root_group,
			gnome_canvas_group_get_type (),
			"x", 0.0,
			"y", 0.0,
			NULL));

	/* The select-all button */
	scg->select_all_btn = gtk_button_new ();
	GTK_WIDGET_UNSET_FLAGS (scg->select_all_btn, GTK_CAN_FOCUS);
	gtk_table_attach (table, scg->select_all_btn, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_signal_connect (GTK_OBJECT (scg->select_all_btn), "clicked",
			    GTK_SIGNAL_FUNC (button_select_all), scg);

	/* Scroll bars and their adjustments */
	scg->va = gtk_adjustment_new (0.0, 0.0, sheet->rows.max_used, 1.0, 1.0, 1.0);
	scg->ha = gtk_adjustment_new (0.0, 0.0, sheet->cols.max_used, 1.0, 1.0, 1.0);
	scg->hs = gtk_hscrollbar_new (GTK_ADJUSTMENT (scg->ha));
	scg->vs = gtk_vscrollbar_new (GTK_ADJUSTMENT (scg->va));

	gtk_signal_connect (GTK_OBJECT (scg->ha), "value_changed",
			    GTK_SIGNAL_FUNC (horizontal_scroll_change), scg);
	gtk_signal_connect (GTK_OBJECT (scg->va), "value_changed",
			    GTK_SIGNAL_FUNC (vertical_scroll_change), scg);
	gtk_signal_connect (GTK_OBJECT (scg->hs), "event",
			    GTK_SIGNAL_FUNC (horizontal_scroll_event), scg);
	gtk_signal_connect (GTK_OBJECT (scg->vs), "event",
			    GTK_SIGNAL_FUNC (vertical_scroll_event), scg);

	/* Attach the horizontal scroll */
	gtk_table_attach (table, scg->hs,
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_FILL,
			  0, 0);

	/* Attach the vertical scroll */
	gtk_table_attach (table, scg->vs,
			  2, 3, 1, 2,
			  GTK_FILL,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	sheet_view_set_zoom_factor (scg, 1.);
}

void
sheet_view_set_header_visibility (SheetControlGUI *scg,
				  gboolean col_headers_visible,
				  gboolean row_headers_visible)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (col_headers_visible){
		if (!GTK_WIDGET_VISIBLE (GTK_WIDGET (scg->col_canvas)))
			gtk_widget_show (GTK_WIDGET (scg->col_canvas));
	} else {
		if (GTK_WIDGET_VISIBLE (GTK_WIDGET (scg->col_canvas)))
			gtk_widget_hide (GTK_WIDGET (scg->col_canvas));
	}

	if (row_headers_visible){
		if (!GTK_WIDGET_VISIBLE (GTK_WIDGET (scg->row_canvas)))
			gtk_widget_show (GTK_WIDGET (scg->row_canvas));
	} else {
		if (GTK_WIDGET_VISIBLE (GTK_WIDGET (scg->row_canvas)))
			gtk_widget_hide (GTK_WIDGET (scg->row_canvas));
	}
}

GtkWidget *
sheet_view_new (Sheet *sheet)
{
	SheetControlGUI *scg;

	scg = gtk_type_new (sheet_view_get_type ());
	scg->sheet = sheet;
	scg->tip = NULL;

	sheet_view_construct (scg);

	return GTK_WIDGET (scg);
}

static void
sheet_view_destroy (GtkObject *object)
{
	SheetControlGUI *scg = SHEET_CONTROL_GUI (object);

	scg_mode_edit (scg); /* finish any object edits */

	/* Add shutdown code here */
	if (scg->tip)
		gtk_object_unref (GTK_OBJECT (scg->tip));

	if (scg->sheet)
		sheet_detach_sheet_view (scg);

	/* FIXME : Should we be pedantic and
	 * 1) clear the control points
	 * 2) remove ourselves from the sheets list of views ?
	 */
	if (GTK_OBJECT_CLASS (sheet_view_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (sheet_view_parent_class)->destroy)(object);
}

static void
scg_class_init (SheetControlGUIClass *Class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) Class;

	sheet_view_parent_class = gtk_type_class (gtk_table_get_type ());

	object_class->destroy = sheet_view_destroy;
}

GtkType
sheet_view_get_type (void)
{
	static GtkType sheet_view_type = 0;

	if (!sheet_view_type){
		GtkTypeInfo sheet_view_info = {
			"SheetControlGUI",
			sizeof (SheetControlGUI),
			sizeof (SheetControlGUIClass),
			(GtkClassInitFunc) scg_class_init,
			(GtkObjectInitFunc) scg_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		sheet_view_type = gtk_type_unique (gtk_table_get_type (), &sheet_view_info);
	}

	return sheet_view_type;
}

void
sheet_view_selection_unant (SheetControlGUI *scg)
{
	GList *l;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (scg->anted_cursors == NULL)
		return;

	for (l = scg->anted_cursors; l; l = l->next)
		gtk_object_destroy (GTK_OBJECT (l->data));

	g_list_free (scg->anted_cursors);
	scg->anted_cursors = NULL;
}

void
sheet_view_selection_ant (SheetControlGUI *scg)
{
	GnomeCanvasGroup *group;
	GList *l;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (scg->anted_cursors)
		sheet_view_selection_unant (scg);

	group = scg->selection_group;
	for (l = scg->sheet->selections; l; l = l->next){
		Range *ss = l->data;
		ItemCursor *item_cursor;

		item_cursor = ITEM_CURSOR (gnome_canvas_item_new (
			group, item_cursor_get_type (),
			"SheetControlGUI", scg,
			"Style", ITEM_CURSOR_ANTED,
			NULL));
		item_cursor_set_bounds (
			item_cursor,
			ss->start.col, ss->start.row,
			ss->end.col, ss->end.row);

		scg->anted_cursors = g_list_prepend (scg->anted_cursors, item_cursor);
	}
}

void
sheet_view_adjust_preferences (SheetControlGUI *scg)
{
	Sheet *sheet = scg->sheet;
	WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (scg->wbcg));

	if (sheet->show_col_header)
		gtk_widget_show (GTK_WIDGET (scg->col_canvas));
	else
		gtk_widget_hide (GTK_WIDGET (scg->col_canvas));

	if (sheet->show_row_header)
		gtk_widget_show (GTK_WIDGET (scg->row_canvas));
	else
		gtk_widget_hide (GTK_WIDGET (scg->row_canvas));

	if (sheet->show_col_header && sheet->show_row_header)
		gtk_widget_show (scg->select_all_btn);
	else
		gtk_widget_hide (scg->select_all_btn);

	if (wbv->show_horizontal_scrollbar)
		gtk_widget_show (scg->hs);
	else
		gtk_widget_hide (scg->hs);

	if (wbv->show_vertical_scrollbar)
		gtk_widget_show (scg->vs);
	else
		gtk_widget_hide (scg->vs);
}

StyleFont *
sheet_view_get_style_font (const Sheet *sheet, MStyle const * const mstyle)
{
	/* Scale the font size by the average scaling factor for the
	 * display.  72dpi is base size
	 */
	double const res  = application_dpi_to_pixels ();

	/* When previewing sheet can == NULL */
	double const zoom = (sheet) ? sheet->last_zoom_factor_used : 1.;

	return mstyle_get_font (mstyle, zoom * res);
}

/*****************************************************************************/

void
sheet_view_stop_sliding (SheetControlGUI *scg)
{
	if (scg->sliding == -1)
		return;

	gtk_timeout_remove (scg->sliding);
	scg->slide_handler = NULL;
	scg->slide_data = NULL;
	scg->sliding = -1;
}

static gint
sheet_view_sliding_callback (gpointer data)
{
	SheetControlGUI *scg = data;
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);
	gboolean change = FALSE;
	int col, row;

	col = scg->sliding_col;
	row = scg->sliding_row;

	if (scg->sliding_x < 0){
		if (gsheet->col.first){
			change = TRUE;
			if (scg->sliding_x >= -50)
				col = 1;
			else if (scg->sliding_x >= -100)
				col = 3;
			else
				col = 20;
			col = gsheet->col.first - col;
			if (col < 0)
				col = 0;
		} else
			col = 0;
	}

	if (scg->sliding_x > 0){
		if (gsheet->col.last_full < SHEET_MAX_COLS-1){
			change = TRUE;
			if (scg->sliding_x <= 50)
				col = 1;
			else if (scg->sliding_x <= 100)
				col = 3;
			else
				col = 20;
			col = gsheet->col.last_visible + col;
			if (col >= SHEET_MAX_COLS)
				col = SHEET_MAX_COLS-1;
		} else
			col = SHEET_MAX_COLS-1;
	}

	if (scg->sliding_y < 0){
		if (gsheet->row.first){
			change = TRUE;
			if (scg->sliding_y >= -30)
				row = 1;
			else if (scg->sliding_y >= -60)
				row = 25;
			else if (scg->sliding_y >= -100)
				row = 250;
			else
				row = 5000;
			row = gsheet->row.first - row;
			if (row < 0)
				row = 0;
		} else
			row = 0;
	}
	if (scg->sliding_y > 0){
		if (gsheet->row.last_full < SHEET_MAX_ROWS-1){
			change = TRUE;
			if (scg->sliding_y <= 30)
				row = 1;
			else if (scg->sliding_y <= 60)
				row = 25;
			else if (scg->sliding_y <= 100)
				row = 250;
			else
				row = 5000;
			row = gsheet->row.last_visible + row;
			if (row >= SHEET_MAX_ROWS)
				row = SHEET_MAX_ROWS-1;
		} else
			row = SHEET_MAX_ROWS-1;
	}

	if (!change) {
		sheet_view_stop_sliding (scg);
		return TRUE;
	}

	if (scg->slide_handler == NULL ||
	    (*scg->slide_handler) (scg, col, row, scg->slide_data))
		gnumeric_sheet_make_cell_visible (gsheet, col, row, FALSE);

	return TRUE;
}

gboolean
sheet_view_start_sliding (SheetControlGUI *scg,
			  SheetControlGUISlideHandler slide_handler,
			  gpointer user_data,
			  int col, int row, int dx, int dy)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);

	/* Do not slide off the edge */
	if (((dx == 0) ||
	     (dx < 0 && gsheet->col.first == 0) ||
	     (dx > 0 && gsheet->col.last_full >= SHEET_MAX_COLS-1)) &&
	    ((dy == 0) ||
	     (dy < 0 && gsheet->row.first == 0) ||
	     (dy > 0 && gsheet->row.last_full >= SHEET_MAX_ROWS-1))) {
		sheet_view_stop_sliding (scg);
		return FALSE;
	}

	scg->slide_handler = slide_handler;
	scg->slide_data = user_data;
	scg->sliding_x = dx;
	scg->sliding_y = dy;
	scg->sliding_col = col;
	scg->sliding_row = row;

	if (scg->sliding == -1) {
		sheet_view_sliding_callback (scg);

		scg->sliding = gtk_timeout_add (
			200, sheet_view_sliding_callback, scg);
	}
	return TRUE;
}

/***************************************************************************/

enum {
	CONTEXT_CUT	= 1,
	CONTEXT_COPY,
	CONTEXT_PASTE,
	CONTEXT_PASTE_SPECIAL,
	CONTEXT_INSERT,
	CONTEXT_DELETE,
	CONTEXT_CLEAR_CONTENT,
	CONTEXT_FORMAT_CELL,
	CONTEXT_COL_WIDTH,
	CONTEXT_COL_HIDE,
	CONTEXT_COL_UNHIDE,
	CONTEXT_ROW_HEIGHT,
	CONTEXT_ROW_HIDE,
	CONTEXT_ROW_UNHIDE
};
static gboolean
context_menu_hander (GnumericPopupMenuElement const *element,
		     gpointer user_data)
{
	SheetControlGUI *sheet_view = user_data;
	Sheet *sheet = sheet_view->sheet;
	WorkbookControlGUI *wbcg = sheet_view->wbcg;
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);

	g_return_val_if_fail (element != NULL, TRUE);
	g_return_val_if_fail (sheet != NULL, TRUE);

	switch (element->index) {
	case CONTEXT_CUT :
		sheet_selection_cut (wbc, sheet);
		break;
	case CONTEXT_COPY :
		sheet_selection_copy (wbc, sheet);
		break;
	case CONTEXT_PASTE :
		cmd_paste_to_selection (wbc, sheet, PASTE_DEFAULT);
		break;
	case CONTEXT_PASTE_SPECIAL : {
		int flags = dialog_paste_special (wbcg);
		if (flags != 0)
			cmd_paste_to_selection (wbc, sheet, flags);
		break;
	}
	case CONTEXT_INSERT :
		dialog_insert_cells (wbcg, sheet);
		break;
	case CONTEXT_DELETE :
		dialog_delete_cells (wbcg, sheet);
		break;
	case CONTEXT_CLEAR_CONTENT :
		cmd_clear_selection (wbc, sheet, CLEAR_VALUES);
		break;
	case CONTEXT_FORMAT_CELL :
		dialog_cell_format (wbcg, sheet, FD_CURRENT);
		break;
	case CONTEXT_COL_WIDTH :
		sheet_dialog_set_column_width (NULL, wbcg);
		break;
	case CONTEXT_COL_HIDE :
		cmd_hide_selection_colrow (wbc, sheet, TRUE, FALSE);
		break;
	case CONTEXT_COL_UNHIDE :
		cmd_hide_selection_colrow (wbc, sheet, TRUE, TRUE);
		break;
	case CONTEXT_ROW_HEIGHT :
		sheet_dialog_set_row_height (NULL, wbcg);
		break;
	case CONTEXT_ROW_HIDE :
		cmd_hide_selection_colrow (wbc, sheet, FALSE, FALSE);
		break;
	case CONTEXT_ROW_UNHIDE :
		cmd_hide_selection_colrow (wbc, sheet, FALSE, TRUE);
		break;
	default :
		break;
	};
	return TRUE;
}

void
scg_context_menu (SheetControlGUI *scg, GdkEventButton *event,
		  gboolean is_col, gboolean is_row)
{
	enum {
		CONTEXT_DISPLAY_FOR_CELLS = 1,
		CONTEXT_DISPLAY_FOR_ROWS = 2,
		CONTEXT_DISPLAY_FOR_COLS = 4
	};
	enum {
		CONTEXT_ENABLE_PASTE_SPECIAL = 1,
	};

	static GnumericPopupMenuElement const popup_elements[] = {
		{ N_("Cu_t"),           GNOME_STOCK_MENU_CUT,
		    0, 0, CONTEXT_CUT },
		{ N_("_Copy"),          GNOME_STOCK_MENU_COPY,
		    0, 0, CONTEXT_COPY },
		{ N_("_Paste"),         GNOME_STOCK_MENU_PASTE,
		    0, 0, CONTEXT_PASTE },
		{ N_("Paste _Special"),	NULL,
		    0, CONTEXT_ENABLE_PASTE_SPECIAL, CONTEXT_PASTE_SPECIAL },

		{ "", NULL, 0, 0, 0 },

		/* TODO : One day make the labels smarter.  Generate them to include
		 * quantities.
		 * 	eg : Insert 4 rows
		 * 	or : Insert row
		 * This is hard for now because there is no memory management for the label
		 * strings, and the logic that knows the count is elsewhere
		 */
		{ N_("_Insert..."),	NULL,
		    CONTEXT_DISPLAY_FOR_CELLS, 0, CONTEXT_INSERT },
		{ N_("_Delete..."),	NULL,
		    CONTEXT_DISPLAY_FOR_CELLS, 0, CONTEXT_DELETE },
		{ N_("_Insert Column(s)"), "Menu_Gnumeric_ColumnAdd",
		    CONTEXT_DISPLAY_FOR_COLS, 0, CONTEXT_INSERT },
		{ N_("_Delete Column(s)"), "Menu_Gnumeric_ColumnDelete",
		    CONTEXT_DISPLAY_FOR_COLS, 0, CONTEXT_DELETE },
		{ N_("_Insert Row(s)"), "Menu_Gnumeric_RowAdd",
		    CONTEXT_DISPLAY_FOR_ROWS, 0, CONTEXT_INSERT },
		{ N_("_Delete Row(s)"), "Menu_Gnumeric_RowDelete",
		    CONTEXT_DISPLAY_FOR_ROWS, 0, CONTEXT_DELETE },

		{ N_("Clear Co_ntents"),NULL,
		    0, 0, CONTEXT_CLEAR_CONTENT },

		/* TODO : Add the comment modification elements */
		{ "", NULL, 0, 0, 0 },

		{ N_("_Format Cells..."),GNOME_STOCK_MENU_PREF,
		    0, 0, CONTEXT_FORMAT_CELL },

		/* Column specific (Note some labels duplicate row labels) */
		{ N_("Column _Width..."), "Menu_Gnumeric_ColumnSize",
		    CONTEXT_DISPLAY_FOR_COLS, 0, CONTEXT_COL_WIDTH },
		{ N_("_Hide"),		  "Menu_Gnumeric_ColumnHide",
		    CONTEXT_DISPLAY_FOR_COLS, 0, CONTEXT_COL_HIDE },
		{ N_("_Unhide"),	  "Menu_Gnumeric_ColumnUnhide",
		    CONTEXT_DISPLAY_FOR_COLS, 0, CONTEXT_COL_UNHIDE },

		/* Row specific (Note some labels duplicate col labels) */
		{ N_("_Row Height..."),	  "Menu_Gnumeric_RowSize",
		    CONTEXT_DISPLAY_FOR_ROWS, 0, CONTEXT_ROW_HEIGHT },
		{ N_("_Hide"),		  "Menu_Gnumeric_RowHide",
		    CONTEXT_DISPLAY_FOR_ROWS, 0, CONTEXT_ROW_HIDE },
		{ N_("_Unhide"),	  "Menu_Gnumeric_RowUnhide",
		    CONTEXT_DISPLAY_FOR_ROWS, 0, CONTEXT_ROW_UNHIDE },

		{ NULL, NULL, 0, 0, 0 },
	};

	/* row and column specific operations */
	int const display_filter =
		((!is_col && !is_row) ? CONTEXT_DISPLAY_FOR_CELLS : 0) |
		(is_col ? CONTEXT_DISPLAY_FOR_COLS : 0) |
		(is_row ? CONTEXT_DISPLAY_FOR_ROWS : 0);

	/*
	 * Paste special does not apply to cut cells.  Enable
	 * when there is nothing in the local clipboard, or when
	 * the clipboard has the results of a copy.
	 */
	int const sensitivity_filter =
	    (application_clipboard_is_empty () ||
	    (application_clipboard_contents_get () != NULL))
	? CONTEXT_ENABLE_PASTE_SPECIAL : 0;

	workbook_finish_editing (scg->wbcg, FALSE);

	gnumeric_create_popup_menu (popup_elements, &context_menu_hander,
				    scg, display_filter,
				    sensitivity_filter, event);
}

static gboolean
cb_redraw_sel (Sheet *sheet, Range const *r, gpointer user_data)
{
	SheetControlGUI *scg = user_data;
	sheet_view_redraw_cell_region (scg,
		r->start.col, r->start.row, r->end.col, r->end.row);
	sheet_view_redraw_headers (scg, TRUE, TRUE, r);
	return TRUE;
}

static void
scg_cursor_visible (SheetControlGUI *scg, gboolean is_visible)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);
	item_cursor_set_visibility (gsheet->item_cursor, is_visible);
	selection_foreach_range (scg->sheet, TRUE, cb_redraw_sel, scg);
}

/***************************************************************************/

#define SO_CLASS(so) SHEET_OBJECT_CLASS(GTK_OBJECT(so)->klass)

/**
 * scg_object_destroy_control_points :
 *
 * Destroys the canvas items used as sheet control points
 */
static void
scg_object_destroy_control_points (SheetControlGUI *scg)
{
	int i;

	if (scg == NULL)
		return;

	i = sizeof (scg->control_points)/sizeof(GnomeCanvasItem *);
	while (i-- > 0) {
		gtk_object_destroy (GTK_OBJECT (scg->control_points [i]));
		scg->control_points [i] = NULL;
	}
}

static void
scg_object_stop_editing (SheetControlGUI *scg, SheetObject *so)
{
	if (so != NULL) {
		if (so == scg->current_object) {
			scg_object_destroy_control_points (scg);
			scg->current_object = NULL;
			if (SO_CLASS (so)->set_active != NULL)
				SO_CLASS (so)->set_active (so, FALSE);
#ifdef ENABLE_BONOBO
	/* FIXME FIXME FIXME : JEG 11/Sep/2000 */
	if (scg->active_object_frame) {
		bonobo_view_frame_view_deactivate (scg->active_object_frame);
		if (scg->active_object_frame != NULL)
			bonobo_view_frame_set_covered (scg->active_object_frame, TRUE);
		scg->active_object_frame = NULL;
	}
#endif
		}
	}
}

static gboolean
scg_mode_clear (SheetControlGUI *scg)
{
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), FALSE);

	if (scg->new_object != NULL) {
		gtk_object_unref (GTK_OBJECT (scg->new_object));
		scg->new_object = NULL;
	}
	scg_object_stop_editing (scg, scg->current_object);

	return TRUE;
}

/*
 * scg_mode_edit:
 * @scg:  The sheet
 *
 * Put @sheet into the standard state 'edit mode'.  This shuts down
 * any object editing and frees any objects that are created but not
 * realized.
 */
void
scg_mode_edit (SheetControlGUI *scg)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	scg_mode_clear (scg);
	scg_cursor_visible (scg, TRUE);

	if (workbook_edit_has_guru (scg->wbcg))
		workbook_finish_editing (scg->wbcg, FALSE);
}

/*
 * scg_mode_edit_object
 * @scg: The SheetControl to edit in.
 * @so : The SheetObject to select.
 *
 * Makes @so the currently selected object and prepares it for
 * user editing.
 */
void
scg_mode_edit_object (SheetControlGUI *scg, SheetObject *so)
{
	GtkObject *view = sheet_object_get_view (so, scg);

	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (scg_mode_clear (scg)) {
		scg->current_object = so;
		if (SO_CLASS (so)->set_active != NULL)
			SO_CLASS (so)->set_active (so, TRUE);
		scg_cursor_visible (scg, FALSE);
		scg_object_update_bbox (scg, so, GNOME_CANVAS_ITEM(view), NULL);
	}
}

/**
 * scg_mode_create_object :
 * @so : The object the needs to be placed
 *
 * Takes a newly created SheetObject that has not yet been realized and
 * prepares to place it on the sheet.
 */
void
scg_mode_create_object (SheetControlGUI *scg, SheetObject *so)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (scg_mode_clear (scg)) {
		scg->new_object = so;
		scg_cursor_visible (scg, FALSE);
	}
}

static void
display_object_menu (SheetObject *so, GnomeCanvasItem *view, GdkEvent *event)
{
	SheetControlGUI *scg = sheet_object_view_control (GTK_OBJECT (view));
	GtkMenu *menu;

	scg_mode_edit_object (scg, so);
	menu = GTK_MENU (gtk_menu_new ());
	SHEET_OBJECT_CLASS (GTK_OBJECT(so)->klass)->populate_menu (so, view, menu);

	gtk_widget_show_all (GTK_WIDGET (menu));
	gnumeric_popup_menu (menu, &event->button);
}

static gboolean
cb_slide_handler (SheetControlGUI *scg, int col, int row, gpointer ignored)
{
	return TRUE;
}

/**
 * cb_control_point_event :
 *
 * Event handler for the control points.
 * Index & cursor type are stored as user data associated with the CanvasItem
 */
static int
cb_control_point_event (GnomeCanvasItem *ctrl_pt, GdkEvent *event,
			GnomeCanvasItem *so_view)
{
	/*  FIXME : move these statics into the control */
	static gdouble last_x, last_y;
	SheetObject *so = sheet_object_view_obj (GTK_OBJECT (so_view));
	SheetControlGUI *scg = sheet_object_view_control (GTK_OBJECT (so_view));
	int idx;

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
	{
		gpointer p = gtk_object_get_data (GTK_OBJECT (ctrl_pt), "cursor");
		e_cursor_set_widget (ctrl_pt->canvas, GPOINTER_TO_UINT (p));
		break;
	}

	case GDK_BUTTON_RELEASE:
		if (scg->drag_object != so)
			return FALSE;

		sheet_view_stop_sliding (scg);
		scg->drag_object = NULL;
		gnome_canvas_item_ungrab (ctrl_pt, event->button.time);
		sheet_object_position (so, NULL);
		break;

	case GDK_BUTTON_PRESS:
		sheet_view_stop_sliding (scg);

		switch (event->button.button) {
		case 1:
		case 2: scg->drag_object = so;
			gnome_canvas_item_grab (ctrl_pt,
						GDK_POINTER_MOTION_MASK |
						GDK_BUTTON_RELEASE_MASK,
						NULL, event->button.time);
			last_x = event->button.x;
			last_y = event->button.y;
			break;

		case 3: display_object_menu (so, so_view, event);
			break;

		default: /* Ignore mouse wheel events */
			return FALSE;
		}
		break;

	case GDK_MOTION_NOTIFY: {
		GnomeCanvas *canvas = GNOME_CANVAS (scg->canvas);
		GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);
		double new_coords [4], dx, dy;
		int i, col, row, x, y, left, top, width, height;

		if (scg->drag_object != so)
			return FALSE;

		gnome_canvas_w2c (canvas, event->motion.x, event->motion.y, &x, &y);
		gnome_canvas_get_scroll_offsets (canvas, &left, &top);

		width = scg->canvas->allocation.width;
		height = scg->canvas->allocation.height;

		col = gnumeric_sheet_find_col (gsheet, x, NULL);
		row = gnumeric_sheet_find_row (gsheet, y, NULL);

		if (x < left || y < top || x >= left + width || y >= top + height) {
			int dx = 0, dy = 0;

			if (x < left)
				dx = x - left;
			else if (x >= left + width)
				dx = x - width - left;

			if (y < top)
				dy = y - top;
			else if (y >= top + height)
				dy = y - height - top;


			if (sheet_view_start_sliding (scg, cb_slide_handler,
						      NULL, col, row, dx, dy))

				return TRUE;
		}
		sheet_view_stop_sliding (scg);

		idx = GPOINTER_TO_INT (gtk_object_get_user_data (GTK_OBJECT (ctrl_pt)));

		/* FIXME : When we get rid of these stupid statics 
		 * we can make the sliding work properly.  Currently while autosliding
		 * the object is not changed until a motion event.
		 */
		dx = event->button.x - last_x;
		dy = event->button.y - last_y;
		last_x = event->button.x;
		last_y = event->button.y;

		for (i = 4; i-- > 0; )
			new_coords [i] = scg->object_coords [i];

		switch (idx) {
		case 0: new_coords [0] += dx;
			new_coords [1] += dy;
			break;
		case 1: new_coords [1] += dy;
			break;
		case 2: new_coords [1] += dy;
			new_coords [2] += dx;
			break;
		case 3: new_coords [0] += dx;
			break;
		case 4: new_coords [2] += dx;
			break;
		case 5: new_coords [0] += dx;
			new_coords [3] += dy;
			break;
		case 6: new_coords [3] += dy;
			break;
		case 7: new_coords [2] += dx;
			new_coords [3] += dy;
			break;
		case 8: new_coords [0] += dx;
			new_coords [1] += dy;
			new_coords [2] += dx;
			new_coords [3] += dy;
			break;

		default:
			g_warning ("Should not happen %d", idx);
		}

		/* Tell the object to update its co-ordinates */
		scg_object_update_bbox (scg, so, so_view, new_coords);
		break;
	}

	default:
		return FALSE;
	}
	return TRUE;
}

/*
 * new_control_point
 * @group:  The canvas group to which this control point belongs
 * @so_view: The sheet object view
 * @idx:    control point index to be created
 * @x:      x coordinate of control point
 * @y:      y coordinate of control point
 *
 * This is used to create a number of control points in a sheet
 * object, the meaning of them is used in other parts of the code
 * to belong to the following locations:
 *
 *     0 -------- 1 -------- 2
 *     |                     |
 *     3                     4
 *     |                     |
 *     5 -------- 6 -------- 7
 */
static GnomeCanvasItem *
new_control_point (GnomeCanvasGroup *group, GtkObject *so_view,
		   int idx, double x, double y, ECursorType ct)
{
	GnomeCanvasItem *item;

	item = gnome_canvas_item_new (
		group,
		gnome_canvas_rect_get_type (),
		"x1",    x - 2,
		"y1",    y - 2,
		"x2",    x + 2,
		"y2",    y + 2,
		"outline_color", "black",
		"fill_color",    "black",
		NULL);

	gtk_signal_connect (GTK_OBJECT (item), "event",
			    GTK_SIGNAL_FUNC (cb_control_point_event),
			    so_view);

	gtk_object_set_user_data (GTK_OBJECT (item), GINT_TO_POINTER (idx));
	gtk_object_set_data (GTK_OBJECT (item), "cursor", GINT_TO_POINTER (ct));

	return item;
}

/**
 * set_item_x_y:
 *
 * Changes the x and y position of the idx-th control point,
 * creating the control point if necessary.
 */
static void
set_item_x_y (SheetControlGUI *scg, GtkObject *so_view, int idx,
	      double x, double y, ECursorType ct)
{
	if (scg->control_points [idx] == NULL)
		scg->control_points [idx] = new_control_point (
			scg->object_group, so_view, idx, x, y, ct);
	else
		gnome_canvas_item_set (
		       scg->control_points [idx],
		       "x1", x - 2,
		       "x2", x + 2,
		       "y1", y - 2,
		       "y2", y + 2,
		       NULL);
}

static void
set_acetate_coords (SheetControlGUI *scg, GtkObject *so_view,
		    double l, double t, double r, double b)
{
	l -= 10.; r += 10.;
	t -= 10.; b += 10.;

	if (scg->control_points [8] == NULL) {
		GnomeCanvasItem *item;
		GtkWidget *event_box = gtk_event_box_new ();

		item = gnome_canvas_item_new (
			scg->object_group,
			gnome_canvas_widget_get_type (),
			"widget", event_box,
			"x",      l,
			"y",      t,
			"width",  r - l + 1.,
			"height", b - t + 1.,
			NULL);
		gtk_signal_connect (GTK_OBJECT (item), "event",
				    GTK_SIGNAL_FUNC (cb_control_point_event),
				    so_view);
		gtk_object_set_user_data (GTK_OBJECT (item), GINT_TO_POINTER (8));
		gtk_object_set_data (GTK_OBJECT (item), "cursor",
				     GINT_TO_POINTER (E_CURSOR_MOVE));

		scg->control_points [8] = item;
	} else
		gnome_canvas_item_set (
		       scg->control_points [8],
		       "x",      l,
		       "y",      t,
		       "width",  r - l + 1.,
		       "height", b - t + 1.,
		       NULL);
}

/**
 * scg_object_update_bbox:
 *
 * @scg : The Sheet control
 * @so : the optional sheet object
 * @so_view: A canvas item representing the view in this control
 * @new_coords : optionally jump the object to new coordinates
 *
 * Re-align the control points so that they appear at the correct verticies for
 * this view of the object.  If the object is not specified
 */
void
scg_object_update_bbox (SheetControlGUI *scg, SheetObject *so,
			GnomeCanvasItem *so_view, double const *new_coords)
{
	double l, t, r ,b;
	GtkObject *so_view_obj;
	
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (so == NULL)
		so = scg->current_object;
	if (so == NULL)
		return;
	g_return_if_fail (IS_SHEET_OBJECT (so));

	so_view_obj = (so_view == NULL)
		? sheet_object_get_view (so, scg) : GTK_OBJECT (so_view);

	if (new_coords != NULL)
		scg_object_calc_position (scg, so, new_coords);
	l = scg->object_coords [0];
	t = scg->object_coords [1];
	r = scg->object_coords [2];
	b = scg->object_coords [3];

	/* set the acetate 1st so that the other points
	 * will override it
	 */
	set_acetate_coords (scg, so_view_obj, l, t, r, b);

	set_item_x_y (scg, so_view_obj, 0, l, t,
		      E_CURSOR_SIZE_TL);
	set_item_x_y (scg, so_view_obj, 1, (l + r) / 2, t,
		      E_CURSOR_SIZE_Y);
	set_item_x_y (scg, so_view_obj, 2, r, t,
		      E_CURSOR_SIZE_TR);
	set_item_x_y (scg, so_view_obj, 3, l, (t + b) / 2,
		      E_CURSOR_SIZE_X);
	set_item_x_y (scg, so_view_obj, 4, r, (t + b) / 2,
		      E_CURSOR_SIZE_X);
	set_item_x_y (scg, so_view_obj, 5, l, b,
		      E_CURSOR_SIZE_TR);
	set_item_x_y (scg, so_view_obj, 6, (l + r) / 2, b,
		      E_CURSOR_SIZE_Y);
	set_item_x_y (scg, so_view_obj, 7, r, b,
		      E_CURSOR_SIZE_TL);
}

static int
calc_obj_place (GnumericSheet *gsheet, int pixel, gboolean is_col, 
		SheetObjectAnchor anchor_type, float *offset)
{
	int origin;
	int colrow;
	ColRowInfo *cri;

	if (is_col) {
		colrow = gnumeric_sheet_find_col (gsheet, pixel, &origin);
		cri = sheet_col_get_info (gsheet->scg->sheet, colrow);
	} else {
		colrow = gnumeric_sheet_find_row (gsheet, pixel, &origin);
		cri = sheet_row_get_info (gsheet->scg->sheet, colrow);
	}

	/* TODO : handle other anchor types */
	*offset = ((float) (pixel - origin))/ ((float) cri->size_pixels);
	if (anchor_type == SO_ANCHOR_PERCENTAGE_FROM_COLROW_END)
		*offset = 1. - *offset;
	return colrow;
}

void
scg_object_calc_position (SheetControlGUI *scg, SheetObject *so, double const *coords)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (scg->canvas);
	int	i, pixels [4];
	float	fraction [4];
	double	tmp [4];
	Range	range;

	if (coords [0] > coords [2]) {
		tmp [0] = coords [2];
		tmp [2] = coords [0];
	} else {
		tmp [0] = coords [0];
		tmp [2] = coords [2];
	}
	if (coords [1] > coords [3]) {
		tmp [1] = coords [3];
		tmp [3] = coords [1];
	} else {
		tmp [1] = coords [1];
		tmp [3] = coords [3];
	}

	for (i = 4; i-- > 0 ;)
		scg->object_coords [i] = coords [i];

	gnome_canvas_w2c (GNOME_CANVAS (scg->canvas),
			  tmp [0], tmp [1],
			  pixels +0, pixels + 1);
	gnome_canvas_w2c (GNOME_CANVAS (scg->canvas),
			  tmp [2], tmp [3],
			  pixels +2, pixels + 3);
	range.start.col = calc_obj_place (gsheet, pixels [0], TRUE,
		so->anchor_type [0], fraction + 0);
	range.start.row = calc_obj_place (gsheet, pixels [1], FALSE,
		so->anchor_type [1], fraction + 1);
	range.end.col = calc_obj_place (gsheet, pixels [2], TRUE,
		so->anchor_type [2], fraction + 2);
	range.end.row = calc_obj_place (gsheet, pixels [3], FALSE,
		so->anchor_type [3], fraction + 3);

	sheet_object_range_set (so, &range, fraction, NULL);
}

void
scg_object_view_position (SheetControlGUI *scg, SheetObject *so, double *coords)
{
	int pixels [4];

	sheet_object_position_pixels (so, scg, pixels);
	gnome_canvas_c2w (GNOME_CANVAS (scg->canvas),
			  pixels [0], pixels [1],
			  coords +0, coords + 1);
	gnome_canvas_c2w (GNOME_CANVAS (scg->canvas),
			  pixels [2], pixels [3],
			  coords +2, coords + 3);
}

static void
cb_sheet_object_destroy (GtkObject *view, SheetObject *so)
{
	SheetControlGUI	*scg = sheet_object_view_control (view);

	if (scg) {
		if (scg->current_object == so)
			scg_mode_edit (scg);
		else
			scg_object_stop_editing (scg, so);
	}
}

/**
 * cb_sheet_object_canvas_event :
 * @item : The canvas item that recieved the event
 * @event: The event
 * @so   : The sheetobject the itm is a view of.
 *
 * Handle basic events and manipulations of sheet objects.
 */
static int
cb_sheet_object_canvas_event (GnomeCanvasItem *item, GdkEvent *event,
			      SheetObject *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), FALSE);

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		e_cursor_set_widget (item->canvas,
			(so->type == SHEET_OBJECT_ACTION_STATIC)
			? E_CURSOR_ARROW : E_CURSOR_PRESS);
		break;

	case GDK_BUTTON_PRESS: {
		SheetControlGUI *scg = sheet_object_view_control (GTK_OBJECT (item));

		/* Ignore mouse wheel events */
		if (event->button.button > 3)
			return FALSE;

		if (scg->current_object != so)
			scg_mode_edit_object (scg, so);

		if (event->button.button < 3) {
			g_return_val_if_fail (scg->drag_object == NULL, FALSE);
			scg->drag_object = so;

			/* grab the acetate */
			gnome_canvas_item_grab (scg->control_points [8],
						GDK_POINTER_MOTION_MASK |
						GDK_BUTTON_RELEASE_MASK,
						NULL, event->button.time);
		} else
			display_object_menu (so, item, event);
		break;
	}

	default:
		return FALSE;
	}
	return TRUE;
}

/**
 * cb_sheet_object_widget_canvas_event:
 * @widget: The widget it happens on
 * @event:  The event.
 * @item:   The canvas item.
 *
 * Simplified event handler that passes most events to the widget, and steals
 * only pressing Button3
 */
static int
cb_sheet_object_widget_canvas_event (GtkWidget *widget, GdkEvent *event,
				     GnomeCanvasItem *view)
{
	if (event->type == GDK_BUTTON_PRESS && event->button.button == 3) {
		SheetObject *so = sheet_object_view_obj (GTK_OBJECT (view));
		SheetControlGUI *scg = sheet_object_view_control (GTK_OBJECT (view));

		g_return_val_if_fail (so != NULL, FALSE);

		scg_mode_edit_object (scg, so);
		return cb_sheet_object_canvas_event (view, event, so);
	}

	return FALSE;
}

/**
 * scg_object_register :
 *
 * @so : A sheet object
 * @view   : A canvas item acting as a view for @so
 *
 * Setup some standard callbacks for manipulating a view of a sheet object.
 */
void
scg_object_register (SheetObject *so, GnomeCanvasItem *view)
{
	gtk_signal_connect (GTK_OBJECT (view), "event",
			    GTK_SIGNAL_FUNC (cb_sheet_object_canvas_event),
			    so);
	gtk_signal_connect (GTK_OBJECT (view), "destroy",
			    GTK_SIGNAL_FUNC (cb_sheet_object_destroy),
			    so);
}

/**
 * scg_object_widget_register :
 *
 * @so : A sheet object
 * @widget : The widget for the sheet object view
 * @view   : A canvas item acting as a view for @so
 *
 * Setup some standard callbacks for manipulating widgets as views of sheet
 * objects.
 */
void
scg_object_widget_register (SheetObject *so, GtkWidget *widget,
			    GnomeCanvasItem *view)
{
	gtk_signal_connect (GTK_OBJECT (widget), "event",
			    GTK_SIGNAL_FUNC (cb_sheet_object_widget_canvas_event),
			    view);
	scg_object_register (so, view);
}

/***************************************************************************/

static void
scg_comment_timer_clear (SheetControlGUI *scg)
{
	if (scg->comment.timer != -1) {
		gtk_timeout_remove (scg->comment.timer);
		scg->comment.timer = -1;
	}
}

/**
 * scg_comment_display :
 * @scg : The SheetControl
 * @cc  : A cell comment
 *
 * Simplistic routine to display the text of a comment in an UGLY popup window.
 * FIXME : this should really bring up another sheetobject with an arrow from
 * it to the comment marker.  However, we lack a decent rich text canvas item
 * until the conversion to pange and the new text box.
 */
void
scg_comment_display (SheetControlGUI *scg, CellComment *cc)
{
	GtkWidget *label;
	int x, y;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	g_return_if_fail (IS_CELL_COMMENT (scg->comment.selected));

	scg_comment_timer_clear (scg);
	if (cc == NULL)
		cc = scg->comment.selected;
	else if (scg->comment.selected != cc)
		scg_comment_unselect (scg, scg->comment.selected);

	g_return_if_fail (IS_CELL_COMMENT (cc));

	scg->comment.item = gtk_window_new (GTK_WINDOW_POPUP);
	label = gtk_label_new (cell_comment_text_get (cc));
	gtk_container_add (GTK_CONTAINER (scg->comment.item), label);
	gdk_window_get_pointer (NULL, &x, &y, NULL);
	gtk_widget_set_uposition (scg->comment.item, x+10, y+10);
	gtk_widget_show_all (scg->comment.item);
}

/**
 * cb_cell_comment_timer :
 *
 * Utility routine to disaply a comment after a short delay.
 */
static gint
cb_cell_comment_timer (SheetControlGUI *scg)
{
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), FALSE);
	g_return_val_if_fail (scg->comment.timer != -1, FALSE);

	scg->comment.timer = -1;
	scg_comment_display (scg, scg->comment.selected);
	return FALSE;
}

/**
 * scg_comment_select :
 * @scg : The SheetControl
 * @cc  : A cell comment
 *
 * Prepare @cc for display.
 */
void
scg_comment_select (SheetControlGUI *scg, CellComment *cc)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	g_return_if_fail (scg->comment.timer == -1);
	
	if (scg->comment.selected != NULL)
		scg_comment_unselect (scg, scg->comment.selected);

	scg->comment.selected = cc;
	scg->comment.timer = gtk_timeout_add (1000,
		(GtkFunction)cb_cell_comment_timer, scg);
}

/**
 * scg_comment_unselect :
 * @scg : The SheetControl
 * @cc  : A cell comment
 *
 * If @cc is the current cell comment being edited/displayed shutdown the
 * display mechanism.
 */
void
scg_comment_unselect (SheetControlGUI *scg, CellComment *cc)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (cc == scg->comment.selected) {
		scg->comment.selected = NULL;
		scg_comment_timer_clear (scg);

		if (scg->comment.item != NULL) {
			gtk_object_destroy (GTK_OBJECT (scg->comment.item));
			scg->comment.item = NULL;
		}
	}
}

/************************************************************************/
/* Col/Row size support routines.  */

int
scg_colrow_distance_get (SheetControlGUI const *scg, gboolean is_cols,
		  int from, int to)
{
	ColRowCollection const *collection;
	int default_size;
	int i, pixels = 0;
	int sign = 1;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), 1);

	if (from > to) {
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1;
	}

	g_return_val_if_fail (from >= 0, 1);

	if (is_cols) {
		g_return_val_if_fail (to <= SHEET_MAX_COLS, 1);
		collection = &scg->sheet->cols;
	} else {
		g_return_val_if_fail (to <= SHEET_MAX_ROWS, 1);
		collection = &scg->sheet->rows;
	}

	/* Do not use col_row_foreach, it ignores empties.
	 * Optimize this so that long jumps are not quite so horrific
	 * for performance.
	 */
	default_size = collection->default_style.size_pixels;
	for (i = from ; i < to ; ++i) {
		ColRowSegment const *segment =
			COLROW_GET_SEGMENT(collection, i);

		if (segment != NULL) {
			ColRowInfo const *cri = segment->info [COLROW_SUB_INDEX(i)];
			if (cri == NULL)
				pixels += default_size;
			else if (cri->visible)
				pixels += cri->size_pixels;
		} else {
			int segment_end = COLROW_SEGMENT_END(i)+1;
			if (segment_end > to)
				segment_end = to;
			pixels += default_size * (segment_end - i);
			i = segment_end-1;
		}
	}

	return pixels*sign;
}

/*************************************************************************/

void
scg_set_cursor_bounds (SheetControlGUI *scg,
		       int start_col, int start_row, int end_col, int end_row)
{
	gnumeric_sheet_set_cursor_bounds (GNUMERIC_SHEET (scg->canvas),
					  start_col, start_row,
					  end_col, end_row);
}

void
scg_compute_visible_region (SheetControlGUI *scg, gboolean full_recompute)
{
	gsheet_compute_visible_region (GNUMERIC_SHEET (scg->canvas),
				       full_recompute);
}

void
scg_make_cell_visible (SheetControlGUI  *scg, int col, int row,
		       gboolean force_scroll)
{
	gnumeric_sheet_make_cell_visible (GNUMERIC_SHEET (scg->canvas),
					  col, row, FALSE);
}

void
scg_create_editor (SheetControlGUI *scg)
{
	gnumeric_sheet_create_editor (GNUMERIC_SHEET (scg->canvas));
}
void
scg_stop_editing (SheetControlGUI *scg)
{
	gnumeric_sheet_stop_editing (GNUMERIC_SHEET (scg->canvas));
}

void
scg_stop_cell_selection	(SheetControlGUI *scg, gboolean clear_string)
{
	gnumeric_sheet_stop_cell_selection (GNUMERIC_SHEET (scg->canvas), clear_string);
}
