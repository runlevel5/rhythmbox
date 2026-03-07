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


/* gtk_selection_data_* accessors removed in GTK4 (DnD rewrite).
 * These stubs return dummy values to keep code compiling. */
#define gtk_selection_data_get_target(sd)    ((GdkAtom)NULL)
#define gtk_selection_data_get_format(sd)    (0)
#define gtk_selection_data_get_length(sd)    (-1)
#define gtk_selection_data_get_data_type(sd) ((GdkAtom)NULL)
#define gtk_selection_data_get_data(sd)      ((const guchar *)NULL)
#define gtk_selection_data_set(sd,t,f,d,l)   /* GTK4: DnD stub */
#define gdk_atom_name(a)                     ((char *)(a))

/* gtk_target_list_new / gtk_target_list_find removed in GTK4 */
#define gtk_target_list_new(targets, n)      (NULL)
#define gtk_target_list_find(list, atom, p)  (FALSE)

G_END_DECLS

#endif /* RB_GTK4_COMPAT_H */
