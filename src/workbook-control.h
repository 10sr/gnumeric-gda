#ifndef GNUMERIC_WORKBOOK_CONTROL_H
#define GNUMERIC_WORKBOOK_CONTROL_H

#include "gnumeric.h"
#include <gtk/gtkobject.h>

#define WORKBOOK_CONTROL_TYPE     (workbook_control_get_type ())
#define WORKBOOK_CONTROL(obj)     (GTK_CHECK_CAST ((obj), WORKBOOK_CONTROL_TYPE, WorkbookControl))
#define IS_WORKBOOK_CONTROL(o)	  (GTK_CHECK_TYPE ((o), WORKBOOK_CONTROL_TYPE))

GtkType workbook_control_get_type    (void);
void 	workbook_control_set_view    (WorkbookControl *wbc,
				      WorkbookView *optional_view,
				      Workbook *optional_wb);
void    workbook_control_sheets_init (WorkbookControl *wbc);

/* Create a new control of the same form */
WorkbookControl *wb_control_wrapper_new (WorkbookControl *wbc,
					 WorkbookView *wbv, Workbook *wb);

void wb_control_title_set	     (WorkbookControl *wbc, char const *title);
void wb_control_prefs_update	     (WorkbookControl *wbc);
void wb_control_format_feedback	     (WorkbookControl *wbc);
void wb_control_zoom_feedback	     (WorkbookControl *wbc);
void wb_control_edit_line_set        (WorkbookControl *wbc, char const *text);
void wb_control_selection_descr_set  (WorkbookControl *wbc, char const *text);
void wb_control_auto_expr_value	     (WorkbookControl *wbc);

void wb_control_sheet_add	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_remove	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_rename	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_focus	     (WorkbookControl *wbc, Sheet *sheet);
void wb_control_sheet_move	     (WorkbookControl *wbc, Sheet *sheet,
				      int new_pos);
void wb_control_sheet_remove_all     (WorkbookControl *wbc);

void wb_control_undo_redo_clear	     (WorkbookControl *wbc, gboolean is_undo);
void wb_control_undo_redo_pop	     (WorkbookControl *wbc, gboolean is_undo);
void wb_control_undo_redo_push	     (WorkbookControl *wbc,
				      char const *text, gboolean is_undo);
void wb_control_undo_redo_labels     (WorkbookControl *wbc,
				      char const *undo, char const *redo);

void wb_control_paste_special_enable (WorkbookControl *wbc, gboolean enable);
void wb_control_paste_from_selection (WorkbookControl *wbc,
				      PasteTarget const *pt, guint32 time);
gboolean wb_control_claim_selection  (WorkbookControl *wbc);

WorkbookView *wb_control_view		(WorkbookControl *wbc);
Workbook     *wb_control_workbook	(WorkbookControl *wbc);
Sheet        *wb_control_cur_sheet	(WorkbookControl *wbc);

gboolean      workbook_parse_and_jump   (WorkbookControl *wbc, const char *text);

#endif /* GNUMERIC_WORKBOOK_CONTROL_H */
