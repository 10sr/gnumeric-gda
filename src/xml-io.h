/*
 * xml-io.h: interfaces to save/read gnumeric Sheets using an XML encoding.
 *
 * Daniel Veillard <Daniel.Veillard@w3.org>
 *
 * $Id$
 */

#ifndef GNUMERIC_XML_IO_H
#define GNUMERIC_XML_IO_H

#include "gnumeric.h"
#include "gnome-xml/tree.h"
#include "gnome-xml/xmlmemory.h"

typedef enum
{
    GNUM_XML_V1,
    GNUM_XML_V2,
    GNUM_XML_V3,	/* >= 0.52 */
    GNUM_XML_V4,	/* >= 0.57 */
    GNUM_XML_V5,	/* >= 0.58 */
} GnumericXMLVersion;

typedef struct _XmlParseContext XmlParseContext;

typedef gboolean     (*XmlSheetObjectWriteFn) (xmlNodePtr   cur,
					       SheetObject const *object,
					       gpointer     user_data);
typedef SheetObject *(*XmlSheetObjectReadFn)  (xmlNodePtr   tree,
					       Sheet       *sheet,
					       gpointer     user_data);

struct _XmlParseContext {
	xmlDocPtr doc;		/* Xml document */
	xmlNsPtr ns;		/* Main name space */
	xmlNodePtr parent;	/* used only for g_hash_table_foreach callbacks */
	Sheet *sheet;		/* the associated sheet */
	Workbook *wb;		/* the associated workbook */
	GHashTable *style_table;/* old style styles compatibility */
	GHashTable *expr_map;	/*
				 * Emitted expressions with ref count > 1
				 * When writing this is map from expr pointer -> index
				 * when reading this is a map from index -> expr pointer
				 */
	XmlSheetObjectWriteFn write_fn;
	XmlSheetObjectReadFn  read_fn;
	gpointer              user_data;
	GnumericXMLVersion    version;
};

int        gnumeric_xml_read_workbook   (IOContext *context,
					 WorkbookView *wb_view,
					 const char *filename);
int        gnumeric_xml_write_workbook  (IOContext *context,
					 WorkbookView *wb_view,
					 const char *filename);

XmlParseContext *xml_parse_ctx_new      (xmlDocPtr             doc,
					 xmlNsPtr              ns);
XmlParseContext *xml_parse_ctx_new_full (xmlDocPtr             doc,
					 xmlNsPtr              ns,
					 GnumericXMLVersion    version,
					 XmlSheetObjectReadFn  read_fn,
					 XmlSheetObjectWriteFn write_fn,
					 gpointer              user_data);
void             xml_parse_ctx_destroy  (XmlParseContext      *ctxt);

xmlNodePtr       xml_workbook_write     (XmlParseContext      *ctx,
					 WorkbookView         *wb_view);
gboolean         xml_workbook_read      (IOContext            *context,
					 WorkbookView	      *new_wb,
					 XmlParseContext      *ctx,
					 xmlNodePtr           tree);

xmlNsPtr         xml_check_version      (xmlDocPtr            doc,
					 GnumericXMLVersion  *version);

int        gnumeric_xml_write_selection_clipboard (WorkbookControl *context, Sheet *sheet,
						   xmlChar **buffer, int *size);
int        gnumeric_xml_read_selection_clipboard  (WorkbookControl *context, CellRegion **cr,
						   xmlChar *buffer);
/*
 * Exported support functions
 */
xmlNodePtr   xml_search_child           (xmlNodePtr node, const char *name);
xmlNodePtr   xml_search_child_lang_list (xmlNodePtr tree, const char *name, GList *lang_list);
xmlNodePtr   xml_search_child_no_lang   (xmlNodePtr tree, const char *name);

void	     xml_set_value_cstr	   (xmlNodePtr node, const char *name, const char *val);
String *     xml_get_value_string  (xmlNodePtr node, const char *name);
void         xml_set_value_string  (xmlNodePtr node, const char *name, const String *val);
gboolean     xml_get_value_int     (xmlNodePtr node, const char *name, int *val);
void         xml_set_value_int     (xmlNodePtr node, const char *name, int val);

xmlNodePtr   xml_write_style       (XmlParseContext *ctxt, MStyle *style);
MStyle      *xml_read_style        (XmlParseContext *ctxt, xmlNodePtr tree);

void      xml_init (void);

#endif /* GNUMERIC_XML_IO_H */
