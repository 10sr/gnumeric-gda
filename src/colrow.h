#ifndef GNUMERIC_COLROW_H
#define GNUMERIC_COLROW_H

#include "gnumeric.h"

struct _ColRowInfo {
	int	pos;		/* the column or row number */

	/* Size including margins, and right grid line */
	float	 size_pts;
	unsigned size_pixels;

	/* These are not scaled, and are the same in points and pixels */
	unsigned  margin_a	: 3;  	/* top/left margin */
	unsigned  margin_b	: 3; 	/* bottom/right margin */

	unsigned  outline_level : 4;
	unsigned  is_collapsed  : 1;	/* Does this terminate an outline ? */
	unsigned  hard_size     : 1;	/* are dimensions explicitly set ? */
	unsigned  visible       : 1;	/* Is row/col visible */

	/* TODO : Add per row/col min/max */

	void *spans;	/* Only used for rows */
};

struct _ColRowCollection
{
	int         max_used;
	ColRowInfo  default_style;
	GPtrArray * info;

	float	    size_pts;
	int	    size_pixels;
};

/* The size, mask, and shift must be kept in sync */
#define COLROW_SEGMENT_SIZE	0x80
#define COLROW_SUB_INDEX(i)	((i) & 0x7f)
#define COLROW_SEGMENT_START(i)	((i) & ~(0x7f))
#define COLROW_SEGMENT_END(i)	((i) | 0x7f)
#define COLROW_SEGMENT_INDEX(i)	((i) >> 7)
#define COLROW_GET_SEGMENT(seg_array, i) \
	(g_ptr_array_index ((seg_array)->info, COLROW_SEGMENT_INDEX(i)))

struct _ColRowSegment
{
	ColRowInfo *info [COLROW_SEGMENT_SIZE];
	float	size_pts;
	int	size_pixels;
};

#define COL_INTERNAL_WIDTH(col) ((col)->size_pixels - ((col)->margin_b + (col)->margin_a + 1))

gboolean colrow_equal	(ColRowInfo const *a, ColRowInfo const *b);
void     colrow_copy	(ColRowInfo *dst, ColRowInfo const *src);
gboolean colrow_foreach	(ColRowCollection const *infos,
			 int first, int last,
			 ColRowHandler callback,
			 void *user_data);

/* Support for Col/Row resizing */
ColRowSizeList	*colrow_size_list_destroy	(ColRowSizeList *list);
ColRowIndexList *colrow_index_list_destroy	(ColRowIndexList *list);
ColRowIndexList *colrow_get_index_list		(int first, int last,
						 ColRowIndexList *list);
double		*colrow_save_sizes		(Sheet *sheet, gboolean const is_cols,
						 int first, int last);
ColRowSizeList	*colrow_set_sizes		(Sheet *sheet, gboolean const is_cols,
						 ColRowIndexList *src, int new_size);
void		 colrow_restore_sizes		(Sheet *sheet, gboolean const is_cols,
						 int first, int last, double *);
void		 colrow_restore_sizes_group	(Sheet *sheet, gboolean const is_cols,
						 ColRowIndexList *selection,
						 ColRowSizeList *saved_sizes,
						 int old_size);

/* Support for Col/Row visibility */
void		 colrow_set_visibility		(Sheet *sheet, gboolean const is_col,
						 gboolean const visible,
						 int first, int last);

ColRowVisList	*colrow_get_visiblity_toggle	(Sheet *sheet, gboolean const is_col,
						 gboolean const visible);
ColRowVisList	*colrow_vis_list_destroy	(ColRowVisList *list);
void		 colrow_set_visibility_list	(Sheet *sheet, gboolean const is_col,
						 gboolean const visible,
						 ColRowVisList *list);

/* Misc */
int              colrow_find_adjacent_visible   (Sheet *sheet, gboolean const is_col,
						 int const index);
void             rows_height_update	(Sheet *sheet, Range const *range);

#endif /* GNUMERIC_COLROW_H */
