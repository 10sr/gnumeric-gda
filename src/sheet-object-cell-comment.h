#ifndef GNUMERIC_SHEET_OBJECT_CELL_COMMENT_H
#define GNUMERIC_SHEET_OBJECT_CELL_COMMENT_H

#include "sheet-object.h"

#define CELL_COMMENT_TYPE     (cell_comment_get_type ())
#define CELL_COMMENT(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), CELL_COMMENT_TYPE, GnmComment))
#define CELL_COMMENT_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), CELL_COMMENT_TYPE, CellCommentClass))
#define IS_CELL_COMMENT(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), CELL_COMMENT_TYPE))

GType	     cell_comment_get_type (void);

char const  *cell_comment_author_get (GnmComment const *cc);
void         cell_comment_author_set (GnmComment       *cc, char const *);
char const  *cell_comment_text_get   (GnmComment const *cc);
void         cell_comment_text_set   (GnmComment       *cc, char const *);

/* convenience routine */
void	     cell_comment_set_cell   (GnmComment *cc, GnmCellPos const *pos);
GnmComment *cell_set_comment (Sheet *sheet, GnmCellPos const *pos,
			       char const *author, char const *text);

#endif /* GNUMERIC_SHEET_OBJECT_CELL_COMMENT_H */
