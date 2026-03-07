/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006  Kristian Rietveld <kris@babi-pangang.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <graphene.h>

#include "gossip-cell-renderer-expander.h"

#define GET_PRIV(obj) (gossip_cell_renderer_expander_get_instance_private (GOSSIP_CELL_RENDERER_EXPANDER (obj)))

static void     gossip_cell_renderer_expander_init         (GossipCellRendererExpander      *expander);
static void     gossip_cell_renderer_expander_class_init   (GossipCellRendererExpanderClass *klass);
static void     gossip_cell_renderer_expander_get_property (GObject                         *object,
							    guint                            param_id,
							    GValue                          *value,
							    GParamSpec                      *pspec);
static void     gossip_cell_renderer_expander_set_property (GObject                         *object,
							    guint                            param_id,
							    const GValue                    *value,
							    GParamSpec                      *pspec);
static void     gossip_cell_renderer_expander_snapshot     (GtkCellRenderer                 *cell,
							    GtkSnapshot                     *snapshot,
							    GtkWidget                       *widget,
							    const GdkRectangle              *background_area,
							    const GdkRectangle              *cell_area,
							    GtkCellRendererState             flags);
static gboolean gossip_cell_renderer_expander_activate     (GtkCellRenderer                 *cell,
							    GdkEvent                        *event,
							    GtkWidget                       *widget,
							    const gchar                     *path,
							    const GdkRectangle              *background_area,
							    const GdkRectangle              *cell_area,
							    GtkCellRendererState             flags);

/* Properties */
enum {
	PROP_0,
	PROP_EXPANDER_STYLE,
	PROP_EXPANDER_SIZE,
	PROP_ACTIVATABLE
};

typedef struct _GossipCellRendererExpanderPriv GossipCellRendererExpanderPriv;

struct _GossipCellRendererExpanderPriv {
	gint                 expander_size;

	guint                activatable : 1;
	gboolean             is_expanded;
};

typedef GossipCellRendererExpanderPriv GossipCellRendererExpanderPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GossipCellRendererExpander, gossip_cell_renderer_expander, GTK_TYPE_CELL_RENDERER)

static void
gossip_cell_renderer_expander_init (GossipCellRendererExpander *expander)
{
	GossipCellRendererExpanderPriv *priv;

	priv = GET_PRIV (expander);

	priv->expander_size = 12;
	priv->activatable = TRUE;

	gtk_cell_renderer_set_padding (GTK_CELL_RENDERER (expander), 2, 2);
	g_object_set (expander,
		      "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
		      NULL);
}

