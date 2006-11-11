#ifndef GNUMERIC_STF_EXPORT_H
#define GNUMERIC_STF_EXPORT_H

#include "gnumeric.h"
#include <gsf/gsf-output-csv.h>

#define GNM_STF_EXPORT_TYPE        (gnm_stf_export_get_type ())
#define GNM_STF_EXPORT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_STF_EXPORT_TYPE, GnmStfExport))
#define IS_GNM_STF_EXPORT(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_STF_EXPORT_TYPE))

typedef enum {
	GNM_STF_TRANSLITERATE_MODE_TRANS,  /* Automatically quote where needed */
	GNM_STF_TRANSLITERATE_MODE_ESCAPE  /* Always quote */
} GnmStfTransliterateMode;
GType gnm_stf_transliterate_mode_get_type (void);
#define GNM_STF_TRANSLITERATE_MODE_TYPE (gnm_stf_transliterate_mode_get_type ())

typedef enum {
	GNM_STF_FORMAT_AUTO,
	GNM_STF_FORMAT_RAW,
	GNM_STF_FORMAT_PRESERVE
} GnmStfFormatMode;
GType gnm_stf_format_mode_get_type (void);
#define GNM_STF_FORMAT_MODE_TYPE (gnm_stf_format_mode_get_type ())

typedef struct _GnmStfExport GnmStfExport;
GType gnm_stf_export_get_type (void);

void gnm_stf_export_options_sheet_list_clear    (GnmStfExport *export_options);
void gnm_stf_export_options_sheet_list_add      (GnmStfExport *export_options, Sheet *sheet);

gboolean gnm_stf_export_can_transliterate (void);

/*
 * Functions that do the actual thing
 */
gboolean gnm_stf_export (GnmStfExport *export_options);

#endif
