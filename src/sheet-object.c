/*
 * sheet-object.c: Implements the sheet object manipulation for Gnumeric
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Michael Meeks   (mmeeks@gnu.org)
 *   Jody Goldberg   (jgoldberg@home.com)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "sheet-control-gui.h"
#include "gnumeric-type-util.h"
#include "dialogs.h"
#include "sheet-object-impl.h"
#include "workbook-edit.h"
#include "sheet.h"
#include "sheet-private.h"
#include "expr.h"
#include "ranges.h"
#include "xml-io.h"

#include "sheet-object-graphic.h"
#include "sheet-object-cell-comment.h"
#ifdef ENABLE_BONOBO
#include "sheet-object-bonobo.h"
#endif

/* Returns the class for a SheetObject */
#define SO_CLASS(so) SHEET_OBJECT_CLASS(GTK_OBJECT(so)->klass)

#define	SO_VIEW_SHEET_CONTROL_KEY	"SheetControl"
#define	SO_VIEW_OBJECT_KEY		"SheetObject"

GtkType    sheet_object_get_type  (void);
static GtkObjectClass *sheet_object_parent_class;

static void
sheet_object_remove_cb (GtkWidget *widget, SheetObject *so)
{
	gtk_object_destroy (GTK_OBJECT (so));
}

static void
cb_sheet_object_configure (GtkWidget *widget, GnomeCanvasItem *obj_view)
{
	SheetControlGUI *scg;
	SheetObject *so;

	g_return_if_fail (obj_view != NULL);

	so = gtk_object_get_data (GTK_OBJECT (obj_view),
				  SO_VIEW_OBJECT_KEY);
	scg = gtk_object_get_data (GTK_OBJECT (obj_view),
				   SO_VIEW_SHEET_CONTROL_KEY);

	SO_CLASS(so)->user_config (so, scg);
}

/**
 * sheet_object_populate_menu:
 * @so:  the sheet object
 * @menu: the menu to insert into
 *
 * Add standard items to the object's popup menu.
 */
static void
sheet_object_populate_menu (SheetObject *so,
			    GnomeCanvasItem *obj_view,
			    GtkMenu *menu)
{
	GtkWidget *item = gnome_stock_menu_item (GNOME_STOCK_MENU_CLOSE,
						 _("Delete"));

	gtk_menu_append (menu, item);
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    GTK_SIGNAL_FUNC (sheet_object_remove_cb), so);

	if (SO_CLASS(so)->user_config != NULL) {
		item = gnome_stock_menu_item (GNOME_STOCK_MENU_PROP,
					      _("Configure"));
		gtk_signal_connect (GTK_OBJECT (item), "activate",
				    GTK_SIGNAL_FUNC (cb_sheet_object_configure), obj_view);
		gtk_menu_append (menu, item);
	}
}

/**
 * sheet_object_unrealize:
 *
 * Clears all views of this object in its current sheet's controls.
 */
static void
sheet_object_unrealize (SheetObject *so)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));

	/* The views remove themselves from the list */
	while (so->realized_list != NULL)
		gtk_object_destroy (GTK_OBJECT (so->realized_list->data));
}

/**
 * sheet_objects_max_extent :
 * @sheet :
 *
 * Utility routine to calculate the maximum extent of objects in this sheet.
 */
static void
sheet_objects_max_extent (Sheet *sheet)
{
	CellPos max_pos = { 0, 0 };
	GList *ptr;

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
		SheetObject *so = SHEET_OBJECT (ptr->data);

		if (max_pos.col < so->cell_bound.end.col)
			max_pos.col = so->cell_bound.end.col;
		if (max_pos.row < so->cell_bound.end.row)
			max_pos.row = so->cell_bound.end.row;
	}

	if (sheet->max_object_extent.col != max_pos.col ||
	    sheet->max_object_extent.row != max_pos.row) {
		sheet->max_object_extent = max_pos;
		sheet_scrollbar_config (sheet);
	}
}

static void
sheet_object_destroy (GtkObject *object)
{
	SheetObject *so = SHEET_OBJECT (object);

	g_return_if_fail (so != NULL);

	sheet_object_unrealize (so);

	if (so->sheet != NULL) {
		g_return_if_fail (IS_SHEET (so->sheet));

		/* If the object has already been inserted then mark sheet as dirty */
		if (NULL != g_list_find	(so->sheet->sheet_objects, so)) {
			so->sheet->sheet_objects =
				g_list_remove (so->sheet->sheet_objects, so);
			so->sheet->modified = TRUE;
		}

		if (so->cell_bound.end.col == so->sheet->max_object_extent.col &&
		    so->cell_bound.end.row == so->sheet->max_object_extent.row)
			sheet_objects_max_extent (so->sheet);
		so->sheet = NULL;
	}
	(*sheet_object_parent_class->destroy)(object);
}

