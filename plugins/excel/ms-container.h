#ifndef MS_OFFICE_CONTAINER_H
#define MS_OFFICE_CONTAINER_H

#include "excel.h"
#include <gtk/gtkobject.h>

typedef struct _MSContainer MSContainer;
typedef struct _MSEscherBlip MSEscherBlip;
typedef struct _MSObj MSObj;

typedef struct
{
	gboolean    (*realize_obj) (MSContainer *container, MSObj *obj);
	GtkObject * (*create_obj)  (MSContainer *container, MSObj *obj);
	ExprTree  * (*parse_expr)  (MSContainer *container,
				    guint8 const *data, int length);
} MSContainerClass;

struct _MSContainer
{
	MSContainerClass const *vtbl;

	MsBiffVersion	 ver;
	gboolean	 free_blips;
	GPtrArray	*blips;
	GList		*obj_queue;
};

void ms_container_init (MSContainer *container, MSContainerClass const *vtbl);
void ms_container_finalize (MSContainer *container);

void                ms_container_add_blip (MSContainer *c, MSEscherBlip *blip);
MSEscherBlip const *ms_container_get_blip (MSContainer *c, int blip_id);
void	  ms_container_set_blips    (MSContainer *c, GPtrArray *blips);
void	  ms_container_add_obj	    (MSContainer *c, MSObj *obj);
void	  ms_container_realize_objs (MSContainer *c);
ExprTree *ms_container_parse_expr   (MSContainer *c,
				     guint8 const *data, int length);

#endif /* MS_OFFICE_CONTAINER_H */
