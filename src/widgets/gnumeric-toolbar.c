/*
 * Gnumeric-toolbar.c: A toolbar that will be extended to be like
 * the Excel one.  It will keep track of user selected items that have
 * been hidden or shown and act accordingly.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric-toolbar.h"

static GtkObjectClass *gnumeric_toolbar_parent_class;

static void
gnumeric_toolbar_finalize (GtkObject *object)
{
	gnumeric_toolbar_parent_class->finalize (object);
}

static void
gnumeric_toolbar_class_init (GtkObjectClass *object_class)
{
	object_class->finalize = gnumeric_toolbar_finalize;

	gnumeric_toolbar_parent_class = gtk_type_class (GTK_TOOLBAR_TYPE);
}

GtkType
gnumeric_toolbar_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"GnumericToolbar",
			sizeof (GnumericToolbar),
			sizeof (GnumericToolbarClass),
			(GtkClassInitFunc) gnumeric_toolbar_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_toolbar_get_type (), &info);
	}

	return type;
}

void
gnumeric_toolbar_construct (GnumericToolbar *toolbar,
			    GnomeUIInfo *info,
			    void *data)
{
	GtkToolbar *gtk_toolbar = GTK_TOOLBAR (toolbar);
	
	g_return_if_fail (info != NULL);
	g_return_if_fail (toolbar != NULL);
	g_return_if_fail (IS_GNUMERIC_TOOLBAR (toolbar));
	
	gtk_toolbar->orientation = GTK_ORIENTATION_HORIZONTAL;
	gtk_toolbar->style = GTK_TOOLBAR_ICONS;

	/*
	 * Fixme: load the show_state from configuration
	 */
	gnome_app_fill_toolbar_with_data (
		gtk_toolbar, info, NULL, data);

	gtk_toolbar_set_style (gtk_toolbar, GTK_TOOLBAR_ICONS);
}

GtkWidget *
gnumeric_toolbar_new (GnomeUIInfo *info, void *data)
{
	GnumericToolbar *toolbar;

	g_return_val_if_fail (info != NULL, NULL);
	
	toolbar = gtk_type_new (gnumeric_toolbar_get_type ());
	gnumeric_toolbar_construct (toolbar, info, data);

	return GTK_WIDGET (toolbar);
}

GtkWidget *
gnumeric_toolbar_get_widget (GnumericToolbar *toolbar, int pos)
{
	GtkToolbarChild *child;
	GList *children;
	int i;
	
	g_return_val_if_fail (toolbar != NULL, NULL);
	g_return_val_if_fail (IS_GNUMERIC_TOOLBAR (toolbar), NULL);
	g_return_val_if_fail (pos > 0, NULL);

	children = GTK_TOOLBAR (toolbar)->children;

	if (!children)
		return NULL;

	i = 0;
	do {
		child = children->data;
		children = children->next;

		if (child->type == GTK_TOOLBAR_CHILD_SPACE)
			continue;

		if (i == pos)
			return child->widget;
		i++;
	} while (children);

	return NULL;
}



