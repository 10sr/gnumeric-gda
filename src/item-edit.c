/*
 * item-edit.c : Edit facilities for worksheets.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg (jgoldberg@home.org)
 *
 * This module provides:
 *   * Integration of an in-sheet text editor (GtkEntry) with the Workbook
 *     GtkEntry as a canvas item.
 *
 *   * Feedback on expressions in the spreadsheet (referenced cells or
 *     ranges are highlighted on the spreadsheet).
 *
 * (C) 1999, 2000 Miguel de Icaza
 */
#include <config.h>

#include "item-edit.h"
#include "item-cursor.h"
#include "item-grid.h"
#include "gnumeric-sheet.h"
#include "sheet-control-gui.h"
#include "value.h"
#include "ranges.h"
#include "style.h"
#include "parse-util.h"
#include "sheet-merge.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "gnumeric-util.h"

#include <ctype.h>
#include <string.h>

static GnomeCanvasItem *item_edit_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_ITEM_GRID,		/* The ItemGrid * argument */
};

static void
scan_at (const char *text, int *scan)
{
	while (*scan > 0){
		const char *p = &text [(*scan)-1];
		char c = *p;

		if (!(c == ':' || c == '$' || isalnum ((unsigned char)*p)))
			break;

		(*scan)--;
	}
}

static gboolean
setup_range_from_value (Range *range, Value *v)
{
	if (v->v_range.cell.a.sheet != v->v_range.cell.a.sheet){
		value_release (v);
		return FALSE;
	}

	range->start.col = v->v_range.cell.a.col;
	range->start.row = v->v_range.cell.a.row;
	range->end.col   = v->v_range.cell.b.col;
	range->end.row   = v->v_range.cell.b.row;
	value_release (v);
	return TRUE;
}

/*
 * This routine could definitely be better.
 *
 * Currently it will not handle ranges like R[-1]C1, nor named ranges,
 * nor will it detect whether something is part of an expression or a
 * string .
 */
static gboolean
point_is_inside_range (ItemEdit *item_edit, const char *text, Range *range)
{
	Value *v;
	int text_len, cursor_pos, scan;

	if ((text = gnumeric_char_start_expr_p (text)) == NULL)
		return FALSE;

	text_len = strlen (text);
	cursor_pos = GTK_EDITABLE (item_edit->entry)->current_pos;
	if (cursor_pos == 0)
		return FALSE;
	cursor_pos--;

	scan = cursor_pos;
	scan_at (text, &scan);

	if ((v = range_parse (item_edit->scg->sheet, &text [scan], FALSE)) != NULL)
		return setup_range_from_value (range, v);

	if (scan == cursor_pos && scan > 0)
		scan--;
	scan_at (text, &scan);

	if ((v = range_parse (item_edit->scg->sheet, &text [scan], FALSE)) != NULL)
		return setup_range_from_value (range, v);

	return FALSE;
}

static void
entry_create_feedback_range (ItemEdit *item_edit, Range *range)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);

	if (!item_edit->feedback_cursor){
		item_edit->feedback_cursor = gnome_canvas_item_new (
			GNOME_CANVAS_GROUP (item->canvas->root),
			item_cursor_get_type (),
			"SheetControlGUI",  item_edit->scg,
			"Grid",   item_edit->item_grid,
			"Style",  ITEM_CURSOR_BLOCK,
			"Color",  "red",
			NULL);
	}

	item_cursor_set_bounds (
		ITEM_CURSOR (item_edit->feedback_cursor),
		range->start.col, range->start.row,
		range->end.col, range->end.row);
}

static void
entry_destroy_feedback_range (ItemEdit *item_edit)
{
	if (item_edit->feedback_cursor){
		gtk_object_destroy (GTK_OBJECT (item_edit->feedback_cursor));
		item_edit->feedback_cursor = NULL;
	}
}

/* WARNING : DO NOT CALL THIS FROM FROM UPDATE.  It may create another
 *           canvas-item which would in turn call update and confuse the
 *           canvas.
 */
