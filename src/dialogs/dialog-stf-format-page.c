/*
 * dialog-stf.c : Controls the widgets on the format page of the dialog
 *
 * Almer. S. Tigelaar <almer1@dds.nl>
 *
 */

#include "dialog-stf.h"

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

/**
 * format_page_update_preview
 * @pagedata : mother struct
 *
 * Will simply utilize the preview rendering functions to update
 * the preview
 *
 * returns : nothing
 **/
static void
format_page_update_preview (DruidPageData_t *pagedata)
{
	FormatInfo_t *info = pagedata->format_info;
	GSList *list;
	GSList *iterator;
	
	stf_preview_colformats_clear (info->format_run_renderdata);
	iterator = info->format_run_list;
	while (iterator) {
	
		stf_preview_colformats_add (info->format_run_renderdata, iterator->data);

		iterator = g_slist_next (iterator);
	}
		
	list = stf_parse_general_cached (info->format_run_parseoptions,
					 info->format_run_cacheoptions);
					 
	stf_preview_render (info->format_run_renderdata,
			    list,
			    info->format_run_displayrows,
			    pagedata->colcount);

}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

/**
 * format_page_scroll_value_changed
 * @adjustment : The gtkadjustment that emitted the signal
 * @data : a mother struct
 *
 * This signal responds to changes in the scrollbar and
 * will force a redraw of the preview
 *
 * returns : nothing
 **/
static void
format_page_scroll_value_changed (GtkAdjustment *adjustment, DruidPageData_t *data)
{
	FormatInfo_t *info = data->format_info;

	stf_preview_set_startrow (info->format_run_renderdata, adjustment->value);

	stf_cache_options_set_range (info->format_run_cacheoptions,
				     info->format_run_renderdata->startrow - 1,
				     (info->format_run_renderdata->startrow - 1) + info->format_run_displayrows);

	format_page_update_preview (data);
}

/**
 * format_page_canvas_button_press_event
 * @canvas : gnome canvas that emitted the signal
 * @event : a button click event
 * @data : a mother struct
 *
 * This signal responds to a click on the canvas, in fact it will find out what
 * column the user clicked on and then select that column in the column list 
 *
 * returns : always TRUE
 **/
static gboolean
format_page_canvas_button_press_event (GnomeCanvas *canvas, GdkEventButton *event, DruidPageData_t *data)
{
	FormatInfo_t *info = data->format_info;
	double worldx, worldy;

	gnome_canvas_window_to_world (canvas, event->x, event->y, &worldx, &worldy);
	
	gtk_clist_select_row (info->format_collist, stf_preview_get_column_at_x (info->format_run_renderdata, worldx), 0);
	
	return TRUE;
}
 
/**
 * format_page_collist_select_row
 * @clist : GtkCList which emmitted the signal
 * @row : row the user selected
 * @column : column the user selected
 * @event : some info on the button the user clicked (unused)
 * @data : Dialog "mother" record
 *
 * this will simply set the gtkentry info->format_format's text to the (char*) format associated
 * with @row (@row is actually the column in the @data->src->sheet *confusing*)
 *
 * returns : nothing
 **/
static void
format_page_collist_select_row (GtkCList *clist, int row, int column, GdkEventButton *event, DruidPageData_t *data)
{
	FormatInfo_t *info = data->format_info;
	char *colformat = g_slist_nth_data (info->format_run_list, row);
	if (!colformat) return;

	stf_preview_set_activecolumn (info->format_run_renderdata, row);

	if (gtk_clist_row_is_visible (info->format_collist, row) == GTK_VISIBILITY_NONE)
		gtk_clist_moveto (info->format_collist, row, 0, 0.5, 0.5);
		
	if (info->format_run_manual_change) {
		info->format_run_manual_change = FALSE;
		return;
	}
	
	info->format_run_index = row;
	gtk_entry_set_text (info->format_format, colformat);
}

/**
 * format_page_sublist_select_row
 * @clist : GtkCList which emmitted the signal
 * @row : row the user selected
 * @column : column the user selected
 * @event : some info on the button the user clicked (unused)
 * @data : Dialog "mother" record
 *
 * If the user selects a different format from @clist, the caption of info->format_format will
 * change to the entry in the @clist the user selected
 *
 * returns : nothing
 **/
