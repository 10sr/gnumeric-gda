/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnumeric-text-view.c: A textview extension handling formatting
 *
 * Copyright (C) 2009  Andreas J. Guelzow

 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 or 3 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
*/

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "gnumeric-text-view.h"

#include <gui-util.h>

#include <gsf/gsf-impl-utils.h>
#include <gtk/gtk.h>
#include <dead-kittens.h>

struct _GnmTextView {
	GtkVBox	parent;

	GtkTextBuffer *buffer;
	GtkTextView *view;

	GtkToggleToolButton *italic;
	GtkToggleToolButton *strikethrough;
	GtkToolButton *bold;
};

typedef struct _GnmTextViewClass {
	GtkVBoxClass base;

	void (* changed)  (GnmTextView *gtv);
} GnmTextViewClass;

/* Signals */
enum {
	CHANGED,
	LAST_SIGNAL
};

/* Properties */
enum {
	PROP_0,
	PROP_TEXT,
	PROP_WRAP,
	PROP_ATTR
};

static guint signals [LAST_SIGNAL] = { 0 };

/* Internal routines */

static void
cb_gtv_emit_changed (G_GNUC_UNUSED GtkTextBuffer *buffer, GnmTextView *gtv)
{
	g_signal_emit (G_OBJECT (gtv), signals [CHANGED], 0);
}

static void
gnm_toggle_tool_button_set_active_no_signal (GtkToggleToolButton *button,
					     gboolean is_active,
					     GnmTextView *gtv)
{
	gulong handler_id = g_signal_handler_find (button, G_SIGNAL_MATCH_DATA,
						   0, 0, NULL, NULL, gtv);

	g_signal_handler_block (button, handler_id);
	gtk_toggle_tool_button_set_active (button, is_active);
	g_signal_handler_unblock (button, handler_id);
}

static void
cb_gtv_mark_set (GtkTextBuffer *buffer,
					  G_GNUC_UNUSED GtkTextIter   *location,
					  G_GNUC_UNUSED GtkTextMark   *mark,
					  GnmTextView *gtv)
{
	GtkTextIter start, end;
	gtk_text_buffer_get_selection_bounds (gtv->buffer, &start, &end);

	{ /* Handling italic button */
		GtkTextTag *tag_italic = gtk_text_tag_table_lookup
			(gtk_text_buffer_get_tag_table (gtv->buffer),
			 "PANGO_STYLE_ITALIC");
		gnm_toggle_tool_button_set_active_no_signal
			(gtv->italic,
			 (tag_italic != NULL)
			 && gtk_text_iter_has_tag (&start, tag_italic),
			 gtv);
	}
	{ /* Handling strikethrough button */
		GtkTextTag *tag_strikethrough = gtk_text_tag_table_lookup
			(gtk_text_buffer_get_tag_table (gtv->buffer),
			 "PANGO_STRIKETHROUGH_TRUE");
		gnm_toggle_tool_button_set_active_no_signal
			(gtv->strikethrough,
			 (tag_strikethrough != NULL)
			 && gtk_text_iter_has_tag (&start, tag_strikethrough),
			 gtv);
	}
}

static void
cb_gtv_set_italic (GtkToggleToolButton *toolbutton, GnmTextView *gtv)
{
	GtkTextIter start, end;

	if (gtk_text_buffer_get_selection_bounds (gtv->buffer, &start, &end)) {
		GtkTextTag *tag_italic = gtk_text_tag_table_lookup
			(gtk_text_buffer_get_tag_table (gtv->buffer),
			 "PANGO_STYLE_ITALIC");
		GtkTextTag *tag_normal = gtk_text_tag_table_lookup
			(gtk_text_buffer_get_tag_table (gtv->buffer),
			 "PANGO_STYLE_NORMAL");

		if (gtk_text_iter_has_tag (&start, tag_italic)) {
			gtk_text_buffer_remove_tag (gtv->buffer, tag_italic,
						    &start, &end);
			gtk_text_buffer_apply_tag (gtv->buffer, tag_normal,
						   &start, &end);
		} else {
			gtk_text_buffer_remove_tag (gtv->buffer, tag_normal,
						    &start, &end);
			gtk_text_buffer_apply_tag (gtv->buffer, tag_italic,
						   &start, &end);
		}
		cb_gtv_emit_changed (NULL, gtv);
	}
}