static void
scan_for_range (ItemEdit *item_edit, const char *text)
{
	Range range;

	if (point_is_inside_range (item_edit, text, &range)){
		if (!item_edit->feedback_disabled)
			entry_create_feedback_range (item_edit, &range);
	} else
		entry_destroy_feedback_range (item_edit);
}

static void
item_edit_draw_cursor (ItemEdit *item_edit, GdkDrawable *drawable, GtkStyle *style,
		       int x, int y, GdkFont *font)
{
	if (!item_edit->cursor_visible)
		return;

	gdk_draw_line (drawable, style->black_gc,
		       x, y-font->ascent,
		       x, y+font->descent);
}

static void
item_edit_draw_text (ItemEdit *item_edit, GdkDrawable *drawable, GtkStyle *style,
		     gint x, gint y, const gchar *text, gint text_length,
		     int const cursor_pos)
{
	GdkFont *font = item_edit->font;

	GdkGC *gc = style->black_gc;

	/* If this segment contains the cursor draw it */
	if (0 <= cursor_pos && cursor_pos <= text_length) {
		if (cursor_pos > 0) {
			gdk_draw_text (drawable, font, gc, x, y, text, cursor_pos);
			x += gdk_text_width (font, text, cursor_pos);
			text += cursor_pos;
			text_length -= cursor_pos;
		}

		item_edit_draw_cursor (item_edit, drawable, style, x, y, font);
	}

	if (text_length > 0){
		if (workbook_auto_completing (item_edit->scg->wbcg)){
			gdk_draw_rectangle (
				drawable, style->black_gc, TRUE,
				x, y - font->ascent,
				gdk_text_width (font, text, text_length),
				font->ascent + font->descent);
			gc = style->white_gc;
		}

		gdk_draw_text (drawable, font, gc, x, y, text, text_length);
	}
}

static void
item_edit_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
		int x, int y, int width, int height)
{
	GtkWidget *canvas   = GTK_WIDGET (item->canvas);
	ItemEdit *item_edit = ITEM_EDIT (item);
	ColRowInfo const * const ci = sheet_col_get_info (item_edit->scg->sheet,
							  item_edit->pos.col);
	int const left_pos = ((int)item->x1) + ci->margin_a - x;

	/* NOTE : This does not handle vertical alignment yet so there may be some
	 * vertical jumping when edit.
	 */
	int top_pos = ((int)item->y1) - y + 1; /* grid line */
	int text_offset = 0;
	int cursor_pos = GTK_EDITABLE (item_edit->entry)->current_pos;
	GSList *ptr;
	const char *text;

	/* no drawing until the font is set */
	if (item_edit->font == NULL)
		return;
	top_pos += item_edit->font->ascent;

       	/* Draw the background (recall that gdk_draw_rectangle excludes far coords) */
	gdk_draw_rectangle (
		drawable, canvas->style->white_gc, TRUE,
		((int)item->x1)-x, ((int)item->y1)-y,
		(int)(item->x2-item->x1),
		(int)(item->y2-item->y1));

	/* Make a number of tests for auto-completion */
	text = workbook_edit_get_display_text (item_edit->scg->wbcg);

	for (ptr = item_edit->text_offsets; ptr != NULL; ptr = ptr->next){
		int const text_end = GPOINTER_TO_INT (ptr->data);

		item_edit_draw_text (item_edit, drawable, canvas->style,
				     left_pos, top_pos, text+text_offset, text_end-text_offset,
				     cursor_pos-text_offset);
		text_offset = text_end;
		top_pos += item_edit->font_height;
	}

	/* draw the remainder */
	item_edit_draw_text (item_edit, drawable, canvas->style,
			     left_pos, top_pos, text+text_offset,
			     strlen (text+text_offset),
			     cursor_pos-text_offset);
}

