/*
 * GMAMEUI
 *
 * Copyright 2009 Andrew Burton <adb@iinet.net.au>
 * based on GXMame code
 * 2002-2005 Stephane Pontier <shadow_walker@users.sourceforge.net>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __GMAMEUI_SIDEBAR_H__
#define __GMAMEUI_SIDEBAR_H__

#include <gtk/gtk.h>
//#include "common.h"
#include "rom_entry.h"
#include "unzip.h"

/**** Sidebar functionality ****/
#define GMAMEUI_TYPE_SIDEBAR        (gmameui_sidebar_get_type ())
#define GMAMEUI_SIDEBAR(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GMAMEUI_TYPE_SIDEBAR, GMAMEUISidebar))
#define GMAMEUI_SIDEBAR_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), GMAMEUI_TYPE_SIDEBAR, GMAMEUISidebarClass))
#define GMAMEUI_IS_SIDEBAR(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GMAMEUI_TYPE_SIDEBAR))
#define GMAMEUI_IS_SIDEBAR_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GMAMEUI_TYPE_SIDEBAR))

typedef struct _GMAMEUISidebar        GMAMEUISidebar;
typedef struct _GMAMEUISidebarClass   GMAMEUISidebarClass;
typedef struct _GMAMEUISidebarPrivate GMAMEUISidebarPrivate;

/*
 * Main object structure
 */

struct _GMAMEUISidebar 
{
	GtkFrame parent; /* Container for all the nested widgets */

	/*< private > */
	GMAMEUISidebarPrivate *priv;
};

struct _GMAMEUISidebarClass {
	GtkFrameClass parent_class;
};

GType gmameui_sidebar_get_type (void);

GtkWidget *gmameui_sidebar_new (void);

/*DELETEstatic void
gmameui_sidebar_set_page (GMAMEUISidebar *sidebar, RomEntry *rom, screenshot_type page);*/
/*DELETE
*/

gboolean gmameui_sidebar_set_history (GMAMEUISidebar *sidebar, RomEntry *rom);
void gmameui_sidebar_set_with_rom (GMAMEUISidebar *sidebar, RomEntry *rom);
void gmameui_sidebar_set_current_page (GMAMEUISidebar *sidebar, int page);

GdkPixbuf *
get_pixbuf_from_zip_file (ZIP *zip, gchar *romname, gchar *parent_romname);

/**** End Sidebar functionality ****/

#endif /* __GMAMEUI_SIDEBAR_H__ */
