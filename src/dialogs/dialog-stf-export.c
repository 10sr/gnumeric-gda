/*
 * dialog-stf-export.c : implementation of the STF export dialog
 *
 * Copyright (C) Almer. S. Tigelaar.
 * EMail: almer1@dds.nl or almer-t@bigfoot.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include "command-context.h"
#include "dialog-stf-export.h"
#include "dialog-stf-export-private.h"
  
#define GLADE_FILE "dialog-stf-export.glade"

/**
 * stf_export_dialog_druid_page_cancel
 * @page : Active druid page
 * @druid : The parent Druid widget
 * @druid_data : mother struct 
 *
 * Presents the user with a nice cancel y/n dialognn
 *
 * returns : TRUE if the user actually wants to cancel, FALSE otherwise.
 **/
static gboolean
stf_export_dialog_druid_page_cancel (GnomeDruidPage *page, GnomeDruid *druid, StfE_DruidData_t *druid_data)
{
	GtkWidget *dialog, *no_button;
	int ret;
	
	g_return_val_if_fail (page != NULL, TRUE);
	g_return_val_if_fail (druid != NULL, TRUE);
	g_return_val_if_fail (druid_data != NULL, TRUE);
	
	dialog = gnome_question_dialog_parented (_("Are you sure you want to cancel?"), 
						 NULL,
						 NULL,
						 druid_data->window);

	no_button = g_list_last (GNOME_DIALOG (dialog)->buttons)->data;
	gtk_widget_grab_focus (no_button);
	ret = gnome_dialog_run (GNOME_DIALOG (dialog));

	gtk_main_quit ();
	
	return (ret==1);
}

/** 
 * stf_dialog_set_initial_keyboard_focus
 * @druid_data : mother struct
 *
 * Sets keyboard focus to the an appropriate widget on the page.
 *
 * returns : nothing
 **/
static void
stf_export_dialog_set_initial_keyboard_focus (StfE_DruidData_t *druid_data)
{
	GtkWidget *focus_widget = NULL;
	g_return_if_fail (druid_data != NULL);

	switch (druid_data->active_page) {
	case DPG_SHEET  :
		focus_widget = (GtkWidget *) druid_data->sheet_page_data->sheet_avail;
		break;
	case DPG_FORMAT :
		focus_widget = (GtkWidget *) druid_data->format_page_data->format_termination;
		break;
	default :
		g_warning ("Unknown druid position");
	}
	
	if (focus_widget)
		gtk_widget_grab_focus (focus_widget);
}

/**
 * stf_export_dialog_format_page_druid_finish
 * @druid : a druid
 * @page : a druidpage
 * @druid_data : mother struct
 *
 * Stops the druid but does not set the cancel property of @data.
 * The main routine (stf_export_dialog()) will know that the druid has successfully
 * been completed.
 *
 * returns : nothing
 **/
static void
stf_export_dialog_druid_format_page_finish (GnomeDruid *druid, GnomeDruidPage *page, StfE_DruidData_t *druid_data)
{
	g_return_if_fail (page != NULL);
	g_return_if_fail (druid != NULL);
	g_return_if_fail (druid_data != NULL);
	
	gtk_main_quit ();
}

/** 
 * stf_export_dialog_druid_position_to_page
 * @druid_data : mother struct
 * @pos : Position in the druid
 *
 * Will translate a DPG_* position into a pointer to the page.
 *
 * returns : A pointer to the GnomeDruidPage indicated by @pos
 **/
static GnomeDruidPage*
stf_export_dialog_druid_position_to_page (StfE_DruidData_t *druid_data, StfE_DruidPosition_t pos)
{
	switch (pos) {
	case DPG_SHEET  : return druid_data->sheet_page;
	case DPG_FORMAT : return druid_data->format_page;
	default :
		g_warning ("Unknown druid position");
		return NULL;
	}
}

/**
 * stf_export_dialog_druid_page_next
 * @page : A druid page
 * @druid : The druid itself
 * @druid_data : mother struct
 * 
 * This function will determine and set the next page depending on choices
 * made in previous pages
 *
 * returns : always TRUE, because it always sets the new page manually
 **/
