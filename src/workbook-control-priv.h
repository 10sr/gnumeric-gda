#ifndef GNUMERIC_WORKBOOK_CONTROL_PRIV_H
#define GNUMERIC_WORKBOOK_CONTROL_PRIV_H

#include "command-context-priv.h"
#include "workbook-control.h"

struct _WorkbookControl {
	CommandContext	context;

	WorkbookView *wb_view;
};
typedef struct {
	CommandContextClass   context_class;

	/* Create a new control of the same form */
	WorkbookControl *(*control_new) (WorkbookControl *wbc, WorkbookView *wbv, Workbook *wb);

	/* Actions on the workbook UI */
	void (*title_set)	    (WorkbookControl *wbc, char const *title);
	void (*prefs_update)	    (WorkbookControl *wbc);
	void (*format_feedback)	    (WorkbookControl *wbc);
	void (*zoom_feedback)	    (WorkbookControl *wbc);
	void (*edit_line_set)	    (WorkbookControl *wbc, char const *text);
	void (*selection_descr_set) (WorkbookControl *wbc, char const *text);
	void (*auto_expr_value)	    (WorkbookControl *wbc);
	struct {
		void (*add)	(WorkbookControl *wbc, Sheet *sheet);
		void (*remove)	(WorkbookControl *wbc, Sheet *sheet);
		void (*rename)  (WorkbookControl *wbc, Sheet *sheet);
		void (*focus)   (WorkbookControl *wbc, Sheet *sheet);
		void (*move)    (WorkbookControl *wbc, Sheet *sheet,
				 int new_pos);
		void (*remove_all) (WorkbookControl *wbc);
	} sheet;
	struct {
		void (*clear)	(WorkbookControl *wbc, gboolean is_undo);
		void (*truncate)(WorkbookControl *wbc, int n, gboolean is_undo);
		void (*pop)	(WorkbookControl *wbc, gboolean is_undo);
		void (*push)	(WorkbookControl *wbc,
				 char const *text, gboolean is_undo);
		void (*labels)	(WorkbookControl *wbc,
				 char const *undo, char const *redo);
	} undo_redo;
	struct {
		void (*special_enable) (WorkbookControl *wbc, Sheet *sheet);
		void (*from_selection) (WorkbookControl *wbc,
					PasteTarget const *pt, guint32 time);
	} paste;
	void     (*insert_cols_rows_enable) (WorkbookControl *wbc, Sheet *sheet);
	void     (*insert_cells_enable)     (WorkbookControl *wbc, Sheet *sheet);
	gboolean (*claim_selection)         (WorkbookControl *wbc);
} WorkbookControlClass;

#define WORKBOOK_CONTROL_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), WORKBOOK_CONTROL_TYPE, WorkbookControlClass))

#endif /* GNUMERIC_WORKBOOK_CONTROL_PRIV_H */