static void
sheet_object_init (GtkObject *object)
{
	int i;
	SheetObject *so = SHEET_OBJECT (object);

	so->type = SHEET_OBJECT_ACTION_STATIC;
	so->sheet = NULL;

	/* Store the logical position as A1 */
	so->cell_bound.start.col = so->cell_bound.start.row = 0;
	so->cell_bound.end.col = so->cell_bound.end.row = 1;

	for (i = 4; i-- > 0 ;) {
		so->offset [i] = 0.;
		so->anchor_type [i] = SO_ANCHOR_UNKNOWN;
	}
}

static void
sheet_object_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_parent_class = gtk_type_class (gtk_object_get_type ());

	object_class->destroy = sheet_object_destroy;
	sheet_object_class->update_bounds = NULL;
	sheet_object_class->populate_menu = sheet_object_populate_menu;
	sheet_object_class->print         = NULL;
	sheet_object_class->user_config   = NULL;

	/* Provide some defaults (derived classes may want to override) */
	sheet_object_class->default_width_pts = 72.;	/* 1 inch */
	sheet_object_class->default_height_pts = 36.;	/* 1/2 inch */
}

GNUMERIC_MAKE_TYPE (sheet_object, "SheetObject", SheetObject,
		    sheet_object_class_init, sheet_object_init,
		    gtk_object_get_type ())

SheetObject *
sheet_object_view_obj (GtkObject *view)
{
	GtkObject *obj = gtk_object_get_data (view, SO_VIEW_OBJECT_KEY);
	return SHEET_OBJECT (obj);
}

SheetControlGUI *
sheet_object_view_control (GtkObject *view)
{
	GtkObject *obj = gtk_object_get_data (view, SO_VIEW_SHEET_CONTROL_KEY);
	return SHEET_CONTROL_GUI (obj);
}

GtkObject *
sheet_object_get_view (SheetObject *so, SheetControlGUI *scg)
{
	GList *l;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	for (l = so->realized_list; l; l = l->next) {
		GtkObject *obj = GTK_OBJECT (l->data);
		if (scg == sheet_object_view_control (obj))
			return obj;
	}

	return NULL;
}

void
sheet_object_position (SheetObject *so, CellPos const *pos)
{
	GList *l;

	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (pos != NULL &&
	    so->cell_bound.end.col < pos->col &&
	    so->cell_bound.end.row < pos->row)
		return;

	for (l = so->realized_list; l; l = l->next) {
		GtkObject *view = GTK_OBJECT (l->data);
		SO_CLASS (so)->update_bounds (so, view,
			sheet_object_view_control (view));
	}
}

/**
 * sheet_object_set_sheet :
 * @so :
 * @sheet :
 */
gboolean
sheet_object_set_sheet (SheetObject *so, Sheet *sheet)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (so->sheet == NULL, TRUE);
	g_return_val_if_fail (g_list_find (sheet->sheet_objects, so) == NULL, TRUE);

	so->sheet = sheet;
	if (SO_CLASS (so)->assign_to_sheet &&
	    SO_CLASS (so)->assign_to_sheet (so, sheet)) {
		so->sheet = NULL;
		return TRUE;
	}

	sheet->sheet_objects = g_list_prepend (sheet->sheet_objects, so);
	sheet_object_realize (so);
	sheet_object_position (so, NULL);

	return FALSE;
}

/**
 * sheet_object_clear_sheet :
 * @so :
 */
gboolean
sheet_object_clear_sheet (SheetObject *so)
{
	GList *ptr;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), TRUE);
	g_return_val_if_fail (IS_SHEET (so->sheet), TRUE);

	ptr = g_list_find (so->sheet->sheet_objects, so);
	g_return_val_if_fail (ptr != NULL, TRUE);

	if (SO_CLASS (so)->remove_from_sheet &&
	    SO_CLASS (so)->remove_from_sheet (so)) {
		so->sheet = NULL;
		return TRUE;
	}
	sheet_object_unrealize (so);
	so->sheet->sheet_objects = g_list_remove_link (so->sheet->sheet_objects, ptr);
	so->sheet = NULL;

	return FALSE;
}

static void
sheet_object_view_destroyed (GtkObject *view, SheetObject *so)
{
	so->realized_list = g_list_remove (so->realized_list, view);
}

