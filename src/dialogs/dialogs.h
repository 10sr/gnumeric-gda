#ifndef GNUMERIC_DIALOGS_H
#define GNUMERIC_DIALOGS_H

#include "gnumeric.h"
#include "summary.h"
#include "workbook-control-gui.h"

typedef enum {
	FD_CURRENT = -1,
	FD_NUMBER,
	FD_ALIGNMENT,
	FD_FONT,
	FD_BORDER,
	FD_BACKGROUND,
	FD_PROTECTION,
	FD_LAST = FD_PROTECTION
} FormatDialogPosition_t;

#ifdef ENABLE_BONOBO
void     dialog_graph_guru	(WorkbookControlGUI *wbcg);
#endif
void     dialog_formula_guru	(WorkbookControlGUI *wbcg);
void     dialog_plugin_manager  (WorkbookControlGUI *wbcg);
void   	 dialog_goto_cell       (WorkbookControlGUI *wbcg);
void   	 dialog_cell_format     (WorkbookControlGUI *wbcg, Sheet *sheet,
				 FormatDialogPosition_t pageno);
int    	 dialog_paste_special   (WorkbookControlGUI *wbcg);
void   	 dialog_insert_cells    (WorkbookControlGUI *wbcg, Sheet *sheet);
void   	 dialog_delete_cells    (WorkbookControlGUI *wbcg, Sheet *sheet);
void   	 dialog_zoom            (WorkbookControlGUI *wbcg, Sheet *sheet);
char   	*dialog_query_load_file (WorkbookControlGUI *wbcg);
void   	 dialog_about           (WorkbookControlGUI *wbcg);
void   	 dialog_define_names    (WorkbookControlGUI *wbcg);
void   	 dialog_cell_comment    (WorkbookControlGUI *wbcg, Cell *cell);
void   	 dialog_cell_sort       (WorkbookControlGUI *wbcg, Sheet *sheet);
void   	 dialog_workbook_attr   (WorkbookControlGUI *wbcg);
void   	 dialog_goal_seek       (WorkbookControlGUI *wbcg, Sheet *sheet);
void   	 dialog_solver          (WorkbookControlGUI *wbcg, Sheet *sheet);
void   	 dialog_printer_setup   (WorkbookControlGUI *wbcg, Sheet *sheet);
void   	 dialog_summary_update  (WorkbookControlGUI *wbcg, SummaryInfo *sin);
void     dialog_autocorrect     (WorkbookControlGUI *wbcg);
void   	 dialog_advanced_filter (WorkbookControlGUI *wbcg);
void   	 dialog_autosave        (WorkbookControlGUI *wbcg);
gboolean dialog_autosave_prompt (WorkbookControlGUI *wbcg);
void     dialog_autoformat      (WorkbookControlGUI *wbcg);
void     dialog_data_analysis   (WorkbookControlGUI *wbcg, Sheet *sheet);
char   	*dialog_get_sheet_name  (WorkbookControlGUI *wbcg, const char *name);
void     dialog_sheet_order     (WorkbookControlGUI *wbcg);
gboolean dialog_get_number      (WorkbookControlGUI *wbcg,
				 const char *glade_file,
				 double *init_and_return_value);

FunctionDefinition *dialog_function_select (WorkbookControlGUI *wbcg);

#endif /* GNUMERIC_DIALOGS_H */
