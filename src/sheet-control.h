#ifndef GNUMERIC_SHEET_CONTROL_H
#define GNUMERIC_SHEET_CONTROL_H

#include "gnumeric.h"
#include <glib-object.h>

#define SHEET_CONTROL_TYPE	(sheet_control_get_type ())
#define SHEET_CONTROL(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), SHEET_CONTROL_TYPE, SheetControl))
#define IS_SHEET_CONTROL(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), SHEET_CONTROL_TYPE))

/* Lifecycle */
GType sheet_control_get_type	(void);
SheetView	*sc_view	(SheetControl const *sc);
Sheet		*sc_sheet	(SheetControl const *sc);
WorkbookControl *sc_wbc		(SheetControl const *sc);

/**
 * NOTE:
 * The GUI layer accesses the SheetControlGUI methods directly without
 * calling the virtual. Change this if the base class becomes something
 * more than a passthrough.
 */
void sc_resize			(SheetControl *sc, gboolean force_scroll);
void sc_redraw_all		(SheetControl *sc, gboolean headers);
void sc_redraw_range		(SheetControl *sc, GnmRange const *r);
void sc_redraw_headers		(SheetControl *sc,
				 gboolean const col, gboolean const row,
				 GnmRange const * r /* optional == NULL */);
void sc_ant			(SheetControl *sc);
void sc_unant			(SheetControl *sc);

void sc_adjust_preferences	(SheetControl *sc);
void sc_scrollbar_config	(SheetControl const *sc);

void sc_mode_edit		(SheetControl *sc);

void sc_set_top_left       	(SheetControl *sc, int col, int row);
void sc_compute_visible_region	(SheetControl *sc, gboolean full_recompute);
void sc_make_cell_visible      	(SheetControl *sc, int col, int row,
				 gboolean couple_panes);

void sc_cursor_bound		(SheetControl *sc, GnmRange const *r);
void sc_set_panes		(SheetControl *sc);
void sc_object_create_view	(SheetControl *sc, SheetObject *so);
void sc_direction_changed	(SheetControl *sc);
void sc_scale_changed		(SheetControl *sc);

#endif /* GNUMERIC_SHEET_CONTROL_H */