/*
 * sheet_object_new_view
 *
 * Creates a GnomeCanvasItem for a SheetControlGUI and sets up the event
 * handlers.
 */
void
sheet_object_new_view (SheetObject *so, SheetControlGUI *scg)
{
	GtkObject *view;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	g_return_if_fail (IS_SHEET_OBJECT (so));

	view = SO_CLASS (so)->new_view (so, scg);

	g_return_if_fail (GTK_IS_OBJECT (view));

	/* Store some useful information */
	gtk_object_set_data (GTK_OBJECT (view), SO_VIEW_OBJECT_KEY, so);
	gtk_object_set_data (GTK_OBJECT (view), SO_VIEW_SHEET_CONTROL_KEY, scg);

	gtk_signal_connect (GTK_OBJECT (view), "destroy",
			    GTK_SIGNAL_FUNC (sheet_object_view_destroyed), so);
	so->realized_list = g_list_prepend (so->realized_list, view);

	SO_CLASS (so)->update_bounds (so, view, scg);
}

/**
 * sheet_object_realize:
 *
 * Creates a view of an object for every control associated with the object's
 * sheet.
 */
void
sheet_object_realize (SheetObject *so)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (IS_SHEET (so->sheet));

	SHEET_FOREACH_CONTROL (so->sheet, control,
		sheet_object_new_view (so, control););
}

void
sheet_object_print (SheetObject const *so, SheetObjectPrintInfo const *pi)
{
	if (SO_CLASS (so)->print)
		SO_CLASS (so)->print (so, pi);
	else
		g_warning ("Un-printable sheet object");
}

SheetObject *
sheet_object_read_xml (XmlParseContext const *ctxt, xmlNodePtr tree)
{
	SheetObject *so;
	char *tmp;

	/* Old crufty IO */
	if (!strcmp (tree->name, "Rectangle")){
		so = sheet_object_box_new (FALSE);
	} else if (!strcmp (tree->name, "Ellipse")){
		so = sheet_object_box_new (TRUE);
	} else if (!strcmp (tree->name, "Arrow")){
		so = sheet_object_line_new (TRUE);
	} else if (!strcmp (tree->name, "Line")){
		so = sheet_object_line_new (FALSE);
	} else {
		GtkObject *obj;
		obj = gtk_object_new (gtk_type_from_name (tree->name), NULL);
		so = SHEET_OBJECT (obj);
	}

	if (so == NULL)
		return NULL;

	if (SO_CLASS (so)->read_xml &&
	    SO_CLASS (so)->read_xml (so, ctxt, tree)) {
		gtk_object_destroy (GTK_OBJECT (so));
		return NULL;
	}
		
	tmp = xmlGetProp (tree, "ObjectBound");
	if (tmp != NULL) {
		Range r;
		if (parse_range (tmp,
				 &r.start.col, &r.start.row,
				 &r.end.col, &r.end.row))
			so->cell_bound = r;
		xmlFree (tmp);
	}

	tmp = xmlGetProp (tree, "ObjectOffset");
	if (tmp != NULL) {
		sscanf (tmp, "%g %g %g %g",
			so->offset +0, so->offset +1,
			so->offset +2, so->offset +3);
	}

	tmp = xmlGetProp (tree, "ObjectAnchorType");
	if (tmp != NULL) {
		int i[4], count;
		sscanf (tmp, "%d %d %d %d", i+0, i+1, i+2, i+3);

		for (count = 4; count-- > 0 ; )
			so->anchor_type[count] = i[count];
	}

	sheet_object_set_sheet (so, ctxt->sheet);
	return so;
}

xmlNodePtr
sheet_object_write_xml (SheetObject const *so, XmlParseContext const *ctxt)
{
	GtkObject *obj;
	xmlNodePtr tree;
	char buffer[4*(DBL_DIG+10)];

	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);
	obj = GTK_OBJECT (so);

	if (SO_CLASS (so)->write_xml == NULL)
		return NULL;

	tree = xmlNewDocNode (ctxt->doc, ctxt->ns,
			      gtk_type_name (GTK_OBJECT_TYPE (obj)), NULL);

	if (tree == NULL)
		return NULL;

	if (SO_CLASS (so)->write_xml (so, ctxt, tree)) {
		xmlUnlinkNode (tree);
		xmlFreeNode (tree);
		return NULL;
	}

	xml_set_value_cstr (tree, "ObjectBound", range_name (&so->cell_bound));
	snprintf (buffer, sizeof (buffer), "%.*g %.*g %.*g %.*g",
		  DBL_DIG, so->offset [0], DBL_DIG, so->offset [1],
		  DBL_DIG, so->offset [2], DBL_DIG, so->offset [3]);
	xml_set_value_cstr (tree, "ObjectOffset", buffer);
	snprintf (buffer, sizeof (buffer), "%d %d %d %d",
		  so->anchor_type [0], so->anchor_type [1],
		  so->anchor_type [2], so->anchor_type [3]);
	xml_set_value_cstr (tree, "ObjectAnchorType", buffer);

	return tree;
}

