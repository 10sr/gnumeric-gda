#ifndef GNUMERIC_GLOBAL_GNOME_FONT_H
#define GNUMERIC_GLOBAL_GNOME_FONT_H

extern GList *gnumeric_font_family_list;
extern GList *gnumeric_point_size_list;
extern int const gnumeric_point_sizes [];

void global_gnome_font_init     (void);
void global_gnome_font_shutdown (void);

#endif
