/* rb-peas-gtk-configurable.c
 *
 * Provides PeasGtkConfigurable interface removed in libpeas-2.
 */

#include "rb-peas-gtk-configurable.h"

G_DEFINE_INTERFACE (PeasGtkConfigurable, peas_gtk_configurable, G_TYPE_OBJECT)

static void
peas_gtk_configurable_default_init (PeasGtkConfigurableInterface *iface)
{
	g_object_interface_install_property (iface,
		g_param_spec_object ("object",
				     "Object",
				     "Object",
				     G_TYPE_OBJECT,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

/**
 * peas_gtk_configurable_create_configure_widget:
 * @configurable: a #PeasGtkConfigurable
 *
 * Creates the configure widget for the plugin.
 *
 * Returns: (transfer full): a #GtkWidget for configuring the plugin
 */
GtkWidget *
peas_gtk_configurable_create_configure_widget (PeasGtkConfigurable *configurable)
{
	PeasGtkConfigurableInterface *iface;

	g_return_val_if_fail (PEAS_GTK_IS_CONFIGURABLE (configurable), NULL);

	iface = PEAS_GTK_CONFIGURABLE_GET_IFACE (configurable);
	g_return_val_if_fail (iface->create_configure_widget != NULL, NULL);

	return iface->create_configure_widget (configurable);
}