static void
gossip_cell_renderer_expander_class_init (GossipCellRendererExpanderClass *klass)
{
	GObjectClass         *object_class;
	GtkCellRendererClass *cell_class;

	object_class  = G_OBJECT_CLASS (klass);
	cell_class = GTK_CELL_RENDERER_CLASS (klass);

	object_class->get_property = gossip_cell_renderer_expander_get_property;
	object_class->set_property = gossip_cell_renderer_expander_set_property;

	cell_class->snapshot = gossip_cell_renderer_expander_snapshot;
	cell_class->activate = gossip_cell_renderer_expander_activate;

	g_object_class_install_property (object_class,
					 PROP_EXPANDER_STYLE,
					 g_param_spec_boolean ("expander-style",
							       "Expander Style",
							       "Whether the expander is expanded",
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_EXPANDER_SIZE,
					 g_param_spec_int ("expander-size",
							   "Expander Size",
							   "The size of the expander",
							   0,
							   G_MAXINT,
							   12,
							   G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_ACTIVATABLE,
					 g_param_spec_boolean ("activatable",
							       "Activatable",
							       "The expander can be activated",
							       TRUE,
							       G_PARAM_READWRITE));

}

static void
gossip_cell_renderer_expander_get_property (GObject    *object,
					    guint       param_id,
					    GValue     *value,
					    GParamSpec *pspec)
{
	GossipCellRendererExpander     *expander;
	GossipCellRendererExpanderPriv *priv;

	expander = GOSSIP_CELL_RENDERER_EXPANDER (object);
	priv = GET_PRIV (expander);

	switch (param_id) {
	case PROP_EXPANDER_STYLE:
		g_value_set_boolean (value, priv->is_expanded);
		break;

	case PROP_EXPANDER_SIZE:
		g_value_set_int (value, priv->expander_size);
		break;

	case PROP_ACTIVATABLE:
		g_value_set_boolean (value, priv->activatable);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
gossip_cell_renderer_expander_set_property (GObject      *object,
					    guint         param_id,
					    const GValue *value,
					    GParamSpec   *pspec)
{
	GossipCellRendererExpander     *expander;
	GossipCellRendererExpanderPriv *priv;

	expander = GOSSIP_CELL_RENDERER_EXPANDER (object);
	priv = GET_PRIV (expander);

	switch (param_id) {
	case PROP_EXPANDER_STYLE:
		priv->is_expanded = g_value_get_boolean (value);
		break;

	case PROP_EXPANDER_SIZE:
		priv->expander_size = g_value_get_int (value);
		break;

	case PROP_ACTIVATABLE:
		priv->activatable = g_value_get_boolean (value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

GtkCellRenderer *
gossip_cell_renderer_expander_new (void)
{
	return g_object_new (GOSSIP_TYPE_CELL_RENDERER_EXPANDER, NULL);
}



static void
gossip_cell_renderer_expander_snapshot (GtkCellRenderer      *cell,
					GtkSnapshot          *snapshot,
					GtkWidget            *widget,
					const GdkRectangle   *background_area,
					const GdkRectangle   *cell_area,
					GtkCellRendererState  flags)
{
	GossipCellRendererExpander     *expander;
	GossipCellRendererExpanderPriv *priv;
	gint                            xpad, ypad;
	gint                            x_offset, y_offset;
	gfloat                          xalign, yalign;

	expander = (GossipCellRendererExpander*) cell;
	priv = GET_PRIV (expander);
	gtk_cell_renderer_get_padding (cell, &xpad, &ypad);
	gtk_cell_renderer_get_alignment (cell, &xalign, &yalign);

	x_offset = xalign * (cell_area->width - (priv->expander_size + (2 * xpad)));
	x_offset = MAX (x_offset, 0);
	y_offset = yalign * (cell_area->height - (priv->expander_size + (2 * ypad)));
	y_offset = MAX (y_offset, 0);

	/* In GTK4, we just use GtkTreeExpander or custom drawing.
	 * For now, use a simple approach: render via cairo snapshot. */
	{
		GtkStyleContext *style_context;
		GtkStateFlags state;
		cairo_t *cr;

		style_context = gtk_widget_get_style_context (widget);
		gtk_style_context_save (style_context);
		gtk_widget_add_css_class (widget, "expander");

		state = gtk_cell_renderer_get_state (cell, widget, flags);
		if (priv->is_expanded) {
			state |= GTK_STATE_FLAG_CHECKED;
		}
		gtk_style_context_set_state (style_context, state);

		cr = gtk_snapshot_append_cairo (snapshot, &GRAPHENE_RECT_INIT (
			cell_area->x + x_offset + xpad,
			cell_area->y + y_offset + ypad,
			priv->expander_size,
			priv->expander_size));

		gtk_render_expander (style_context,
				     cr,
				     0, 0,
				     priv->expander_size,
				     priv->expander_size);

		cairo_destroy (cr);
		gtk_widget_remove_css_class (widget, "expander");
		gtk_style_context_restore (style_context);
	}
}

static gboolean
gossip_cell_renderer_expander_activate (GtkCellRenderer      *cell,
					GdkEvent             *event,
					GtkWidget            *widget,
					const gchar          *path_string,
					const GdkRectangle   *background_area,
					const GdkRectangle   *cell_area,
					GtkCellRendererState  flags)
{
	GossipCellRendererExpanderPriv *priv;
	GtkTreePath                    *path;
	gboolean                        in_cell;
	int                             mouse_x;
	int                             mouse_y;

	priv = GET_PRIV (cell);

	if (!GTK_IS_TREE_VIEW (widget) || !priv->activatable)
		return FALSE;

	path = gtk_tree_path_new_from_string (path_string);

	/* In GTK4, get coordinates from the event directly */
	{
		double ex, ey;
		gdk_event_get_position (event, &ex, &ey);
		mouse_x = (int) ex;
		mouse_y = (int) ey;
	}
	gtk_tree_view_convert_widget_to_bin_window_coords (GTK_TREE_VIEW (widget),
							   mouse_x, mouse_y,
							   &mouse_x, &mouse_y);

	/* check if click is within the cell */
	if (mouse_x - cell_area->x >= 0
	    && mouse_x - cell_area->x <= cell_area->width) {
		in_cell = TRUE;
	} else {
		in_cell = FALSE;
	}

	if (! in_cell) {
		gtk_tree_path_free (path);
		return FALSE;
	}

	if (gtk_tree_view_row_expanded (GTK_TREE_VIEW (widget), path)) {
		gtk_tree_view_collapse_row (GTK_TREE_VIEW (widget), path);
	} else {
		gtk_tree_view_expand_row (GTK_TREE_VIEW (widget), path, FALSE);
	}

	gtk_tree_path_free (path);

	return TRUE;
}