static void
format_page_sublist_select_row (GtkCList *clist, int row, int column, GdkEventButton *event, DruidPageData_t *data)
{
	FormatInfo_t *info = data->format_info;
	char *t[1];

	/* User did not select, it was done in the code with gtk_clist_select_row */
	if (info->format_run_manual_change) {
		info->format_run_manual_change = FALSE;
		return;
	}
	
	/* WEIRD THING : when scrolling with keys it will give the right row, but always -1 as column,
	   because we have only one column, always set "column" to 0 for now */
	column = 0;
	
	gtk_clist_get_text (clist, row, column, t);

	if (strcmp (t[0], _("Custom")) != 0)  
		gtk_entry_set_text (info->format_format, t[0]);
}

/**
 * format_page_format_changed
 * @entry : GtkEntry which emitted the signal
 * @data : Dialog "mother" record
 * 
 * Updates the selected column on the sheet with the new
 * format the user choose/entered.
 *
 * returns : nothing
 **/
static void
format_page_format_changed (GtkEntry *entry, DruidPageData_t *data)
{
	GSList *listitem;
	FormatInfo_t *info = data->format_info;
	char *format = gtk_editable_get_chars (GTK_EDITABLE (entry), 0, -1);
	char *t[1];
	int i, found;
	
	if (info->format_run_index >= 0) {

		listitem = g_slist_nth (info->format_run_list, info->format_run_index);
		g_return_if_fail (listitem != NULL);

		if (strcmp (listitem->data, format) != 0)
			stf_cache_options_invalidate (info->format_run_cacheoptions);
		
		if (listitem->data)
			g_free (listitem->data);
		listitem->data = format;

		gtk_clist_set_text (info->format_collist, info->format_run_index, 1, format);
		
		gtk_clist_set_column_width (info->format_collist,
					    1,
					    gtk_clist_optimal_column_width (info->format_collist, 1));
					    
		found = 0;
		for (i = 0; i < info->format_sublist->rows; i++) {
			gtk_clist_get_text (info->format_sublist, i, 0, t);
			if (strcmp (t[0], format)==0) {
				found = i;
				break;
			}
		}
		
		info->format_run_manual_change = TRUE;
		gtk_clist_select_row (info->format_sublist, found, 0);
		
		if (gtk_clist_row_is_visible (info->format_sublist, found) == GTK_VISIBILITY_NONE)
			gtk_clist_moveto (info->format_sublist, found, 0, 0.5, 0.5);
			
	}
		
	format_page_update_preview (data);
}

/*************************************************************************************************
 * FORMAT EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * stf_dialog_format_page_prepare
 * @page : format page
 * @druid : gnome druid hosting @page
 * @data : mother struct
 *
 * This will prepare the widgets on the format page before
 * the page gets displayed
 *
 * returns : nothing
 **/
void
stf_dialog_format_page_prepare (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *data)
{
	FormatInfo_t *info = data->format_info;
	char *t[2];
	int listcount, i;

	data->colcount = stf_parse_get_colcount (info->format_run_parseoptions, data->cur);
	
	listcount = g_slist_length (info->format_run_list);

	/* If necessary add new items (non-visual) */
	while (listcount <= data->colcount) {
		info->format_run_list = g_slist_append (info->format_run_list, (char*) g_strdup (cell_formats[0][0]));
		listcount++;
	}
	
	/* Add new items visual */
	gtk_clist_clear (info->format_collist);
	
	for (i = 0; i <= data->colcount; i++) {
		t[0] = g_strdup_printf ("%d", i);
		t[1] = g_slist_nth_data (info->format_run_list, i);
		gtk_clist_append (info->format_collist, t);
		g_free (t[0]);
	}

	gtk_clist_columns_autosize (info->format_collist);

	GTK_RANGE (info->format_scroll)->adjustment->upper = data->lines + 1;

	stf_preview_colwidths_clear (info->format_run_renderdata);
	for (i = 0; i < data->colcount + 1; i++) {
		stf_preview_colwidths_add (info->format_run_renderdata, stf_parse_get_colwidth (info->format_run_parseoptions, data->cur, i));
	}

	stf_cache_options_set_range (info->format_run_cacheoptions,
				     info->format_run_renderdata->startrow - 1,
				     (info->format_run_renderdata->startrow - 1) + stf_preview_get_displayed_rowcount (info->format_run_renderdata));
	
	info->format_run_manual_change = TRUE;
	gtk_clist_select_row (info->format_collist, 0, 0);
	
	info->format_run_index = 0;
	
	t[0] = g_slist_nth_data (info->format_run_list, 0);
	gtk_entry_set_text (info->format_format, t[0]);
}

