/*
 * html.h
 *
 * Copyright (C) 1999 Rasca, Berlin
 * EMail: thron@gmx.de
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GNUMERIC_PLUGIN_HTML_H
#define GNUMERIC_PLUGIN_HTML_H

#include "gnumeric.h"
#include "file.h"
#include "error-info.h"
#include "gnumeric-util.h"

void html32_file_save (FileSaver const *fs, IOContext *io_context, WorkbookView *wb_view, const char *filename);
void html40_file_save (FileSaver const *fs, IOContext *io_context, WorkbookView *wb_view, const char *filename);
void html32_file_open (FileOpener const *fo, IOContext *io_context, WorkbookView *wb_view, const char *filename);

#define G_PLUGIN_FOR_HTML "GPFH/0.5"

#endif