static gboolean
stf_export_dialog_druid_page_next (GnomeDruidPage *page, GnomeDruid *druid, StfE_DruidData_t *druid_data)
{
	StfE_DruidPosition_t newpos;
	GnomeDruidPage *nextpage;

	g_return_val_if_fail (page != NULL, FALSE);
	g_return_val_if_fail (druid != NULL, FALSE);
	g_return_val_if_fail (druid_data != NULL, FALSE);
	
	switch (druid_data->active_page) {
	case DPG_SHEET : {
		if (!stf_export_dialog_sheet_page_can_continue (GTK_WIDGET (druid_data->window),
								druid_data->sheet_page_data)) {
			return TRUE; /* If we are not ready to continue stick to the current page */
		}		
		newpos = DPG_FORMAT;
		break;
	}
	default :
		g_warning ("Page Cycle Error : Unknown page %d", druid_data->active_page);
		return FALSE;
	}

	nextpage = stf_export_dialog_druid_position_to_page (druid_data, newpos);
	if (!nextpage)
		return FALSE;
		
	gnome_druid_set_page (druid, nextpage);
	druid_data->active_page = newpos;

	stf_export_dialog_set_initial_keyboard_focus (druid_data);
	
	if (newpos == DPG_FORMAT) {
	
		gnome_druid_set_show_finish (druid_data->druid, TRUE);
		gtk_widget_grab_default (druid_data->druid->finish);
	}

	return TRUE;
}

/**
 * stf_export_dialog_druid_page_previous
 * @page : a druid page
 * @druid : a druid
 * @druid_data : mother struct
 * 
 * Determines the previous page based on choices made earlier on
 *
 * returns : always TRUE, because it always cycles to the previous page manually
 **/
static gboolean
stf_export_dialog_druid_page_previous (GnomeDruidPage *page, GnomeDruid *druid, StfE_DruidData_t *druid_data)
{
	StfE_DruidPosition_t newpos;
	GnomeDruidPage *previouspage;

	g_return_val_if_fail (page != NULL, FALSE);
	g_return_val_if_fail (druid != NULL, FALSE);
	g_return_val_if_fail (druid_data != NULL, FALSE);

	switch (druid_data->active_page) {
	case DPG_FORMAT : newpos = DPG_SHEET; break;
	default :
		g_warning ("Page Cycle Error : Unknown page %d", druid_data->active_page);
		return FALSE;
	}
	
	previouspage = stf_export_dialog_druid_position_to_page (druid_data, newpos);
	if (!previouspage)
		return FALSE;

	gnome_druid_set_page (druid_data->druid, previouspage);
	druid_data->active_page = newpos;

	stf_export_dialog_set_initial_keyboard_focus (druid_data);
	
	if (newpos == DPG_SHEET)
		gnome_druid_set_buttons_sensitive (druid, FALSE, TRUE, TRUE);
	else
		gtk_widget_grab_default (druid_data->druid->next);
		
	return TRUE;
}

/**
 * stf_export_dialog_druid_cancel
 * @druid : a druid
 * @druid_data : mother struct
 * 
 * Stops the druid and indicates the user has cancelled
 *
 * returns : nothing
 **/
static void
stf_export_dialog_druid_cancel (GnomeDruid *druid, StfE_DruidData_t *druid_data)
{
	g_return_if_fail (druid != NULL);
	g_return_if_fail (druid_data != NULL);
	
	druid_data->canceled = TRUE;
	gtk_main_quit ();
}

/**
 * stf_export_dialog_check_escape
 * @druid : a druid
 * @event : the event
 * @druid_data : mother struct
 * 
 * Stops the druid if the user pressed escape.
 *
 * returns : TRUE if we handled the keypress, FALSE if we pass it on.
 **/