static void
cb_gtv_set_strikethrough (GtkToggleToolButton *toolbutton, GnmTextView *gtv)
{
	GtkTextIter start, end;

	if (gtk_text_buffer_get_selection_bounds (gtv->buffer, &start, &end)) {
		GtkTextTag *tag_no_strikethrough = gtk_text_tag_table_lookup
			(gtk_text_buffer_get_tag_table (gtv->buffer),
			 "PANGO_STRIKETHROUGH_FALSE");
		GtkTextTag *tag_strikethrough = gtk_text_tag_table_lookup
			(gtk_text_buffer_get_tag_table (gtv->buffer),
			 "PANGO_STRIKETHROUGH_TRUE");

		if (gtk_text_iter_has_tag (&start, tag_strikethrough)) {
			gtk_text_buffer_remove_tag (gtv->buffer, tag_strikethrough,
						    &start, &end);
			gtk_text_buffer_apply_tag (gtv->buffer, tag_no_strikethrough,
						   &start, &end);
		} else {
			gtk_text_buffer_remove_tag (gtv->buffer, tag_no_strikethrough,
						    &start, &end);
			gtk_text_buffer_apply_tag (gtv->buffer, tag_strikethrough,
						   &start, &end);
		}
		cb_gtv_emit_changed (NULL, gtv);
	}
}

static GtkToggleToolButton *
gtv_build_toggle_button (GtkWidget *tb,  GnmTextView *gtv,
			 char const *button_name, GCallback cb)
{
	GtkToolItem * tb_button;

	tb_button = gtk_toggle_tool_button_new_from_stock (button_name);
	gtk_toolbar_insert(GTK_TOOLBAR(tb), tb_button, -1);
	g_signal_connect (G_OBJECT (tb_button), "toggled", cb, gtv);
	return GTK_TOGGLE_TOOL_BUTTON (tb_button);
}

static void
gtv_remove_weight_tags (GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end)
{
	static char const *tag_names[] = {
		"PANGO_WEIGHT_THIN",
		"PANGO_WEIGHT_ULTRALIGHT",
		"PANGO_WEIGHT_LIGHT",
		"PANGO_WEIGHT_BOOK",
		"PANGO_WEIGHT_NORMAL",
		"PANGO_WEIGHT_MEDIUM",
		"PANGO_WEIGHT_SEMIBOLD",
		"PANGO_WEIGHT_BOLD",
		"PANGO_WEIGHT_ULTRABOLD",
		"PANGO_WEIGHT_HEAVY",
		"PANGO_WEIGHT_ULTRAHEAVY",
		NULL
	};
	char const **tag_names_ptr;

	for (tag_names_ptr = tag_names; *tag_names_ptr != NULL; tag_names_ptr++)
		gtk_text_buffer_remove_tag_by_name (buffer, *tag_names_ptr, start, end);
}


static void
gtv_bold_button_activated (GtkMenuItem *menuitem, GnmTextView *gtv)
{
	char const *val = g_object_get_data (G_OBJECT (menuitem), "boldvalue");
	if (val != NULL) {
		GtkTextIter start, end;
		if (gtk_text_buffer_get_selection_bounds (gtv->buffer, &start, &end)) {
			GtkTextTag *tag = gtk_text_tag_table_lookup
				(gtk_text_buffer_get_tag_table (gtv->buffer), val);
			gtv_remove_weight_tags (gtv->buffer, &start, &end);
			gtk_text_buffer_apply_tag (gtv->buffer, tag, &start, &end);
			cb_gtv_emit_changed (NULL, gtv);
		}
		g_object_set_data (G_OBJECT (gtv->bold), "boldvalue", (char *) val);
	}
}

