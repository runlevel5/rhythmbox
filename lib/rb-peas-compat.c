/* rb-peas-compat.c
 *
 * Provides PeasActivatable interface removed in libpeas-2.
 */

#include "rb-peas-compat.h"

G_DEFINE_INTERFACE (PeasActivatable, peas_activatable, G_TYPE_OBJECT)

static void
peas_activatable_default_init (PeasActivatableInterface *iface)
{
	g_object_interface_install_property (iface,
		g_param_spec_object ("object",
				     "Object",
				     "Object",
				     G_TYPE_OBJECT,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

void
peas_activatable_activate (PeasActivatable *activatable)
{
	PeasActivatableInterface *iface;

	g_return_if_fail (PEAS_IS_ACTIVATABLE (activatable));

	iface = PEAS_ACTIVATABLE_GET_IFACE (activatable);
	g_return_if_fail (iface->activate != NULL);

	iface->activate (activatable);
}

void
peas_activatable_deactivate (PeasActivatable *activatable)
{
	PeasActivatableInterface *iface;

	g_return_if_fail (PEAS_IS_ACTIVATABLE (activatable));

	iface = PEAS_ACTIVATABLE_GET_IFACE (activatable);
	g_return_if_fail (iface->deactivate != NULL);

	iface->deactivate (activatable);
}
