/*
 * Support for dynamically-loaded Gnumeric plugin components.
 *
 * Authors:
 *  Old plugin engine:
 *    Tom Dyas (tdyas@romulus.rutgers.edu)
 *    Dom Lachowicz (dominicl@seas.upenn.edu)
 *  New plugin engine:
 *    Zbigniew Chyla (cyba@gnome.pl)
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <glib.h>
#include <gmodule.h>
#include <gnome.h>
#include <gnome-xml/parser.h>
#include <gnome-xml/parserInternals.h>
#include <gnome-xml/xmlmemory.h>
#include <gal/util/e-util.h>
#include <gal/util/e-xml-utils.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gutils.h"
#include "command-context.h"
#include "xml-io.h"
#include "file.h"
#include "workbook.h"
#include "workbook-view.h"
#include "error-info.h"
#include "plugin-loader.h"
#include "plugin-loader-module.h"
#include "plugin-service.h"
#include "plugin.h"

#define PLUGIN_INFO_FILE_NAME          "plugin.xml"

typedef struct _PluginLoaderStaticInfo PluginLoaderStaticInfo;
struct _PluginLoaderStaticInfo {
	gchar *loader_type_str;
	GList *attr_names, *attr_values;
};

struct _PluginInfo {
	gchar   *dir_name;
	gchar   *id;
	gchar   *name;
	gchar   *description;

	gboolean is_active;
	PluginLoaderStaticInfo *loader_static_info;
	GnumericPluginLoader *loader;
	GList *service_list;
	PluginServicesData *services_data;
};

typedef struct _PluginLoaderTypeInfo PluginLoaderTypeInfo;
struct _PluginLoaderTypeInfo {
	gchar  *id_str;
	gboolean has_type;
	GtkType loader_type;
	PluginLoaderGetTypeCallback get_type_callback;
	gpointer get_type_callback_data;
};

static GList *plugins_marked_for_deactivation = NULL;

static GList *known_plugin_id_list = NULL;
static gboolean known_plugin_id_list_is_ready = FALSE;

static GList *available_plugin_info_list = NULL;
static gboolean available_plugin_info_list_is_ready = FALSE;

static GList *saved_active_plugin_id_list = NULL;
static gboolean saved_active_plugin_id_list_is_ready = FALSE;

static GList *registered_loader_types = NULL;

static void plugin_get_loader_if_needed (PluginInfo *pinfo, ErrorInfo **ret_error);

/*
 * Accessor functions
 */

gchar *
plugin_info_get_dir_name (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return g_strdup (pinfo->dir_name);
}

gchar *
plugin_info_get_id (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return g_strdup (pinfo->id);
}

gchar *
plugin_info_get_name (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return g_strdup (pinfo->name);
}

gchar *
plugin_info_get_description (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return g_strdup (pinfo->description);
}

gboolean
plugin_info_is_active (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, FALSE);

	return pinfo->is_active;
}

gint
plugin_info_get_extra_info_list (PluginInfo *pinfo, GList **ret_keys_list, GList **ret_values_list)
{
	ErrorInfo *ignored_error;

	*ret_keys_list = NULL;
	*ret_values_list = NULL;

	plugin_get_loader_if_needed (pinfo, &ignored_error);
	if (ignored_error == NULL) {
		return gnumeric_plugin_loader_get_extra_info_list (pinfo->loader, ret_keys_list, ret_values_list);
	} else {
		error_info_free (ignored_error);
		return 0;
	}
}

const gchar *
plugin_info_peek_dir_name (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return pinfo->dir_name;
}

const gchar *
plugin_info_peek_id (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return pinfo->id;
}

const gchar *
plugin_info_peek_name (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return pinfo->name;
}

const gchar *
plugin_info_peek_description (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return pinfo->description;
}

const gchar *
plugin_info_peek_loader_type_str (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return pinfo->loader_static_info->loader_type_str;
}

/*
 * If loader_type_str == NULL, it returns TRUE for plugin providing _any_ loader.
 */
gboolean
plugin_info_provides_loader_by_type_str (PluginInfo *pinfo, const gchar *loader_type_str)
{
	GList *l;

	g_return_val_if_fail (pinfo != NULL, FALSE);

	for (l = pinfo->service_list; l != NULL; l = l->next) {
		PluginService *service;

		service = (PluginService *) l->data;
		if (service->service_type == PLUGIN_SERVICE_PLUGIN_LOADER &&
		    (loader_type_str == NULL ||
		     strcmp (service->t.plugin_loader.loader_id, loader_type_str) == 0)) {
			return TRUE;
		}	
	}

	return FALSE;
}

gboolean
plugin_info_is_loaded (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, FALSE);

	return pinfo->loader != NULL && gnumeric_plugin_loader_is_loaded (pinfo->loader);
}