#define SETUPBPLDMENUITEM(string, value)                                       \
	child = gtk_menu_item_new_with_label (string);                         \
        gtk_widget_show (child);					       \
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), child);                  \
	g_signal_connect (G_OBJECT (child), "activate",                        \
			  G_CALLBACK (gtv_bold_button_activated),              \
                          gtv);                                                \
	g_object_set_data (G_OBJECT (child), "boldvalue",                      \
			   (char *) value);


static GtkToolButton *
gtv_build_button_bold (GtkWidget *tb, GnmTextView *gtv)
{
	GtkToolItem * tb_button;
	GtkWidget *menu;
	GtkWidget *child;

	menu = gtk_menu_new ();

	SETUPBPLDMENUITEM(_("Thin"), "PANGO_WEIGHT_THIN")
	SETUPBPLDMENUITEM(_("Ultralight"), "PANGO_WEIGHT_ULTRALIGHT")
	SETUPBPLDMENUITEM(_("Light"), "PANGO_WEIGHT_LIGHT")
	SETUPBPLDMENUITEM(_("Normal"), "PANGO_WEIGHT_NORMAL")
	SETUPBPLDMENUITEM(_("Medium"), "PANGO_WEIGHT_MEDIUM")
	SETUPBPLDMENUITEM(_("Semibold"), "PANGO_WEIGHT_SEMIBOLD")
	SETUPBPLDMENUITEM(_("Bold"), "PANGO_WEIGHT_BOLD")
	SETUPBPLDMENUITEM(_("Ultrabold"), "PANGO_WEIGHT_ULTRABOLD")
	SETUPBPLDMENUITEM(_("Heavy"), "PANGO_WEIGHT_HEAVY")
	SETUPBPLDMENUITEM(_("Ultraheavy"), "PANGO_WEIGHT_ULTRAHEAVY")

	tb_button = gtk_menu_tool_button_new_from_stock (GTK_STOCK_BOLD);
	gtk_toolbar_insert(GTK_TOOLBAR(tb), tb_button, -1);
	gtk_menu_tool_button_set_menu (GTK_MENU_TOOL_BUTTON (tb_button), menu);
	g_object_set_data (G_OBJECT (tb_button), "boldvalue",
                           (char *) "PANGO_WEIGHT_BOLD");
	g_signal_connect (G_OBJECT (tb_button), "clicked",
			  G_CALLBACK (gtv_bold_button_activated),
                          gtv);
	return GTK_TOOL_BUTTON (tb_button);
}

#undef SETUPBPLDMENUITEM


/* Object routines */

static GObjectClass *parent_class = NULL;

static void
gtv_destroy (GtkWidget *widget)
{
	gnm_destroy_class_chain (parent_class, widget);
}