static double
item_edit_point (GnomeCanvasItem *item, double c_x, double c_y, int cx, int cy,
		 GnomeCanvasItem **actual_item)
{
	*actual_item = NULL;
	if ((cx < item->x1) || (cy < item->y1) || (cx >= item->x2) || (cy >= item->y2))
		return 10000.0;

	*actual_item = item;
	return 0.0;
}

static void
item_edit_translate (GnomeCanvasItem *item, double dx, double dy)
{
	g_warning ("item_cursor_translate %g, %g\n", dx, dy);
}

static int
item_edit_event (GnomeCanvasItem *item, GdkEvent *event)
{
	/* FIXME : Should we handle mouse events here ? */
	return 0;
}

static void
recalc_spans (GnomeCanvasItem *item)
{
	ItemEdit *item_edit = ITEM_EDIT (item);
	Sheet    *sheet     = item_edit->scg->sheet;
	GdkFont  *font      = item_edit->font;
	const char *start = workbook_edit_get_display_text (item_edit->scg->wbcg);
	const char *text  = start;
	int col_span, row_span, tmp;
	GSList	*text_offsets = NULL;
	Range const *merged;

	/* Adjust the spans */
	GnumericSheet *gsheet = GNUMERIC_SHEET (item->canvas);
	int cur_line = 1;
	int cur_col = item_edit->pos.col, max_col = cur_col;
	ColRowInfo const * cri = sheet_col_get_info (sheet, cur_col);

	/* Start after the grid line and the left margin */
	int left_in_col = cri->size_pixels - cri->margin_a - 1;
	int ignore_rows = 0;

	/* the entire string */
	while (*text) {
		int pos_size = gdk_text_width (font, text++, 1);

		/* Be wary of large fonts and small columns */
		while (left_in_col < pos_size) {
			do {
				++cur_col;
				if (cur_col > gsheet->col.last_full || cur_col >= SHEET_MAX_COLS) {
					int offset = text - start - 1;
					if (offset < 0)
						offset = 0;

					cur_line++;
					cur_col = item_edit->pos.col;
					text_offsets = g_slist_prepend (text_offsets,
									GINT_TO_POINTER(offset));
					if (item->y1 + cur_line * item_edit->font_height >
					    gsheet->row_offset.last_visible)
						ignore_rows++;
				} else if (max_col < cur_col)
					max_col = cur_col;

				cri = sheet_col_get_info (sheet, cur_col);
				g_return_if_fail (cri != NULL);

				/* Be careful not to allow for the potential
				 * of an infinite loop if we somehow start on an
				 * invisible column
				 */
			} while (!cri->visible && cur_col != item_edit->pos.col);

			if (cur_col == item_edit->pos.col)
				left_in_col = cri->size_pixels - cri->margin_a - 1;
			else
				left_in_col += cri->size_pixels;
		}
		left_in_col -= pos_size;
	}
	item_edit->col_span = 1 + max_col - item_edit->pos.col;
	item_edit->lines = cur_line;

	if (item_edit->text_offsets != NULL)
		g_slist_free (item_edit->text_offsets);
	item_edit->text_offsets = g_slist_reverse (text_offsets);

	col_span = item_edit->col_span;
	merged = sheet_merge_is_corner (sheet, &item_edit->pos);
	if (merged != NULL) {
		int tmp = merged->end.col - merged->start.col + 1;
		if (col_span < tmp)
			col_span = tmp;
		row_span = merged->end.row - merged->start.row + 1;
	} else
		row_span = 1;

	/* The lower right is based on the span size excluding the grid lines
	 * Recall that the bound excludes the far point
	 */
	item->x2 = 1 + item->x1 - 2 +
		scg_colrow_distance_get (item_edit->scg, TRUE, item_edit->pos.col,
					 item_edit->pos.col + col_span);

	tmp = scg_colrow_distance_get (item_edit->scg, FALSE, item_edit->pos.row,
				       item_edit->pos.row + row_span) - 2;
	item->y2 = 1 + item->y1 +
		MAX (item_edit->lines * item_edit->font_height, tmp);

	gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent), item);
}

