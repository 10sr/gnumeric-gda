/*
 * about.c: Shows the contributors to Gnumeric.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
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
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <workbook.h>
#include <gui-util.h>
#include <gtk/gtkhbox.h>
#include <libgnomeui/gnome-about.h>
#include <libgnomeui/gnome-href.h>

#define ABOUT_KEY          "about-dialog"

/*
 * We need to get rid of that so that we will be able
 * to list everybody.  Something like guname would be
 * nice
 */
void
dialog_about (WorkbookControlGUI *wbcg)
{
        GtkWidget *about, *hbox;
        GdkPixbuf *icon;
	static gboolean translated = FALSE;

        gchar const *authors[] = {
		N_("Miguel de Icaza, creator."),
		N_("Jody Goldberg, maintainer."),
		N_("Sean Atkinson, functions and X-Base importing."),
		N_("Michel Berkelaar, Simplex algorithm for Solver (LP Solve)."),
		N_("Grandma Chema Celorio, Tester and sheet copy."),
		N_("Frank Chiulli, OLE support."),
		N_("Kenneth Christiansen, i18n, misc stuff."),
		N_("Zbigniew Chyla, plugin system, i18n."),
		N_("J.H.M. Dassen (Ray), debian packaging."),
		N_("Jeroen Dirks, Simplex algorithm for Solver (LP Solve)."),
		N_("Tom Dyas, plugin support."),
		/* xgettext:
		 * if your charset allows it, replace the 'o' of 'Gergo'
		 * with 'odoubleacute' (U0151) and the 'E' of 'Erdi'
		 * with 'Eacute' (U00C9) */
		N_("Gergo Erdi, Gnumeric hacker."),
		N_("John Gotts, rpm packaging."),
		/* xgettext:
		 * The funny character between "G" and "l" here is a \"u.  */
		N_("Andreas J. G\xc3\xbclzow, Gnumeric hacker."),
		/* xgettext:
		 * The funny character between "K" and "r" here is 'aring'.  */
		N_("Jon K\xc3\xa5re Hellan, Gnumeric hacker."),
		N_("Ross Ihaka, special functions."),
		N_("Jukka-Pekka Iivonen, numerous functions and tools."),
		N_("Jakub Jelinek, Gnumeric hacker."),
		N_("Jakub Steiner, Icons and Images."),
		N_("Chris Lahey, number format engine."),
		N_("Takashi Matsuda, original text plugin."),
		N_("Michael Meeks, Excel and OLE2 importing."),
		N_("Lutz Muller, SheetObject improvements"),
		N_("Federico M. Quintero, canvas support."),
		N_("Mark Probst, Guile support."),
		N_("Rasca, HTML, troff, LaTeX exporters."),
		N_("Vincent Renardias, original CSV support, French localization."),
		N_("Ariel Rios, Guile support."),
		N_("Arturo Tena, OLE support."),
		N_("Almer S. Tigelaar, Gnumeric hacker."),
		N_("Bruno Unna, Excel bits."),
		N_("Daniel Veillard, XML support."),
		N_("Vladimir Vuksan, financial functions."),
		N_("Morten Welinder, Gnumeric hacker."),
		NULL
	};

        gchar const *documenters[] = {
		"Kevin Breit",
		"Thomas Canty",
		"Adrian Custer",
		"Adrian Likins",
		"Aaron Weber",
		"Alexander Kirillov",
		NULL
	};

	if (!translated) {
		int i;

		for (i = 0; authors[i] != NULL; i++)
		    authors[i] = _(authors[i]);
		translated = TRUE;
	}
	/* Ensure we only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, ABOUT_KEY))
		return;

	icon = gnumeric_load_pixbuf ("gnome-gnumeric.png");
        about = gnome_about_new (_("Gnumeric"), VERSION,
				 "Copyright \xc2\xa9 2001-2004 Jody Goldberg\n"
				 "Copyright \xc2\xa9 1998-2000 Miguel de Icaza",
				 _("A production ready spreadsheet"),
				 authors, documenters,
/* Translate the following string with the names of all translators for this locale. */
				 _("This is an untranslated version of Gnumeric."), icon);
	if (icon != NULL)
	    g_object_unref (icon);

	hbox = gtk_hbox_new (TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox),
		gnome_href_new ("http://www.gnumeric.org", _("Gnumeric Home Page")),
		FALSE, FALSE, 0);

	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (about)->vbox),
			    hbox, TRUE, FALSE, 0);
	gtk_widget_show_all (hbox);

	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (about), ABOUT_KEY);
	gtk_widget_show (GTK_WIDGET (about));
}
