/* rb-peas-gtk-configurable.h
 *
 * Compatibility shim for libpeas-2.
 * PeasGtkConfigurable was removed from libpeas-2 -- apps must define
 * their own configurable interface. This provides a drop-in replacement
 * so existing plugin configuration code works without modification.
 */

#ifndef RB_PEAS_GTK_CONFIGURABLE_H
#define RB_PEAS_GTK_CONFIGURABLE_H

#include <gtk/gtk.h>
#include <libpeas.h>

G_BEGIN_DECLS

#define PEAS_GTK_TYPE_CONFIGURABLE (peas_gtk_configurable_get_type ())

G_DECLARE_INTERFACE (PeasGtkConfigurable, peas_gtk_configurable, PEAS_GTK, CONFIGURABLE, GObject)

struct _PeasGtkConfigurableInterface
{
	GTypeInterface g_iface;

	GtkWidget *(*create_configure_widget) (PeasGtkConfigurable *configurable);
};

GtkWidget *peas_gtk_configurable_create_configure_widget (PeasGtkConfigurable *configurable);

G_END_DECLS

#endif /* RB_PEAS_GTK_CONFIGURABLE_H */