static gint
stf_export_dialog_check_escape (GnomeDruid *druid, GdkEventKey *event,
				StfE_DruidData_t *druid_data)
{
	g_return_val_if_fail (druid != NULL, FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	g_return_val_if_fail (druid_data != NULL, FALSE);
	
	if (event->keyval == GDK_Escape) {
		gtk_button_clicked (GTK_BUTTON (druid_data->druid->cancel));
		return TRUE;
	} else
		return FALSE;
}

/**
 * stf_export_dialog_attach_page_signals
 * @gui : the glade gui of the dialog
 * @druid_data : mother struct
 * 
 * Connects all signals to all pages and fills the mother struct
 *
 * returns : nothing
 **/
static void
stf_export_dialog_attach_page_signals (GladeXML *gui, StfE_DruidData_t *druid_data)
{
	g_return_if_fail (gui != NULL);
	g_return_if_fail (druid_data != NULL);

	druid_data->window     = GTK_WINDOW  (glade_xml_get_widget (gui, "window"));
	druid_data->druid      = GNOME_DRUID (glade_xml_get_widget (gui, "druid"));

	druid_data->sheet_page   = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "sheet_page"));
	druid_data->format_page  = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "format_page"));

	druid_data->active_page  = DPG_SHEET;
	
	gnome_druid_set_buttons_sensitive (druid_data->druid, FALSE, TRUE, TRUE);
	
	/* Signals for individual pages */

	gtk_signal_connect (GTK_OBJECT (druid_data->sheet_page), 
			    "next", 
			    GTK_SIGNAL_FUNC (stf_export_dialog_druid_page_next),
			    druid_data);
			    
        gtk_signal_connect (GTK_OBJECT (druid_data->format_page), 
			    "back", 
			    GTK_SIGNAL_FUNC (stf_export_dialog_druid_page_previous),
			    druid_data);
	gtk_signal_connect (GTK_OBJECT (druid_data->format_page), 
			    "finish", 
			    GTK_SIGNAL_FUNC (stf_export_dialog_druid_format_page_finish),
			    druid_data);
			    
	gtk_signal_connect (GTK_OBJECT (druid_data->sheet_page), 
			    "cancel", 
			    GTK_SIGNAL_FUNC (stf_export_dialog_druid_page_cancel),
			    druid_data);
	gtk_signal_connect (GTK_OBJECT (druid_data->format_page), 
			    "cancel", 
			    GTK_SIGNAL_FUNC (stf_export_dialog_druid_page_cancel),
			    druid_data);

	/* Signals for the druid itself */

	gtk_signal_connect (GTK_OBJECT (druid_data->druid), 
			    "cancel", 
			    GTK_SIGNAL_FUNC (stf_export_dialog_druid_cancel),
			    druid_data);

	/* And for the surrounding window */

	gtk_signal_connect (GTK_OBJECT (druid_data->window),
			    "key_press_event",
			    GTK_SIGNAL_FUNC (stf_export_dialog_check_escape),
			    druid_data);
}

/** 
 * stf_export_dialog_editables_enter
 * @druid_date : mother struct
 *
 * Make <Ret> in text fields activate default.
 *
 * returns : nothing
 **/
static void
stf_export_dialog_editables_enter (StfE_DruidData_t *druid_data)
{
	gnumeric_editable_enters (druid_data->window,
				  GTK_EDITABLE (druid_data->format_page_data->format_custom));
	gnumeric_combo_enters (druid_data->window,
			       druid_data->format_page_data->format_quotechar);
}

/**
 * stf_dialog
 * @context : a Commandcontext (can be NULL)
 * @wb : The workbook to export
 *
 * This will start the export druid.
 * (NOTE : you have to free the DialogStfResult_t that this function returns by
 *  using the stf_export_dialog_result_free function)
 *
 * returns : A StfE_Result_t struct on success, NULL otherwise.
 **/
StfE_Result_t *
stf_export_dialog (CommandContext *context, Workbook *wb)
{
	GladeXML *gui;
	StfE_Result_t *dialogresult;
	StfE_DruidData_t druid_data;
	char* message;

	g_return_val_if_fail (wb != NULL, NULL);

	gui = gnumeric_glade_xml_new (NULL, GLADE_FILE);	
	if (!gui) {
	
		message = g_strdup_printf (_("Missing %s file"), GLADE_FILE);
		
		if (context)
			gnumeric_error_read (context, message);
		else
			g_warning (message);
			
		g_free (message);
		
		return NULL;
	}

	druid_data.canceled         = FALSE;
	druid_data.sheet_page_data  = stf_export_dialog_sheet_page_init (gui, wb);
	druid_data.format_page_data = stf_export_dialog_format_page_init (gui);

	stf_export_dialog_attach_page_signals (gui, &druid_data);

	stf_export_dialog_editables_enter (&druid_data);
	gtk_widget_grab_default (druid_data.druid->next);
	
	gnumeric_set_transient (context, druid_data.window);
	gtk_widget_show (GTK_WIDGET (druid_data.window));
	
	gtk_main ();

	if (druid_data.canceled) {
	
		dialogresult = NULL;
	} else {
		
		dialogresult = g_new (StfE_Result_t, 1);

		/* Construct the export options */
		dialogresult->export_options = stf_export_options_new ();

		stf_export_dialog_sheet_page_result (druid_data.sheet_page_data,
						     dialogresult->export_options);
		stf_export_dialog_format_page_result (druid_data.format_page_data,
						     dialogresult->export_options);

		
	}

	stf_export_dialog_sheet_page_cleanup (druid_data.sheet_page_data);
	stf_export_dialog_format_page_cleanup (druid_data.format_page_data);

	gtk_widget_destroy (GTK_WIDGET (druid_data.window));
	gtk_object_unref (GTK_OBJECT (gui));
	
	return dialogresult;
}

/**
 * stf_export_dialog_result_free:
 * @result: an StfE_Result_t struct
 * 
 * This routine will properly free @result and its members
 **/
void
stf_export_dialog_result_free (StfE_Result_t *result)
{
	stf_export_options_free (result->export_options);
	
	g_free (result);
}