PluginServicesData *
plugin_info_peek_services_data (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return pinfo->services_data;
}

struct _GnumericPluginLoader *
plugin_info_get_loader (PluginInfo *pinfo)
{
	g_return_val_if_fail (pinfo != NULL, NULL);

	return pinfo->loader;
}

/*
 *
 */

void
plugin_loader_register_type (const gchar *id_str, GtkType loader_type)
{
	PluginLoaderTypeInfo *loader_type_info;

	g_return_if_fail (id_str != NULL);
 
	loader_type_info = g_new (PluginLoaderTypeInfo, 1);
	loader_type_info->id_str = g_strdup (id_str);
	loader_type_info->has_type = TRUE;
	loader_type_info->loader_type = loader_type;
	loader_type_info->get_type_callback = NULL;
	loader_type_info->get_type_callback_data = NULL;
	registered_loader_types = g_list_append (registered_loader_types, loader_type_info);
}

void
plugin_loader_register_id_only (const gchar *id_str, PluginLoaderGetTypeCallback callback,
                                gpointer callback_data)
{
	PluginLoaderTypeInfo *loader_type_info;

	g_return_if_fail (id_str != NULL);
	g_return_if_fail (callback != NULL);
 
	loader_type_info = g_new (PluginLoaderTypeInfo, 1);
	loader_type_info->id_str = g_strdup (id_str);
	loader_type_info->has_type = FALSE;
	loader_type_info->loader_type = (GtkType) 0;
	loader_type_info->get_type_callback = callback;
	loader_type_info->get_type_callback_data = callback_data;
	registered_loader_types = g_list_append (registered_loader_types, loader_type_info);
}

GtkType
plugin_loader_get_by_id (const gchar *id_str, ErrorInfo **ret_error)
{
	GList *l;

	g_return_val_if_fail (id_str != NULL, (GtkType) 0);
	g_return_val_if_fail (ret_error != NULL, (GtkType) 0);

	*ret_error = NULL;
	for (l = registered_loader_types; l != NULL; l = l->next) {
		PluginLoaderTypeInfo *loader_type_info;

		loader_type_info = (PluginLoaderTypeInfo *) l->data;
		if (strcmp (loader_type_info->id_str, id_str) == 0) {
			if (loader_type_info->has_type) {
				return loader_type_info->loader_type;
			} else {
				ErrorInfo *error;
				GtkType loader_type;

				loader_type = loader_type_info->get_type_callback (
				              loader_type_info->get_type_callback_data,
				              &error);
				if (error == NULL) {
					loader_type_info->has_type = TRUE;
					loader_type_info->loader_type = loader_type;
					return loader_type_info->loader_type;
				} else {
					*ret_error = error_info_new_printf (
					             _("Error while preparing loader \"%s\"."),
					             id_str);
					error_info_add_details (*ret_error, error);
					return (GtkType) 0;
				}
			}
		}
	}

	*ret_error = error_info_new_printf (
	             _("Unsupported loader type \"%s\"."),
	             id_str);
	return (GtkType) 0;
}