/**
 * stf_dialog_format_page_cleanup
 * @pagedata : mother struct
 *
 * This should be called when the druid has finished to clean up resources
 * used. In this case the format_run_list data pointers and the format_run_list
 * itself will be freed
 *
 * returns : nothing
 **/
void
stf_dialog_format_page_cleanup (DruidPageData_t *pagedata)
{
	FormatInfo_t *info = pagedata->format_info;

	stf_preview_free (info->format_run_renderdata);

	stf_cache_options_free (info->format_run_cacheoptions);
	info->format_run_cacheoptions = NULL;
}

/**
 * stf_dialog_format_page_init
 * @gui : The glade gui of the dialog
 * @pagedata : pagedata mother struct passed to signal handlers etc.
 *
 * This routine prepares/initializes all widgets on the format Page of the
 * Druid.
 *
 * returns : nothing
 **/
void
stf_dialog_format_page_init (GladeXML *gui, DruidPageData_t *pagedata)
{
	FormatInfo_t *info;
	const char * const * const * mainiterator = cell_formats;
	const char * const * subiterator;
	char *temp[1];
	int rownumber;

	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);
	g_return_if_fail (pagedata->format_info != NULL);

	info = pagedata->format_info;
		
        /* Create/get object and fill information struct */
	info->format_collist       = GTK_CLIST (glade_xml_get_widget (gui, "format_collist"));
	info->format_sublist       = GTK_CLIST (glade_xml_get_widget (gui, "format_sublist"));
	info->format_sublistholder = GTK_SCROLLED_WINDOW (glade_xml_get_widget (gui, "format_sublistholder"));     
	info->format_format        = GTK_ENTRY (glade_xml_get_widget (gui, "format_format"));

	info->format_canvas = GNOME_CANVAS   (glade_xml_get_widget (gui, "format_canvas"));
	info->format_scroll = GTK_VSCROLLBAR (glade_xml_get_widget (gui, "format_scroll"));

	/* Set properties */
	info->format_run_renderdata    = stf_preview_new (info->format_canvas, TRUE); 
	info->format_run_list          = NULL;
	info->format_run_index         = -1;
	info->format_run_manual_change = FALSE;
	info->format_run_displayrows   = stf_preview_get_displayed_rowcount (info->format_run_renderdata);
	info->format_run_cacheoptions  = stf_cache_options_new ();

        gtk_clist_column_titles_passive (info->format_sublist);

	rownumber = 0;
	temp[0] = _("Custom");
	gtk_clist_append (info->format_sublist, temp);
	while (*mainiterator) {
		subiterator = *mainiterator;
		while (*subiterator) {
			temp[0] = (char*) *subiterator;
			gtk_clist_append (info->format_sublist, temp);
			subiterator++;
			rownumber++;
		}
		mainiterator++;
	}

	gtk_clist_set_column_justification (info->format_collist, 0, GTK_JUSTIFY_RIGHT);
	
	/* Connect signals */
	gtk_signal_connect (GTK_OBJECT (info->format_format),
			    "changed",
			    GTK_SIGNAL_FUNC (format_page_format_changed),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->format_collist),
			    "select_row",
			    GTK_SIGNAL_FUNC (format_page_collist_select_row),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->format_sublist),
			    "select_row",
			    GTK_SIGNAL_FUNC (format_page_sublist_select_row),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->format_canvas),
			    "button_press_event",
			    GTK_SIGNAL_FUNC (format_page_canvas_button_press_event),
			    pagedata);
			    
	gtk_signal_connect (GTK_OBJECT (GTK_RANGE (info->format_scroll)->adjustment),
			    "value_changed",
			    GTK_SIGNAL_FUNC (format_page_scroll_value_changed),
			    pagedata);
}











