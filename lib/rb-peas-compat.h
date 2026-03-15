/* rb-peas-compat.h
 *
 * Compatibility shim for libpeas-2.
 * PeasActivatable was removed from libpeas-2 -- apps must define
 * their own activatable interface. This provides a drop-in replacement
 * so existing plugin code compiles without modification.
 */

#ifndef RB_PEAS_COMPAT_H
#define RB_PEAS_COMPAT_H

#include <libpeas.h>

G_BEGIN_DECLS

#define PEAS_TYPE_ACTIVATABLE (peas_activatable_get_type ())

G_DECLARE_INTERFACE (PeasActivatable, peas_activatable, PEAS, ACTIVATABLE, GObject)

struct _PeasActivatableInterface
{
	GTypeInterface g_iface;

	void (*activate)   (PeasActivatable *activatable);
	void (*deactivate) (PeasActivatable *activatable);
};

void peas_activatable_activate   (PeasActivatable *activatable);
void peas_activatable_deactivate (PeasActivatable *activatable);

G_END_DECLS

#endif /* RB_PEAS_COMPAT_H */
