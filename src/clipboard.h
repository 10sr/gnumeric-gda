#ifndef GNUMERIC_CLIPBOARD_H
#define GNUMERIC_CLIPBOARD_H

#include "gnumeric.h"
#include <pango/pango-context.h>

enum {
	PASTE_CONTENTS		= 1 << 0, /* either CONTENTS or AS_VALUES */
	PASTE_AS_VALUES		= 1 << 1, /*  can be applied, not both */
	PASTE_FORMATS		= 1 << 2,
	PASTE_COMMENTS		= 1 << 3,
	PASTE_OBJECTS		= 1 << 4,

	/* Operations that can be performed at paste time on a cell */
	PASTE_OPER_ADD		= 1 << 5,
	PASTE_OPER_SUB		= 1 << 6,
	PASTE_OPER_MULT		= 1 << 7,
	PASTE_OPER_DIV		= 1 << 8,

	/* Whether the paste transposes or not */
	PASTE_TRANSPOSE		= 1 << 9,

	PASTE_LINK              = 1 << 10,

	/* If copying a range that includes blank cells, this
	   prevents pasting blank cells over existing data */
	PASTE_SKIP_BLANKS       = 1 << 11,

	/* Do not paste merged regions (probably not needed) */
	PASTE_DONT_MERGE        = 1 << 12,

	/* Do not clear comments */
	PASTE_IGNORE_COMMENTS   = 1 << 13,

	/* Update the row height when pasting? (for large fonts, etc.) */
	PASTE_UPDATE_ROW_HEIGHT = 1 << 14,

	PASTE_EXPR_LOCAL_RELOCATE = 1 << 15,

	/* Avoid flagging dependencies.  */
	PASTE_NO_RECALC         = 1 << 16
};

#define PASTE_ALL_TYPES (PASTE_CONTENTS | PASTE_FORMATS | PASTE_COMMENTS | PASTE_OBJECTS)
#define PASTE_DEFAULT   PASTE_ALL_TYPES
#define PASTE_OPER_MASK (PASTE_OPER_ADD | PASTE_OPER_SUB | PASTE_OPER_MULT | PASTE_OPER_DIV)

typedef struct {
	int col_offset, row_offset; /* Position of the cell */
	GnmValue *val;
	GnmExprTop const *texpr;
} GnmCellCopy;

struct _GnmCellRegion {
	Sheet		*origin_sheet; /* can be NULL */
	GnmCellPos	 base;
	int		 cols, rows;
	GSList		*contents;
	GnmStyleList	*styles;
	GSList		*merged;
	GSList		*objects;
	gboolean	 not_as_contents;
	unsigned	 ref_count;
};

struct _GnmPasteTarget {
	Sheet      *sheet;
	GnmRange    range;
	int         paste_flags;
};

GnmCellRegion  *clipboard_copy_range   (Sheet *sheet, GnmRange const *r);
GnmCellRegion  *clipboard_copy_obj     (Sheet *sheet, GSList *objects);
gboolean        clipboard_paste_region (GnmCellRegion const *contents,
					GnmPasteTarget const *pt,
					GOCmdContext *cc);
GnmPasteTarget *paste_target_init      (GnmPasteTarget *pt,
					Sheet *sheet, GnmRange const *r,
					int flags);

GnmCellRegion *cellregion_new	(Sheet *origin_sheet);
void           cellregion_ref   (GnmCellRegion *contents);
void           cellregion_unref (GnmCellRegion *contents);
int            cellregion_cmd_size (GnmCellRegion const *contents);

GnmCellCopy *gnm_cell_copy_new	(int col_offset, int row_offset);

void clipboard_init (void);
void clipboard_shutdown (void);


#endif /* GNUMERIC_CLIPBOARD_H */