gboolean
plugin_loader_is_available_by_id (const gchar *id_str)
{
	GList *l;

	g_return_val_if_fail (id_str != NULL, FALSE);

	for (l = registered_loader_types; l != NULL; l = l->next) {
		PluginLoaderTypeInfo *loader_type_info;

		loader_type_info = (PluginLoaderTypeInfo *) l->data;
		if (strcmp (loader_type_info->id_str, id_str) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

static GList *
plugin_info_read_service_list (xmlNode *tree, ErrorInfo **ret_error)
{
	GList *service_list = NULL;
	GList *error_list = NULL;
	xmlNode *node;
	gint i;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (strcmp (tree->name, "services") == 0, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	for (i = 0, node = tree->xmlChildrenNode; node != NULL; i++, node = node->next) {
		if (strcmp (node->name, "service") == 0) {
			PluginService *service;
			ErrorInfo *service_error;

			service = plugin_service_read (node, &service_error);
			
			if (service != NULL) {
				g_assert (service_error == NULL);
				service_list = g_list_prepend (service_list, service);
			} else {
				ErrorInfo *error;

				error = error_info_new_printf (
				        _("Error while reading service #%d info."),
				        i);
				error_info_add_details (error, service_error);
				error_list = g_list_prepend (error_list, error);
			}
		}
	}
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
		g_list_free_custom (service_list, (GFreeFunc) plugin_service_free);
		return NULL;
	} else {
		return g_list_reverse (service_list);
	}
}

static PluginLoaderStaticInfo *
plugin_info_read_loader_static_info (xmlNode *tree, ErrorInfo **ret_error)
{
	PluginLoaderStaticInfo *loader_info = NULL;
	gchar *loader_type_str;
	xmlNode *node;

	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (strcmp (tree->name, "loader") == 0, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	loader_type_str = e_xml_get_string_prop_by_name (tree, "type");
	if (loader_type_str != NULL) {
		loader_info = g_new (PluginLoaderStaticInfo, 1);
		loader_info->loader_type_str = loader_type_str;
		loader_info->attr_names = NULL;
		loader_info->attr_values = NULL;
		for (node = tree->xmlChildrenNode; node != NULL; node = node->next) {
			if (strcmp (node->name, "attribute") == 0) {
				gchar *name;

				name = e_xml_get_string_prop_by_name (node, "name");
				if (name != NULL) {
					gchar *value;

					value = e_xml_get_string_prop_by_name (node, "value");
					loader_info->attr_names = g_list_prepend (loader_info->attr_names, name);
					loader_info->attr_values = g_list_prepend (loader_info->attr_values, value);
				}
			}
		}
		loader_info->attr_names = g_list_reverse (loader_info->attr_names);
		loader_info->attr_values = g_list_reverse (loader_info->attr_values);
	} else {
		*ret_error = error_info_new_str (_("Unspecified loader type."));
	}

	return loader_info;
}

static void
loader_static_info_free (PluginLoaderStaticInfo *loader_info)
{
	g_return_if_fail (loader_info != NULL);

	g_free (loader_info->loader_type_str);
	e_free_string_list (loader_info->attr_names);
	e_free_string_list (loader_info->attr_values);
	g_free (loader_info);
}

PluginInfo *
plugin_info_read (const gchar *dir_name, xmlNode *tree, ErrorInfo **ret_error)
{
	PluginInfo *pinfo = NULL;
	gchar *id, *name, *description;
	xmlNode *information_node, *loader_node, *services_node;
	ErrorInfo *services_error, *loader_error;
	GList *service_list;
	PluginLoaderStaticInfo *loader_static_info;

	*ret_error = NULL;

	g_return_val_if_fail (dir_name != NULL, NULL);
	g_return_val_if_fail (tree != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);
	g_return_val_if_fail (strcmp (tree->name, "plugin") == 0, NULL);

	id = e_xml_get_string_prop_by_name (tree, "id");
	information_node = e_xml_get_child_by_name_by_lang_list (tree, "information", NULL);
	if (information_node != NULL) {
		name = e_xml_get_string_prop_by_name (information_node, "name");
		description = e_xml_get_string_prop_by_name (information_node, "description");
	} else {
		name = NULL;
		description = NULL;
	}
	loader_node = e_xml_get_child_by_name (tree, "loader");
	if (loader_node != NULL) {
		loader_static_info = plugin_info_read_loader_static_info (loader_node, &loader_error);
	} else {
		loader_static_info = NULL;
		loader_error = NULL;
	}
	services_node = e_xml_get_child_by_name (tree, "services");
	if (services_node != NULL) {
		service_list = plugin_info_read_service_list (services_node, &services_error);
	} else {
		service_list = NULL;
		services_error = NULL;
	}
	if (id != NULL && name != NULL && loader_static_info != NULL && service_list != NULL) {
		GList *l;

		g_assert (loader_error == NULL);
		g_assert (services_error == NULL);

		pinfo = g_new0 (PluginInfo, 1);
		pinfo->dir_name = g_strdup (dir_name);
		pinfo->id = id;
		pinfo->name = name;
		pinfo->description = description;
		pinfo->is_active = FALSE;
		pinfo->loader_static_info = loader_static_info;
		pinfo->loader = NULL;
		pinfo->service_list = service_list;
		for (l = pinfo->service_list; l != NULL; l = l->next) {
			PluginService *service;

			service = (PluginService *) l->data;
			plugin_service_set_plugin (service, pinfo);
		}
		pinfo->services_data = plugin_services_data_new ();
	} else {
		if (id == NULL) {
			*ret_error = error_info_new_str (_("Plugin has no id."));
			error_info_free (loader_error);
			error_info_free (services_error);
		} else {
			GList *error_list = NULL;
			ErrorInfo *error;

			if (name == NULL) {
				error_list = g_list_prepend (error_list, error_info_new_str (
				             _("Unknown plugin name.")));
			}
			if (loader_static_info == NULL) {
				if (loader_error != NULL) {
					error = error_info_new_printf (
					        _("Errors while reading loader info for plugin with id=\"%s\"."),
					        id);
					error_info_add_details (error, loader_error);
				} else {
					error = error_info_new_printf (
					        _("No loader defined for plugin with id=\"%s\"."),
					        id);
				}
				error_list = g_list_prepend (error_list, error);
			}
			if (service_list == NULL) {
				if (services_error != NULL) {
					error = error_info_new_printf (
					        _("Errors while reading services for plugin with id=\"%s\"."),
					        id);
					error_info_add_details (error, services_error);
				} else {
					error = error_info_new_printf (
					        _("No services defined for plugin with id=\"%s\"."),
					        id);
				}
				error_list = g_list_prepend (error_list, error);
			}
			g_assert (error_list != NULL);
			error_list = g_list_reverse (error_list);
			*ret_error = error_info_new_from_error_list (error_list);
		}

		if (loader_static_info != NULL) {
			loader_static_info_free (loader_static_info);
		}
		g_list_free_custom (service_list, (GFreeFunc) plugin_service_free);
		g_free (id);
		g_free (name);
		g_free (description);
	}

	return pinfo;
}

static void
plugin_get_loader_if_needed (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	PluginLoaderStaticInfo *loader_info;
	GtkType loader_type;
	ErrorInfo *error;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (pinfo->loader != NULL) {
		return;
	}
	loader_info = pinfo->loader_static_info;
	loader_type = plugin_loader_get_by_id (loader_info->loader_type_str, &error);
	if (error == NULL) {
		GnumericPluginLoader *loader;
		ErrorInfo *error;

		loader = GNUMERIC_PLUGIN_LOADER (gtk_type_new (loader_type));
		gnumeric_plugin_loader_set_attributes (loader, loader_info->attr_names, loader_info->attr_values, &error);
		if (error == NULL) {
			pinfo->loader = loader;
			gnumeric_plugin_loader_set_plugin (loader, pinfo);
		} else {
			gtk_object_destroy (GTK_OBJECT (loader));
			loader = NULL;
			*ret_error = error_info_new_printf (
			             _("Error initializing plugin loader (\"%s\")."),
			             loader_info->loader_type_str);
			error_info_add_details (*ret_error, error);
		}
	} else {
		*ret_error = error;
	}
}

void
activate_plugin (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	GList *error_list = NULL;
	GList *l;
	gint i;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (pinfo->is_active) {
		return;
	}
	for (i = 0, l = pinfo->service_list; l != NULL; i++, l = l->next) {
		PluginService *service;
		ErrorInfo *service_error;

		service = (PluginService *) l->data;
		plugin_service_activate (service, &service_error);
		if (service_error != NULL) {
			ErrorInfo *error;

			error = error_info_new_printf (
			        _("Error while activating plugin service #%d."),
			        i);
			error_info_add_details (error, service_error);
			error_list = g_list_prepend (error_list, error);
		}
	}
	if (error_list != NULL) {
		*ret_error = error_info_new_from_error_list (error_list);
		/* FIXME - deactivate activated services */
	} else {
		pinfo->is_active = TRUE;
	}
}

void
deactivate_plugin (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	GList *error_list = NULL;
	GList *l;
	gint i;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	if (!pinfo->is_active) {
		return;
	}
	for (i = 0, l = pinfo->service_list; l != NULL; i++, l = l->next) {
		PluginService *service;
		ErrorInfo *service_error;

		service = (PluginService *) l->data;
		plugin_service_deactivate (service, &service_error);
		if (service_error != NULL) {
			ErrorInfo *error;

			error = error_info_new_printf (
			        _("Error while deactivating plugin service #%d."),
			        i);
			error_info_add_details (error, service_error);
			error_list = g_list_prepend (error_list, error);
		}
	}
	if (error_list != NULL) {
		*ret_error = error_info_new_from_error_list (error_list);
		/* FIXME - some services are still active (or broken) */
	} else {
		pinfo->is_active = FALSE;
	}
}

gboolean
plugin_can_deactivate (PluginInfo *pinfo)
{
	GList *l;

	g_return_val_if_fail (pinfo != NULL, FALSE);
	g_return_val_if_fail (pinfo->is_active, FALSE);

	for (l = pinfo->service_list; l != NULL; l = l->next) {
		PluginService *service;

		service = (PluginService *) l->data;
		if (!plugin_service_can_deactivate (service)) {
			return FALSE;
		}
	}

	return TRUE;
}

static void
plugin_info_force_mark_inactive (PluginInfo *pinfo)
{
	g_return_if_fail (pinfo != NULL);
	
	pinfo->is_active = FALSE;
}

void
plugin_load_service (PluginInfo *pinfo, PluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	plugin_get_loader_if_needed (pinfo, &error);
	if (error == NULL) {
		gnumeric_plugin_loader_load_service (pinfo->loader, service, &error);
		*ret_error = error;
	} else {
		*ret_error = error_info_new_str_with_details (
		             _("Cannot load plugin loader."),
		             error);
	}
}

void
plugin_unload_service (PluginInfo *pinfo, PluginService *service, ErrorInfo **ret_error)
{
	ErrorInfo *error;

	g_return_if_fail (pinfo != NULL);
	g_return_if_fail (pinfo->loader != NULL);
	g_return_if_fail (service != NULL);
	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	gnumeric_plugin_loader_unload_service (pinfo->loader, service, &error);
	*ret_error = error;
}

void
plugin_info_free (PluginInfo *pinfo)
{
	if (pinfo == NULL) {
		return;
	}
	g_free (pinfo->id);
	g_free (pinfo->name);
	g_free (pinfo->dir_name);
	g_free (pinfo->description);
	if (pinfo->loader_static_info != NULL) {
		loader_static_info_free (pinfo->loader_static_info);
	}
	if (pinfo->loader != NULL) {
		gtk_object_destroy (GTK_OBJECT (pinfo->loader));
	}
	g_list_free_custom (pinfo->service_list, (GFreeFunc) plugin_service_free);
	plugin_services_data_free (pinfo->services_data);

	g_free (pinfo);
}

/* 
 * May return partial list and some error info.
 */
GList *
plugin_info_list_read_for_dir (const gchar *dir_name, ErrorInfo **ret_error)
{
	GList *plugin_info_list = NULL;
	gchar *file_name;
	xmlDocPtr doc;

	g_return_val_if_fail (dir_name != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	file_name = g_concat_dir_and_file (dir_name, PLUGIN_INFO_FILE_NAME);
	doc = xmlParseFile (file_name);
	if (doc != NULL && doc->xmlRootNode != NULL
	    && strcmp (doc->xmlRootNode->name, "gnumeric_plugin_group") == 0) {
		gint i;
		xmlNode *node;
		GList *plugin_error_list = NULL;

		for (i = 0, node = doc->xmlRootNode->xmlChildrenNode;
		     node != NULL;
		     i++, node = node->next) {
			if (strcmp (node->name, "plugin") == 0) {
				PluginInfo *pinfo;
				ErrorInfo *error;

				pinfo = plugin_info_read (dir_name, node, &error);
				if (pinfo != NULL) {
					g_assert (error == NULL);
					plugin_info_list = g_list_prepend (plugin_info_list, pinfo);
				} else {
					ErrorInfo *new_error;

					new_error = error_info_new_printf (
					            _("Can't read plugin #%d info."),
					            i + 1);
					error_info_add_details (new_error, error);
					plugin_error_list = g_list_prepend (plugin_error_list, new_error);
				}
			}
		}
		if (plugin_error_list != NULL) {
			plugin_error_list = g_list_reverse (plugin_error_list);
			*ret_error = error_info_new_printf (
			             _("Errors occured while reading plugin informations from file \"%s\"."),
			             file_name);
			error_info_add_details_list (*ret_error, plugin_error_list);
		}
	} else {
		if (!g_file_exists (file_name)) {
			/* no error */
		} else if (access (file_name, R_OK) != 0) {
			*ret_error = error_info_new_printf (
			             _("Can't read plugin group info file (\"%s\")."),
			             file_name);
		} else {
			*ret_error = error_info_new_printf (
			             _("File \"%s\" is not valid plugin group info file."),
			             file_name);
		}
	}
	g_free (file_name);
	xmlFreeDoc (doc);

	return g_list_reverse (plugin_info_list);
}

/* 
 * May return partial list and some error info.
 */
GList *
plugin_info_list_read_for_subdirs_of_dir (const gchar *dir_name, ErrorInfo **ret_error)
{
	GList *plugin_info_list = NULL;
	DIR *dir;
	struct dirent *entry;
	GList *error_list = NULL;

	g_return_val_if_fail (dir_name != NULL, NULL);
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	dir = opendir (dir_name);
	if (dir == NULL) {
		return NULL;
	}

	while ((entry = readdir (dir)) != NULL) {
		gchar *full_entry_name;

		if (strcmp (entry->d_name, ".") == 0 || strcmp (entry->d_name, "..") == 0) {
			continue;
		}
		full_entry_name = g_concat_dir_and_file (dir_name, entry->d_name);
		if (g_file_test (full_entry_name, G_FILE_TEST_ISDIR)) {
			ErrorInfo *error;
			GList *subdir_plugin_info_list;

			subdir_plugin_info_list = plugin_info_list_read_for_dir (full_entry_name, &error);
			plugin_info_list = g_list_concat (plugin_info_list, subdir_plugin_info_list);
			if (error != NULL) {
				error_list = g_list_prepend (error_list, error);
			}
		}
		g_free (full_entry_name);
	}
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}

	closedir (dir);

	return plugin_info_list;
}

/* 
 * May return partial list and some error info.
 */
GList *
plugin_info_list_read_for_subdirs_of_dir_list (GList *dir_list, ErrorInfo **ret_error)
{
	GList *plugin_info_list = NULL;
	GList *dir_iterator;
	GList *error_list = NULL;

	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	for (dir_iterator = dir_list; dir_iterator != NULL; dir_iterator = dir_iterator->next) {
		gchar *dir_name;
		ErrorInfo *error;
		GList *dir_plugin_info_list;

		dir_name = (gchar *) dir_iterator->data;
		dir_plugin_info_list = plugin_info_list_read_for_subdirs_of_dir (dir_name, &error);
		if (error != NULL) {
			error_list = g_list_prepend (error_list, error);
		}
		if (dir_plugin_info_list != NULL) {
			plugin_info_list = g_list_concat (plugin_info_list, dir_plugin_info_list);
		}
	}
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}

	return plugin_info_list;
}

GList *
gnumeric_extra_plugin_dirs (void)
{
	static GList *extra_dirs;
	static gboolean list_ready = FALSE;

	if (!list_ready) {
		gchar *plugin_path_env;

		extra_dirs = gnumeric_config_get_string_list ("Gnumeric/Plugin/",
		                                              "ExtraPluginsDir");
		plugin_path_env = g_getenv ("GNUMERIC_PLUGIN_PATH");
		if (plugin_path_env != NULL) {
			extra_dirs = g_list_concat (extra_dirs,
			                            g_strsplit_to_list (plugin_path_env, ":"));
		}
		list_ready = TRUE;
	}

	return g_string_list_copy (extra_dirs);
}

/* 
 * May return partial list and some error info.
 */
GList *
plugin_info_list_read_for_all_dirs (ErrorInfo **ret_error)
{
	GList *dir_list;
	GList *plugin_info_list;
	ErrorInfo *error;

	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	dir_list = g_create_list (gnumeric_sys_plugin_dir (),
	                          gnumeric_usr_plugin_dir (),
	                          NULL);
	dir_list = g_list_concat (dir_list, gnumeric_extra_plugin_dirs ());
	plugin_info_list = plugin_info_list_read_for_subdirs_of_dir_list (dir_list, &error);
	e_free_string_list (dir_list);
	*ret_error = error;

	return plugin_info_list;
}

/*
 * Currently it just moves plugins containing loader services to the top.
 * Returns "shallow copy' of the list.
 */
static GList *
plugin_list_sort_by_dependency (GList *plugin_list)
{
	GList *loaders_list = NULL, *others_list = NULL, *l;

	for (l = plugin_list; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = (PluginInfo *) l->data;
		if (plugin_info_provides_loader_by_type_str (pinfo, NULL)) {
			loaders_list = g_list_prepend (loaders_list, pinfo);
		} else {
			others_list = g_list_prepend (others_list, pinfo);
		}
	}
	loaders_list = g_list_reverse (loaders_list);
	others_list = g_list_reverse (others_list);

	return g_list_concat (loaders_list, others_list);
}

/* 
 * May activate some plugins and return error info for the rest.
 * Doesn't report errors for plugins with unknown loader types
 * (plugin_loader_is_available_by_id() returns FALSE).
 */
void
plugin_db_activate_plugin_list (GList *plugins, ErrorInfo **ret_error)
{
	GList *sorted_plugins, *l;
	GList *error_list = NULL;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	sorted_plugins = plugin_list_sort_by_dependency (plugins);
	for (l = sorted_plugins; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = (PluginInfo *) l->data;
		if (!pinfo->is_active &&
		    plugin_loader_is_available_by_id (plugin_info_peek_loader_type_str (pinfo))) {
			ErrorInfo *error;

			activate_plugin ((PluginInfo *) l->data, &error);
			if (error != NULL) {
				ErrorInfo *new_error;

				new_error = error_info_new_printf (
				            _("Couldn't activate plugin \"%s\" (ID: %s)."),
				            pinfo->name,
				            pinfo->id);
				error_info_add_details (new_error, error);
				error_list = g_list_prepend (error_list, new_error);
			}
		}
	}
	g_list_free (sorted_plugins);
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}
}

/* 
 * May deactivate some plugins and return error info for the rest.
 * Doesn't report errors for plugins that are currently in use
 * (plugin_can_deactivate() returns FALSE).
 */
void
plugin_db_deactivate_plugin_list (GList *plugins, ErrorInfo **ret_error)
{
	GList *l;
	GList *error_list = NULL;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	for (l = plugins; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = (PluginInfo *) l->data;
		if (pinfo->is_active && plugin_can_deactivate (pinfo)) {
			ErrorInfo *error;

			deactivate_plugin (pinfo, &error);
			if (error != NULL) {
				ErrorInfo *new_error;

				new_error = error_info_new_printf (
				            _("Couldn't deactivate plugin \"%s\" (ID: %s)."),
				            pinfo->name,
				            pinfo->id);
				error_info_add_details (new_error, error);
				error_list = g_list_prepend (error_list, new_error);
			}
		}
	}
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		*ret_error = error_info_new_from_error_list (error_list);
	}
}

GList *
plugin_db_get_known_plugin_id_list (void)
{
	if (!known_plugin_id_list_is_ready) {
		if (known_plugin_id_list != NULL) {
			e_free_string_list (known_plugin_id_list);
		}
		known_plugin_id_list = gnumeric_config_get_string_list (
		                       "Gnumeric/Plugin/KnownPlugins",
		                       NULL);
		known_plugin_id_list_is_ready = TRUE; 
	}

	return known_plugin_id_list;
}

void
plugin_db_extend_known_plugin_id_list (GList *extra_ids)
{
	GList *old_ids;

	old_ids = plugin_db_get_known_plugin_id_list ();
	known_plugin_id_list = g_list_concat (old_ids, extra_ids);
	gnumeric_config_set_string_list (known_plugin_id_list,
	                                 "Gnumeric/Plugin/KnownPlugins",
	                                 NULL);
}

gboolean
plugin_db_is_known_plugin (const gchar *plugin_id)
{
	GList *l;

	g_return_val_if_fail (plugin_id != NULL, FALSE);

	for (l = plugin_db_get_known_plugin_id_list(); l != NULL; l = l->next) {
		if (strcmp ((gchar *) l->data, plugin_id) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

GList *
plugin_db_get_available_plugin_info_list (ErrorInfo **ret_error)
{
	g_return_val_if_fail (ret_error != NULL, NULL);

	*ret_error = NULL;
	if (!available_plugin_info_list_is_ready) {
		ErrorInfo *error;

		if (available_plugin_info_list != NULL) {
			GList *l;

			for (l = available_plugin_info_list; l != NULL; l = l->next) {
				plugin_info_free ((PluginInfo *) l->data);
			}
		}
		available_plugin_info_list = plugin_info_list_read_for_all_dirs (&error);
		if (error != NULL) {
			*ret_error = error;
		}
		available_plugin_info_list_is_ready	= TRUE;
	}

	return available_plugin_info_list;
}

PluginInfo *
plugin_db_get_plugin_info_by_plugin_id (const gchar *plugin_id)
{
	ErrorInfo *error;
	GList *plugin_info_list, *l;

	g_return_val_if_fail (plugin_id != NULL, NULL);

	plugin_info_list = plugin_db_get_available_plugin_info_list (&error);
	error_info_free (error);
	for (l = plugin_info_list; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = (PluginInfo *) l->data;
		if (strcmp (pinfo->id, plugin_id) == 0) {
			return pinfo;
		}
	}

	return NULL;
}

GList *
plugin_db_get_saved_active_plugin_id_list (void)
{
	if (!saved_active_plugin_id_list_is_ready) {
		if (saved_active_plugin_id_list != NULL) {
			e_free_string_list (saved_active_plugin_id_list);
		}
		saved_active_plugin_id_list = gnumeric_config_get_string_list (
		                              "Gnumeric/Plugin/ActivePlugins",
		                              NULL);
		saved_active_plugin_id_list_is_ready = TRUE;
	}

	return saved_active_plugin_id_list;
}

void
plugin_db_update_saved_active_plugin_id_list (void)
{
	GList *plugin_list, *l;
	ErrorInfo *error;

	if (saved_active_plugin_id_list != NULL) {
		e_free_string_list (saved_active_plugin_id_list);
	}
	saved_active_plugin_id_list = NULL;
	plugin_list = plugin_db_get_available_plugin_info_list (&error);
	error_info_free (error);
	for (l = plugin_list; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = (PluginInfo *) l->data;
		if (pinfo->is_active) {
			saved_active_plugin_id_list = g_list_prepend (saved_active_plugin_id_list,
			                                              g_strdup (pinfo->id));
		}
	}
	saved_active_plugin_id_list = g_list_reverse (saved_active_plugin_id_list);
	gnumeric_config_set_string_list (saved_active_plugin_id_list,
	                                 "Gnumeric/Plugin/ActivePlugins",
	                                 NULL);
	saved_active_plugin_id_list_is_ready = TRUE;
}

void
plugin_db_extend_saved_active_plugin_id_list (GList *extra_ids)
{
	GList *old_ids;
 
	old_ids = plugin_db_get_saved_active_plugin_id_list ();
	saved_active_plugin_id_list = g_list_concat (old_ids, extra_ids);
	gnumeric_config_set_string_list (saved_active_plugin_id_list,
	                                 "Gnumeric/Plugin/ActivePlugins",
	                                 NULL);
}

gboolean
plugin_db_is_saved_active_plugin (const gchar *plugin_id)
{
	GList *l;

	g_return_val_if_fail (plugin_id != NULL, FALSE);

	for (l = plugin_db_get_saved_active_plugin_id_list (); l != NULL; l = l->next) {
		if (strcmp ((gchar *) l->data, plugin_id) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

void
plugin_db_mark_plugin_for_deactivation (PluginInfo *pinfo, gboolean mark)
{
	plugins_marked_for_deactivation = g_list_remove (plugins_marked_for_deactivation, pinfo);   
	if (mark) {
		plugins_marked_for_deactivation = g_list_prepend (plugins_marked_for_deactivation, pinfo);
	}
}

gboolean
plugin_db_is_plugin_marked_for_deactivation (PluginInfo *pinfo)
{
	return g_list_find (plugins_marked_for_deactivation, pinfo) != NULL;
}

/* 
 * May return errors for some plugins.
 */
void
plugin_db_init (ErrorInfo **ret_error)
{
	ErrorInfo *error;
	GList *known_plugin_ids, *new_plugin_ids;
	GList *plugin_list, *l;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	plugin_list = plugin_db_get_available_plugin_info_list (&error);
	if (error != NULL) {
		*ret_error = error;
	}
	known_plugin_ids = plugin_db_get_known_plugin_id_list ();
	new_plugin_ids = NULL;
	for (l = plugin_list; l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = (PluginInfo *) l->data;
		if (g_list_find_custom (known_plugin_ids, pinfo->id, &g_str_compare) == NULL) {
			new_plugin_ids = g_list_prepend (new_plugin_ids, g_strdup (pinfo->id));
		}
	}

	if (gnome_config_get_bool_with_default ("Gnumeric/Plugin/ActivateNewByDefault", FALSE)) {
		plugin_db_extend_saved_active_plugin_id_list (g_string_list_copy (new_plugin_ids));
	}

	plugin_db_extend_known_plugin_id_list (new_plugin_ids);
}

/* 
 * May return errors for some plugins.
 */
void
plugin_db_shutdown (ErrorInfo **ret_error)
{
	GList *plugin_list;
	ErrorInfo *error;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	plugin_list = plugin_db_get_available_plugin_info_list (&error);
	error_info_free (error);
	plugin_db_deactivate_plugin_list (plugin_list, &error);
	*ret_error = error;
}

void
plugin_db_activate_saved_active_plugins (ErrorInfo **ret_error)
{
	GList *plugin_list = NULL, *l;
	ErrorInfo *error;

	g_return_if_fail (ret_error != NULL);

	*ret_error = NULL;
	for (l = plugin_db_get_saved_active_plugin_id_list (); l != NULL; l = l->next) {
		PluginInfo *pinfo;

		pinfo = plugin_db_get_plugin_info_by_plugin_id ((gchar *) l->data);
		if (pinfo != NULL) {
			plugin_list = g_list_prepend (plugin_list, pinfo);
		}
	}
	plugin_list = g_list_reverse (plugin_list);
	plugin_db_activate_plugin_list (plugin_list, &error);
	*ret_error = error;
	g_list_free (plugin_list);

	plugin_db_update_saved_active_plugin_id_list ();
}

void
plugins_init (CommandContext *context)
{
	GList *error_list = NULL;
	ErrorInfo *error;

#ifdef PLUGIN_DEBUG
	gnumeric_time_counter_push ();
#endif
	plugin_loader_register_type ("g_module", TYPE_GNUMERIC_PLUGIN_LOADER_MODULE);
	plugin_db_init (&error);
	if (error != NULL) {
		error_list = g_list_prepend (error_list, error_info_new_str_with_details (
		                             _("Errors while reading info about available plugins."),
		                             error));
	}
	plugin_db_activate_saved_active_plugins (&error);
	if (error != NULL) {
		error_list = g_list_prepend (error_list, error_info_new_str_with_details (
		                             _("Errors while activating plugins."),
		                             error));
	}
	if (error_list != NULL) {
		error_list = g_list_reverse (error_list);
		error = error_info_new_str_with_details_list (
		        _("Errors while initializing plugin system."),
		        error_list);
		gnumeric_error_info_dialog_show (WORKBOOK_CONTROL_GUI (context), error);
		error_info_free (error);
	}
#ifdef PLUGIN_DEBUG
	g_print ("plugins_init() time: %fs\n", gnumeric_time_counter_pop ());
#endif
}

void
plugins_shutdown (void)
{
	GList *l;
	ErrorInfo *ignored_error;

	for (l = plugins_marked_for_deactivation; l != NULL; l = l->next) {
		plugin_info_force_mark_inactive ((PluginInfo *) l->data);
	}
	plugin_db_update_saved_active_plugin_id_list ();
	plugin_db_shutdown (&ignored_error);
	error_info_free (ignored_error);
	gnome_config_sync ();
}
