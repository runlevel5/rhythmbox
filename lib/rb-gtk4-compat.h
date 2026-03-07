/* rb-gtk4-compat.h
 *
 * Compatibility shims for the GTK3 -> GTK4 transition.
 * These stubs exist only to let the code compile during the port
 * and should be removed once each subsystem is properly ported.
 */

#ifndef RB_GTK4_COMPAT_H
#define RB_GTK4_COMPAT_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* GtkTargetEntry was removed in GTK4; DnD now uses GdkContentFormats.
 * This stub struct keeps existing code compiling until DnD is ported. */
typedef struct {
	char *target;
	guint flags;
	guint info;
} GtkTargetEntry;

/* GtkTargetList was removed in GTK4 */
typedef gpointer GtkTargetList;

/* GdkAtom was removed in GTK4, replaced by const char * mime types */
typedef const char * GdkAtom;

/* GdkDragContext was removed in GTK4, replaced by GdkDrag / GdkDrop */
typedef gpointer GdkDragContext;

/* GTK_ICON_SIZE_MENU removed in GTK4 */
#ifndef GTK_ICON_SIZE_MENU
#define GTK_ICON_SIZE_MENU GTK_ICON_SIZE_NORMAL
#endif

G_END_DECLS

#endif /* RB_GTK4_COMPAT_H */