Range const *
sheet_object_range_get (SheetObject const *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), NULL);

	return &so->cell_bound;
}

void
sheet_object_range_set (SheetObject *so, Range const *r,
			float const *offsets, SheetObjectAnchor const *types)
{
	int i;

	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (r != NULL) {
		so->cell_bound = *r;
		if (so->sheet)
			sheet_objects_max_extent (so->sheet);

	}

	if (offsets != NULL)
		for (i = 4; i-- > 0 ; )
			so->offset [i] = offsets [i];
	if (types != NULL)
		for (i = 4; i-- > 0 ; )
			so->anchor_type [i] = types [i];

	if (so->sheet != NULL)
		sheet_object_position (so, NULL);
}

static int
cell_offset_calc_pixel (Sheet const *sheet, int i, gboolean is_col,
			SheetObjectAnchor anchor_type, float offset)
{
	ColRowInfo const *cri = is_col
		? sheet_col_get_info (sheet, i)
		: sheet_row_get_info (sheet, i);
	/* TODO : handle other anchor types */
	if (anchor_type == SO_ANCHOR_PERCENTAGE_FROM_COLROW_END)
		return (1. - offset) * cri->size_pixels;
	return offset * cri->size_pixels;
}

/**
 * sheet_object_position_pixels :
 *
 * @so : The sheet object
 * @coords : array of 4 ints
 *
 * Calculate the position of the object @so in pixels from the logical position
 * in the object.
 */
void
sheet_object_position_pixels (SheetObject const *so,
			      SheetControlGUI const *scg, int *coords)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (so->sheet != NULL);

	coords [0] = scg_colrow_distance_get (scg, TRUE, 0,
		so->cell_bound.start.col);
	coords [1] = scg_colrow_distance_get (scg, FALSE, 0,
		so->cell_bound.start.row);

	coords [2] = coords [0] + scg_colrow_distance_get (scg, TRUE,
		so->cell_bound.start.col, so->cell_bound.end.col);
	coords [3] = coords [1] + scg_colrow_distance_get (scg, FALSE,
		so->cell_bound.start.row, so->cell_bound.end.row);

	coords [0] += cell_offset_calc_pixel (so->sheet, so->cell_bound.start.col,
		TRUE, so->anchor_type [0], so->offset [0]);
	coords [1] += cell_offset_calc_pixel (so->sheet, so->cell_bound.start.row,
		FALSE, so->anchor_type [1], so->offset [1]);
	coords [2] += cell_offset_calc_pixel (so->sheet, so->cell_bound.end.col,
		TRUE, so->anchor_type [2], so->offset [2]);
	coords [3] += cell_offset_calc_pixel (so->sheet, so->cell_bound.end.row,
		FALSE, so->anchor_type [3], so->offset [3]);
}

static double
cell_offset_calc_pt (Sheet const *sheet, int i, gboolean is_col,
		     SheetObjectAnchor anchor_type, float offset)
{
	ColRowInfo const *cri = is_col
		? sheet_col_get_info (sheet, i)
		: sheet_row_get_info (sheet, i);
	/* TODO : handle other anchor types */
	if (anchor_type == SO_ANCHOR_PERCENTAGE_FROM_COLROW_END)
		return (1. - offset) * cri->size_pixels;
	return offset * cri->size_pixels;
}

/**
 * sheet_object_default_size 
 * @so : The sheet object
 * @w : a ptr into which to store the default_width.
 * @h : a ptr into which to store the default_height.
 *
 * Measurements are in pts.
 */
void
sheet_object_default_size (SheetObject *so, double *w, double *h)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (w != NULL);
	g_return_if_fail (h != NULL);

	*w = SO_CLASS(so)->default_width_pts;
	*h = SO_CLASS(so)->default_height_pts;
}

/**
 * sheet_object_position_pts :
 *
 * @so : The sheet object
 * @coords : array of 4 doubles
 *
 * Calculate the position of the object @so in pts from the logical position in
 * the object.
 */