static void
gtv_set_property (GObject      *object,
		  guint         prop_id,
		  GValue const *value,
		  GParamSpec   *pspec)
{
	GnmTextView *gtv = GNM_TEXT_VIEW (object);
	switch (prop_id) {
	case PROP_TEXT:
		gtk_text_buffer_set_text (gtv->buffer,
					  g_value_get_string (value), -1);
		break;
	case PROP_WRAP:
		gtk_text_view_set_wrap_mode (gtv->view, g_value_get_int (value));
		break;
	case PROP_ATTR:
		gnm_load_pango_attributes_into_buffer (g_value_get_boxed (value),
						       gtv->buffer, NULL);
	break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
gtv_get_property (GObject      *object,
		  guint         prop_id,
		  GValue       *value,
		  GParamSpec   *pspec)
{
	GnmTextView *gtv = GNM_TEXT_VIEW (object);
	switch (prop_id) {
	case PROP_TEXT:
	{
		char *text;
		text = gnumeric_textbuffer_get_text (gtv->buffer);
		g_value_set_string (value, text);
		g_free (text);
	}
	break;
	case PROP_WRAP:
		g_value_set_int (value, gtk_text_view_get_wrap_mode (gtv->view));
	break;
	case PROP_ATTR:
	{
		PangoAttrList *attr = gnm_get_pango_attributes_from_buffer
			(gtv->buffer);
		g_value_take_boxed (value, attr);
	}
	break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}


static void
gtv_init (GnmTextView *gtv)
{
	GtkWidget *tb = gtk_toolbar_new ();
	GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);

	gtv->view = GTK_TEXT_VIEW (gtk_text_view_new ());
	gtv->buffer = gtk_text_view_get_buffer (gtv->view);
	gnm_create_std_tags_for_buffer (gtv->buffer);

	gtv->italic = gtv_build_toggle_button (tb, gtv, GTK_STOCK_ITALIC,
					       G_CALLBACK (cb_gtv_set_italic));
	gtv->strikethrough = gtv_build_toggle_button (tb, gtv,
						      GTK_STOCK_STRIKETHROUGH,
						      G_CALLBACK
						      (cb_gtv_set_strikethrough));
	gtk_toolbar_insert(GTK_TOOLBAR(tb), gtk_separator_tool_item_new (), -1);
	gtv->bold = gtv_build_button_bold (tb, gtv);

	gtk_container_set_border_width (GTK_CONTAINER (gtv->view), 5);
	gtk_text_view_set_wrap_mode (gtv->view, GTK_WRAP_WORD_CHAR);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);

	g_signal_connect (G_OBJECT (gtv->buffer), "changed",
			  G_CALLBACK (cb_gtv_emit_changed), gtv);
	g_signal_connect (G_OBJECT (gtv->buffer), "mark_set",
			  G_CALLBACK (cb_gtv_mark_set), gtv);

	gtk_box_pack_start (GTK_BOX (gtv), tb, FALSE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (sw), GTK_WIDGET (gtv->view));
	gtk_box_pack_start (GTK_BOX (gtv), sw, TRUE, TRUE, 0);


}

static void
gtv_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->set_property	= gtv_set_property;
	gobject_class->get_property	= gtv_get_property;
	gnm_destroy_class_set (gobject_class, gtv_destroy);

	signals [CHANGED] = g_signal_new ("changed",
		GNM_TEXT_VIEW_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmTextViewClass, changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	g_object_class_install_property (gobject_class,
		PROP_TEXT,
		g_param_spec_string ("text", "Text",
			"The text content",
			"",
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		PROP_WRAP,
		g_param_spec_int ("wrap", "Wrap",
				  "The wrapping mode",
				  GTK_WRAP_NONE, GTK_WRAP_WORD_CHAR,
				  GTK_WRAP_WORD,
				  GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property
		(gobject_class, PROP_ATTR,
		 g_param_spec_boxed
		 ("attributes", "PangoAttrList",
		  "A PangoAttrList derived from the buffer content.",
		  PANGO_TYPE_ATTR_LIST,
		  GSF_PARAM_STATIC | G_PARAM_READWRITE));
}


GSF_CLASS (GnmTextView, gnm_text_view,
	   gtv_class_init,
	   gtv_init, GTK_TYPE_VBOX)


/**
 * gnm_text_view_new:
 * @wbcg : #WBCGtk non-NULL
 * @with_icon : append a rollup icon to the end of the entry
 *
 * Creates a new #GnmTextView, which is an entry widget with support
 * for range selections.
 * The entry is created with default flag settings which are suitable for use
 * in many dialogs, but see #gnm_text_view_set_flags.
 *
 * Return value: a new #GnmTextView.
 **/
GnmTextView *
gnm_text_view_new (void)
{
	return g_object_new (GNM_TEXT_VIEW_TYPE, NULL);
}


