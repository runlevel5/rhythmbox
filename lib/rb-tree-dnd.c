/* rbtreednd.c
 * Copyright (C) 2001  Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#include config.h

#include <string.h>
#include <gtk/gtk.h>

#include rb-tree-dnd.h

/*
 * DnD support for tree views.
 *
 * TODO: The GTK3 drag-and-drop implementation has been stripped out
 * as GTK4 uses a completely different DnD API based on GdkDrag,
 * GdkDrop, GtkDragSource, and GtkDropTarget.  This needs to be
 * reimplemented using those APIs.
 */

GType
rb_tree_drag_source_get_type (void)
{
	static GType our_type = 0;

	if (!our_type) {
		static const GTypeInfo our_info = {
			sizeof (RbTreeDragSourceIface),
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			0,
			0,
			NULL
		};

		our_type = g_type_register_static (G_TYPE_INTERFACE,
						   RbTreeDragSource,
						   &our_info, 0);
	}

	return our_type;
}

GType
rb_tree_drag_dest_get_type (void)
{
	static GType our_type = 0;

	if (!our_type) {
		static const GTypeInfo our_info = {
			sizeof (RbTreeDragDestIface),
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			0,
			0,
			NULL
		};

		our_type = g_type_register_static (G_TYPE_INTERFACE,
						   RbTreeDragDest,
						   &our_info, 0);
	}

	return our_type;
}

gboolean
rb_tree_drag_source_row_draggable (RbTreeDragSource *drag_source,
				   GList            *path_list)
{
	RbTreeDragSourceIface *iface = RB_TREE_DRAG_SOURCE_GET_IFACE (drag_source);
	if (iface->rb_row_draggable)
		return iface->rb_row_draggable (drag_source, path_list);
	return FALSE;
}

gboolean
rb_tree_drag_source_drag_data_delete (RbTreeDragSource *drag_source,
				      GList            *path_list)
{
	RbTreeDragSourceIface *iface = RB_TREE_DRAG_SOURCE_GET_IFACE (drag_source);
	if (iface->rb_drag_data_delete)
		return iface->rb_drag_data_delete (drag_source, path_list);
	return FALSE;
}

gboolean
rb_tree_drag_source_drag_data_get (RbTreeDragSource *drag_source,
				   GList            *path_list,
				   gpointer          selection_data)
{
	RbTreeDragSourceIface *iface = RB_TREE_DRAG_SOURCE_GET_IFACE (drag_source);
	if (iface->rb_drag_data_get)
		return iface->rb_drag_data_get (drag_source, path_list, selection_data);
	return FALSE;
}

gboolean
rb_tree_drag_dest_drag_data_received (RbTreeDragDest        *drag_dest,
				      GtkTreePath           *dest,
				      GtkTreeViewDropPosition pos,
				      gpointer               selection_data)
{
	RbTreeDragDestIface *iface = RB_TREE_DRAG_DEST_GET_IFACE (drag_dest);
	if (iface->rb_drag_data_received)
		return iface->rb_drag_data_received (drag_dest, dest, pos, selection_data);
	return FALSE;
}

gboolean
rb_tree_drag_dest_row_drop_possible (RbTreeDragDest        *drag_dest,
				     GtkTreePath           *dest_path,
				     GtkTreeViewDropPosition pos,
				     gpointer               selection_data)
{
	RbTreeDragDestIface *iface = RB_TREE_DRAG_DEST_GET_IFACE (drag_dest);
	if (iface->rb_row_drop_possible)
		return iface->rb_row_drop_possible (drag_dest, dest_path, pos, selection_data);
	return FALSE;
}

gboolean
rb_tree_drag_dest_row_drop_position (RbTreeDragDest        *drag_dest,
				     GtkTreePath           *dest_path,
				     GList                 *targets,
				     GtkTreeViewDropPosition *pos)
{
	RbTreeDragDestIface *iface = RB_TREE_DRAG_DEST_GET_IFACE (drag_dest);
	if (iface->rb_row_drop_position)
		return iface->rb_row_drop_position (drag_dest, dest_path, targets, pos);
	return FALSE;
}