void
sheet_object_position_pts (SheetObject const *so, double *coords)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));

	coords [0] = sheet_col_get_distance_pts (so->sheet, 0,
		so->cell_bound.start.col);
	coords [2] = coords [0] + sheet_col_get_distance_pts (so->sheet,
		so->cell_bound.start.col, so->cell_bound.end.col);
	coords [1] = sheet_row_get_distance_pts (so->sheet, 0,
		so->cell_bound.start.row);
	coords [3] = coords [1] + sheet_row_get_distance_pts (so->sheet,
		so->cell_bound.start.row, so->cell_bound.end.row);

	coords [0] += cell_offset_calc_pt (so->sheet, so->cell_bound.start.col,
		TRUE, so->anchor_type [0], so->offset [0]);
	coords [1] += cell_offset_calc_pt (so->sheet, so->cell_bound.start.row,
		FALSE, so->anchor_type [1], so->offset [1]);
	coords [2] += cell_offset_calc_pt (so->sheet, so->cell_bound.end.col,
		TRUE, so->anchor_type [2], so->offset [2]);
	coords [3] += cell_offset_calc_pt (so->sheet, so->cell_bound.end.row,
		FALSE, so->anchor_type [3], so->offset [3]);
}

/**
 * sheet_relocate_objects :
 *
 * @sheet : the sheet.
 * @rinfo : details on what should be moved.
 */
void
sheet_relocate_objects (ExprRelocateInfo const *rinfo)
{
	GList   *ptr, *next;
	Range	 dest;
	gboolean clear, change_sheets;

	g_return_if_fail (rinfo != NULL);
	g_return_if_fail (IS_SHEET (rinfo->origin_sheet));
	g_return_if_fail (IS_SHEET (rinfo->target_sheet));
	    
	dest = rinfo->origin;
	clear = range_translate (&dest, rinfo->col_offset, rinfo->row_offset);
	change_sheets = (rinfo->origin_sheet != rinfo->target_sheet);

	/* Clear the destination range on the target sheet */
	if (change_sheets) {
		GList *copy = g_list_copy (rinfo->target_sheet->sheet_objects);
		for (ptr = copy; ptr != NULL ; ptr = ptr->next ) {
			SheetObject *so = SHEET_OBJECT (ptr->data);
			if (range_contains (&dest,
					    so->cell_bound.start.col,
					    so->cell_bound.start.row))
				gtk_object_destroy (GTK_OBJECT (so));
		}
		g_list_free (copy);
	}

	ptr = rinfo->origin_sheet->sheet_objects;
	for (; ptr != NULL ; ptr = next ) {
		SheetObject *so = SHEET_OBJECT (ptr->data);
		next = ptr->next;
		if (range_contains (&rinfo->origin,
				    so->cell_bound.start.col,
				    so->cell_bound.start.row)) {
			/* FIXME : just moving the range is insufficent for all anchor types */
			/* Toss any objects that would be clipped. */
			if (range_translate (&so->cell_bound, rinfo->col_offset, rinfo->row_offset)) {
				gtk_object_destroy (GTK_OBJECT (so));
				continue;
			}
			if (change_sheets) {
				sheet_object_clear_sheet (so);
				sheet_object_set_sheet (so, rinfo->target_sheet);
			} else {
				sheet_object_position (so, NULL);
			}
		} else if (!change_sheets &&
			   range_contains (&dest,
					   so->cell_bound.start.col,
					   so->cell_bound.start.row)) {
			gtk_object_destroy (GTK_OBJECT (so));
			continue;
		}
	}

	sheet_objects_max_extent (rinfo->origin_sheet);
	if (change_sheets)
		sheet_objects_max_extent (rinfo->target_sheet);
}

/**
 * sheet_get_objects :
 *
 * @sheet : the sheet.
 * @r     : an optional range to look in
 * @t     : The type of object to lookup
 *
 * Returns a list of which the caller must free (just the list not the content).
 * Containing all objects of exactly the specified type (inheritence does not count).
 */
GList *
sheet_get_objects (Sheet const *sheet, Range const *r, GtkType t)
{
	GList *res = NULL;
	GList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
		GtkObject *obj = GTK_OBJECT (ptr->data);

		if (GTK_OBJECT_TYPE (obj) == t) {
			SheetObject *so = SHEET_OBJECT (obj);
			if (r == NULL || range_overlap (r, &so->cell_bound))
				res = g_list_prepend (res, so);
		}
	}
	return res;
}

void
sheet_object_register (void)
{
	SHEET_OBJECT_GRAPHIC_TYPE;
	SHEET_OBJECT_FILLED_TYPE;
	CELL_COMMENT_TYPE;
#ifdef ENABLE_BONOBO
	SHEET_OBJECT_BONOBO_TYPE;
#endif
}