static void
item_edit_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	ItemEdit *item_edit = ITEM_EDIT (item);

	if (GNOME_CANVAS_ITEM_CLASS (item_edit_parent_class)->update)
		(*GNOME_CANVAS_ITEM_CLASS(item_edit_parent_class)->update)(item, affine, clip_path, flags);

	/* do not calculate spans until after row/col has been set */
	if (item_edit->font != NULL) {
		/* Redraw before and after in case the span changes */
		gnome_canvas_request_redraw (GNOME_CANVAS_ITEM (item_edit)->canvas,
					     item->x1, item->y1, item->x2, item->y2);
		recalc_spans (item);
		/* Redraw before and after in case the span changes */
		gnome_canvas_request_redraw (GNOME_CANVAS_ITEM (item_edit)->canvas,
					     item->x1, item->y1, item->x2, item->y2);
	}
}

static int
cb_item_edit_cursor_blink (ItemEdit *item_edit)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);

	item_edit->cursor_visible = !item_edit->cursor_visible;

	gnome_canvas_item_request_update (item);
	return TRUE;
}

static void
item_edit_cursor_blink_stop (ItemEdit *item_edit)
{
	if (item_edit->blink_timer == -1)
		return;

	gtk_timeout_remove (item_edit->blink_timer);
	item_edit->blink_timer = -1;
}

static void
item_edit_cursor_blink_start (ItemEdit *item_edit)
{
	item_edit->blink_timer = gtk_timeout_add (
		500, (GtkFunction)(cb_item_edit_cursor_blink),
		item_edit);
}

/*
 * Instance initialization
 */
static void
item_edit_init (ItemEdit *item_edit)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 1;
	item->y2 = 1;

	item_edit->col_span = 1;
	item_edit->lines = 1;
	item_edit->scg = NULL;
	item_edit->pos.col = -1;
	item_edit->pos.row = -1;
	item_edit->font = NULL;
	item_edit->font_height = 1;
	item_edit->cursor_visible = TRUE;
	item_edit->feedback_disabled = FALSE;
	item_edit->feedback_cursor = NULL;
}

/*
 * Invoked when the GtkEntry has changed
 *
 * We use this to sync up the GtkEntry with our display on the screen.
 */
static void
entry_changed (GtkEntry *entry, void *data)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (data);
	ItemEdit *item_edit = ITEM_EDIT (item);
	const char *text;

	text = gtk_entry_get_text (item_edit->entry);

	if (gnumeric_char_start_expr_p (text))
		scan_for_range (item_edit, text);

	gnome_canvas_item_request_update (item);
}

static void
item_edit_destroy (GtkObject *o)
{
	ItemEdit *item_edit = ITEM_EDIT (o);
	GtkEntry *entry = item_edit->entry;

	if (item_edit->text_offsets != NULL)
		g_slist_free (item_edit->text_offsets);

	item_edit_cursor_blink_stop (item_edit);
	entry_destroy_feedback_range (item_edit);

	gtk_signal_disconnect (GTK_OBJECT (entry), item_edit->signal_changed);
	gtk_signal_disconnect (GTK_OBJECT (entry), item_edit->signal_key_press);
	gtk_signal_disconnect (GTK_OBJECT (entry), item_edit->signal_button_press);

	if (GTK_OBJECT_CLASS (item_edit_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_edit_parent_class)->destroy)(o);
}

static int
entry_event (GtkEntry *entry, GdkEvent *event, GnomeCanvasItem *item)
{
	entry_changed (entry, item);
	return TRUE;
}

