#ifndef GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H
#define GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H

#include "gnumeric.h"
#include "workbook-control-gui.h"
#include "workbook-control-priv.h"
#include "file.h"
#include "style.h"
#include "widgets/gnumeric-expr-entry.h"

#include <gtk/gtknotebook.h>

struct _WorkbookControlGUI {
	WorkbookControl	wb_control;

	GtkWindow   *toplevel;
	GtkNotebook *notebook;
	GtkWidget   *progress_bar;

	struct {
		GnmExprEntry *entry; /* The real edit line */
		GnmExprEntry *temp_entry; /* A tmp overlay eg from a guru */
		GtkWidget*guru;
		gulong         signal_changed, signal_insert, signal_delete;
		gulong         signal_cursor_pos, signal_selection_bound;
		PangoAttrList *full_content;	/* include the cell attrs too */
		PangoAttrList *markup;	/* just the markup */
		PangoAttrList *cur_fmt;	/* attrs for new text (depends on position) */
	} edit_line;

	/* While editing these should be visible */
	GtkWidget *ok_button, *cancel_button;

	/* While not editing these should be visible */
	GtkWidget *func_button;

	gboolean    updating_ui;

	/* Auto completion */
	void		*auto_complete;         /* GtkType is (Complete *) */
	gboolean	 auto_completing;
	char		*auto_complete_text;

	/* Used to detect if the user has backspaced, so we turn off auto-complete */
	int              auto_max_size;

	/* Keep track of whether the last key pressed was END, so end-mode works */
	gboolean last_key_was_end;

	SheetControlGUI *rangesel;

	GtkWidget  *table;
	GtkWidget  *auto_expr_label;
	GtkWidget  *status_text;
	GtkWidget  *statusbar;

	/* Edit area */
	GtkWidget *selection_descriptor;	/* A GtkEntry */

	/* Autosave */
        gboolean   autosave;
        gboolean   autosave_prompt;
        gint       autosave_minutes;
        gint       autosave_timer;

	PangoFontDescription *font_desc;

	GnmFileSaver *current_saver;
};

typedef struct {
	WorkbookControlClass base;

	/* signals */
	void (*markup_changed)		(WorkbookControlGUI const *wbcg);

	/* virtuals */
	void (*set_transient)		(WorkbookControlGUI *wbcg, GtkWindow *window);
	void (*create_status_area)	(WorkbookControlGUI *wbcg, GtkWidget *progress,
					 GtkWidget *status, GtkWidget *autoexpr);
	void (*actions_sensitive)	(WorkbookControlGUI *wbcg,
					 gboolean actions, gboolean font_actions);
	void (*set_zoom_label)		(WorkbookControlGUI const *wbcg, char const *label);
	void (*reload_recent_file_menu)	(WorkbookControlGUI const *wbcg);
	void (*set_action_sensitivity)  (WorkbookControlGUI const *wbcg,
					 char const *action,
					 gboolean sensitive);
	void (*set_action_label)        (WorkbookControlGUI const *wbcg,
					 char const *action,
					 char const *prefix,
					 char const *suffix,
					 char const *new_tip);
	void (*set_toggle_action_state) (WorkbookControlGUI const *wbcg,
					 char const *action,
					 gboolean state);
} WorkbookControlGUIClass;

#define WORKBOOK_CONTROL_GUI_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), WORKBOOK_CONTROL_GUI_TYPE, WorkbookControlGUIClass))

/* Protected functions */
void	 wbcg_set_toplevel	      (WorkbookControlGUI *wbcg, GtkWidget *w);
gboolean wbcg_scroll_wheel_support_cb (GtkWidget *ignored,
				       GdkEventScroll *event,
				       WorkbookControlGUI *wbcg);
gboolean wbcg_close_control	      (WorkbookControlGUI *wbcg);
int	 wbcg_close_if_user_permits   (WorkbookControlGUI *wbcg,
				       WorkbookView *wb_view, gboolean close_clean,
				       gboolean exiting, gboolean ask_user);
void	 scg_delete_sheet_if_possible (GtkWidget *ignored, SheetControlGUI *scg);
void	 wbcg_insert_sheet	      (GtkWidget *ignored, WorkbookControlGUI *wbcg);
void	 wbcg_append_sheet	      (GtkWidget *ignored, WorkbookControlGUI *wbcg);
void	 wbcg_set_selection_halign    (WorkbookControlGUI *wbcg, StyleHAlignFlags halign);
void	 wbcg_set_selection_valign    (WorkbookControlGUI *wbcg, StyleHAlignFlags halign);

enum {
	WBCG_MARKUP_CHANGED,
	WBCG_LAST_SIGNAL
};

extern guint wbcg_signals [WBCG_LAST_SIGNAL];

#endif /* GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H */