static void
item_edit_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item      = GNOME_CANVAS_ITEM (o);
	ItemEdit        *item_edit = ITEM_EDIT (o);
	GnumericSheet   *gsheet    = GNUMERIC_SHEET (item->canvas);
	Sheet		*sheet;
	GtkEntry        *entry;

	/* We can only set the item_grid once */
	g_return_if_fail (arg_id == ARG_ITEM_GRID);
	g_return_if_fail (item_edit->item_grid == NULL);

	item_edit->item_grid = GTK_VALUE_POINTER (*arg);
	item_edit->scg = item_edit->item_grid->scg;
	sheet = item_edit->scg->sheet;
	item_edit->entry = GTK_ENTRY (workbook_get_entry (item_edit->scg->wbcg));
	item_edit->pos = sheet->edit_pos;

	entry = item_edit->entry;
	item_edit->signal_changed = gtk_signal_connect (
		GTK_OBJECT (entry), "changed",
		GTK_SIGNAL_FUNC (entry_changed), item_edit);
	item_edit->signal_key_press = gtk_signal_connect_after (
		GTK_OBJECT (entry), "key-press-event",
		GTK_SIGNAL_FUNC (entry_event), item_edit);
	item_edit->signal_button_press = gtk_signal_connect_after (
		GTK_OBJECT (entry), "button-press-event",
		GTK_SIGNAL_FUNC (entry_event), item_edit);

	scan_for_range (item_edit, "");

	/*
	 * Init the auto-completion
	 */

	/* set the font and the upper left corner if this is the first pass */
	if (item_edit->font == NULL) {
		MStyle *mstyle = sheet_style_compute (sheet,
			item_edit->pos.col, item_edit->pos.row);
		StyleFont *sf = sheet_view_get_style_font (sheet, mstyle);

		item_edit->font = style_font_gdk_font (sf);
		item_edit->font_height = style_font_get_height (sf);
		style_font_unref (sf);
		mstyle_unref (mstyle);

		/* move inwards 1 pixel for the grid line */
		item->x1 = 1 + gsheet->col_offset.first +
			scg_colrow_distance_get (item_edit->scg, TRUE,
					  gsheet->col.first, item_edit->pos.col);
		item->y1 = 1 + gsheet->row_offset.first +
			scg_colrow_distance_get (item_edit->scg, FALSE,
					  gsheet->row.first, item_edit->pos.row);

		item->x2 = item->x1 + 1;
		item->y2 = item->y2 + 1;
	}

	item_edit_cursor_blink_start (item_edit);

	gnome_canvas_item_request_update (item);
}

/*
 * ItemEdit class initialization
 */
static void
item_edit_class_init (ItemEditClass *item_edit_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_edit_parent_class = gtk_type_class (gnome_canvas_item_get_type ());

	object_class = (GtkObjectClass *) item_edit_class;
	item_class = (GnomeCanvasItemClass *) item_edit_class;

	gtk_object_add_arg_type ("ItemEdit::Grid", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_ITEM_GRID);

	object_class->set_arg = item_edit_set_arg;
	object_class->destroy = item_edit_destroy;

	/* GnomeCanvasItem method overrides */
	item_class->update      = item_edit_update;
	item_class->draw        = item_edit_draw;
	item_class->point       = item_edit_point;
	item_class->translate   = item_edit_translate; /* deprecated in canvas */
	item_class->event       = item_edit_event;
}

GtkType
item_edit_get_type (void)
{
	static GtkType item_edit_type = 0;

	if (!item_edit_type) {
		GtkTypeInfo item_edit_info = {
			"ItemEdit",
			sizeof (ItemEdit),
			sizeof (ItemEditClass),
			(GtkClassInitFunc) item_edit_class_init,
			(GtkObjectInitFunc) item_edit_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		item_edit_type = gtk_type_unique (gnome_canvas_item_get_type (), &item_edit_info);
	}

	return item_edit_type;
}

void
item_edit_disable_highlight (ItemEdit *item_edit)
{
	g_return_if_fail (item_edit != NULL);
	g_return_if_fail (IS_ITEM_EDIT (item_edit));

	entry_destroy_feedback_range (item_edit);
	item_edit->feedback_disabled = TRUE;
}

void
item_edit_enable_highlight (ItemEdit *item_edit)
{
	g_return_if_fail (item_edit != NULL);
	g_return_if_fail (IS_ITEM_EDIT (item_edit));

	item_edit->feedback_disabled = FALSE;
}
