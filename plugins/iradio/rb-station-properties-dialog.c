/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Colin Walters <walters@gnu.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <string.h>
#include <time.h>

#include <glib/gi18n.h>
#include <adwaita.h>

#include "rb-station-properties-dialog.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"
#include "rb-util.h"

static void rb_station_properties_dialog_class_init (RBStationPropertiesDialogClass *klass);
static void rb_station_properties_dialog_init (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_dispose (GObject *object);
static void rb_station_properties_dialog_finalize (GObject *object);
static void rb_station_properties_dialog_set_property (GObject *object,
						       guint prop_id,
						       const GValue *value,
						       GParamSpec *pspec);
static void rb_station_properties_dialog_get_property (GObject *object,
						       guint prop_id,
						       GValue *value,
						       GParamSpec *pspec);
static void rb_station_properties_dialog_setup (RBStationPropertiesDialog *dialog);
static gboolean rb_station_properties_dialog_get_current_entry (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_title (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_location (RBStationPropertiesDialog *dialog);

static void rb_station_properties_dialog_update (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_title_entry (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_genre (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_play_count (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_bitrate (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_last_played (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_rating (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_update_playback_error (RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_rated_cb (RBRating *rating,
						   double score,
						   RBStationPropertiesDialog *dialog);
static void rb_station_properties_dialog_sync_entries (RBStationPropertiesDialog *dialog);

struct RBStationPropertiesDialogPrivate
{
	GObject     *plugin;
	RBEntryView *entry_view;
	RhythmDB    *db;
	RhythmDBEntry *current_entry;

	GtkWidget   *title;
	GtkWidget   *genre;
	GtkWidget   *location;
	GtkWidget   *lastplayed;
	GtkWidget   *playcount;
	GtkWidget   *bitrate;
	GtkWidget   *rating;
	GtkWidget   *playback_error;
	GtkWidget   *playback_error_box;
};

#define RB_STATION_PROPERTIES_DIALOG_GET_PRIVATE(o) (rb_station_properties_dialog_get_instance_private (o))

enum
{
	PROP_0,
	PROP_ENTRY_VIEW,
	PROP_PLUGIN
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (RBStationPropertiesDialog,
	rb_station_properties_dialog,
	ADW_TYPE_DIALOG,
	0,
	G_ADD_PRIVATE_DYNAMIC (RBStationPropertiesDialog))

static void
rb_station_properties_dialog_class_init (RBStationPropertiesDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = rb_station_properties_dialog_set_property;
	object_class->get_property = rb_station_properties_dialog_get_property;

	g_object_class_install_property (object_class,
					 PROP_ENTRY_VIEW,
					 g_param_spec_object ("entry-view",
					                      "RBEntryView",
					                      "RBEntryView object",
					                      RB_TYPE_ENTRY_VIEW,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_PLUGIN,
					 g_param_spec_object ("plugin",
					                      "plugin instance",
					                      "plugin instance to use to find files",
					                      G_TYPE_OBJECT,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	object_class->dispose = rb_station_properties_dialog_dispose;
	object_class->finalize = rb_station_properties_dialog_finalize;
}

static void
rb_station_properties_dialog_class_finalize (RBStationPropertiesDialogClass *klass)
{
}

static void
rb_station_properties_dialog_init (RBStationPropertiesDialog *dialog)
{
        dialog->priv = RB_STATION_PROPERTIES_DIALOG_GET_PRIVATE (dialog);
}

/* Helper: add a bold label + value label row to a grid */
static void
add_label_row (GtkGrid *grid, int row, const char *desc_text,
	       GtkWidget **value_widget)
{
	GtkWidget *desc;
	PangoAttrList *attrs;

	desc = gtk_label_new (desc_text);
	gtk_label_set_xalign (GTK_LABEL (desc), 0.0);
	gtk_widget_set_halign (desc, GTK_ALIGN_START);

	attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes (GTK_LABEL (desc), attrs);
	pango_attr_list_unref (attrs);

	gtk_grid_attach (grid, desc, 0, row, 1, 1);

	*value_widget = gtk_label_new ("-");
	gtk_label_set_xalign (GTK_LABEL (*value_widget), 0.0);
	gtk_widget_set_halign (*value_widget, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (*value_widget, TRUE);
	gtk_label_set_selectable (GTK_LABEL (*value_widget), TRUE);

	gtk_grid_attach (grid, *value_widget, 1, row, 1, 1);
}

/* Helper: add a bold label + editable entry row to a grid */
static GtkWidget *
add_entry_row (GtkGrid *grid, int row, const char *desc_text, gboolean use_underline)
{
	GtkWidget *desc;
	GtkWidget *entry;
	PangoAttrList *attrs;

	desc = gtk_label_new (desc_text);
	gtk_label_set_xalign (GTK_LABEL (desc), 0.0);
	gtk_widget_set_halign (desc, GTK_ALIGN_START);
	gtk_label_set_use_underline (GTK_LABEL (desc), use_underline);

	attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes (GTK_LABEL (desc), attrs);
	pango_attr_list_unref (attrs);

	gtk_grid_attach (grid, desc, 0, row, 1, 1);

	entry = gtk_entry_new ();
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	gtk_widget_set_hexpand (entry, TRUE);
	gtk_label_set_mnemonic_widget (GTK_LABEL (desc), entry);

	gtk_grid_attach (grid, entry, 1, row, 1, 1);

	return entry;
}

static void
rb_station_properties_dialog_closed_cb (AdwDialog *adw_dialog)
{
	RBStationPropertiesDialog *dialog = RB_STATION_PROPERTIES_DIALOG (adw_dialog);

	if (dialog->priv->current_entry)
		rb_station_properties_dialog_sync_entries (dialog);
}

static void
rb_station_properties_dialog_setup (RBStationPropertiesDialog *dialog)
{
	GtkWidget *toolbar_view;
	GtkWidget *header_bar;
	GtkWidget *stack;
	GtkWidget *switcher;
	GtkWidget *grid;
	GtkWidget *rating_desc;
	GtkWidget *rating_box;
	PangoAttrList *attrs;
	int row;

	/* Stack + Switcher in header bar */
	stack = gtk_stack_new ();
	switcher = gtk_stack_switcher_new ();
	gtk_stack_switcher_set_stack (GTK_STACK_SWITCHER (switcher), GTK_STACK (stack));

	header_bar = adw_header_bar_new ();
	adw_header_bar_set_title_widget (ADW_HEADER_BAR (header_bar), switcher);

	toolbar_view = adw_toolbar_view_new ();
	adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar_view), header_bar);
	adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar_view), stack);

	adw_dialog_set_child (ADW_DIALOG (dialog), toolbar_view);
	adw_dialog_set_content_width (ADW_DIALOG (dialog), 450);
	adw_dialog_set_content_height (ADW_DIALOG (dialog), 400);

	/* Sync entries on close */
	g_signal_connect (dialog, "closed",
			  G_CALLBACK (rb_station_properties_dialog_closed_cb), NULL);

	/* ---- Basic page ---- */
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_widget_set_margin_start (grid, 12);
	gtk_widget_set_margin_end (grid, 12);
	gtk_widget_set_margin_top (grid, 12);
	gtk_widget_set_margin_bottom (grid, 12);
	row = 0;

	dialog->priv->title = add_entry_row (GTK_GRID (grid), row++, _("_Title:"), TRUE);
	dialog->priv->genre = add_entry_row (GTK_GRID (grid), row++, _("_Genre:"), TRUE);

	/* Playback error box */
	dialog->priv->playback_error_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_visible (dialog->priv->playback_error_box, FALSE);
	{
		GtkWidget *error_image = gtk_image_new_from_icon_name ("dialog-error");
		gtk_image_set_pixel_size (GTK_IMAGE (error_image), 64);
		gtk_box_append (GTK_BOX (dialog->priv->playback_error_box), error_image);

		dialog->priv->playback_error = gtk_label_new (NULL);
		gtk_label_set_wrap (GTK_LABEL (dialog->priv->playback_error), TRUE);
		gtk_box_append (GTK_BOX (dialog->priv->playback_error_box), dialog->priv->playback_error);
	}
	gtk_grid_attach (GTK_GRID (grid), dialog->priv->playback_error_box, 0, row, 2, 1);

	gtk_stack_add_titled (GTK_STACK (stack), grid, "basic", _("Basic"));

	/* ---- Details page ---- */
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_widget_set_margin_start (grid, 12);
	gtk_widget_set_margin_end (grid, 12);
	gtk_widget_set_margin_top (grid, 12);
	gtk_widget_set_margin_bottom (grid, 12);
	row = 0;

	dialog->priv->location = add_entry_row (GTK_GRID (grid), row++, _("L_ocation:"), TRUE);

	add_label_row (GTK_GRID (grid), row++, _("Bitrate:"),
		       &dialog->priv->bitrate);
	add_label_row (GTK_GRID (grid), row++, _("Last played:"),
		       &dialog->priv->lastplayed);
	add_label_row (GTK_GRID (grid), row++, _("Play count:"),
		       &dialog->priv->playcount);

	/* Rating row — bold label + RBRating widget */
	rating_desc = gtk_label_new (_("_Rating:"));
	gtk_label_set_use_underline (GTK_LABEL (rating_desc), TRUE);
	gtk_label_set_xalign (GTK_LABEL (rating_desc), 0.0);
	gtk_widget_set_halign (rating_desc, GTK_ALIGN_START);
	attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes (GTK_LABEL (rating_desc), attrs);
	pango_attr_list_unref (attrs);
	gtk_grid_attach (GTK_GRID (grid), rating_desc, 0, row, 1, 1);

	dialog->priv->rating = GTK_WIDGET (rb_rating_new ());
	g_signal_connect_object (dialog->priv->rating,
				 "rated",
				 G_CALLBACK (rb_station_properties_dialog_rated_cb),
				 G_OBJECT (dialog), 0);

	rating_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_append (GTK_BOX (rating_box), dialog->priv->rating);
	gtk_grid_attach (GTK_GRID (grid), rating_box, 1, row, 1, 1);

	/* accessibility */
	gtk_accessible_update_relation (GTK_ACCESSIBLE (dialog->priv->rating),
					GTK_ACCESSIBLE_RELATION_LABELLED_BY,
					rating_desc, NULL,
					-1);

	gtk_stack_add_titled (GTK_STACK (stack), grid, "details", _("Details"));
}

static void
rb_station_properties_dialog_dispose (GObject *object)
{
	RBStationPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (object));

	dialog = RB_STATION_PROPERTIES_DIALOG (object);
	g_return_if_fail (dialog->priv != NULL);

	g_clear_object (&dialog->priv->db);

	G_OBJECT_CLASS (rb_station_properties_dialog_parent_class)->dispose (object);
}

static void
rb_station_properties_dialog_finalize (GObject *object)
{
	RBStationPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (object));

	dialog = RB_STATION_PROPERTIES_DIALOG (object);
	g_return_if_fail (dialog->priv != NULL);

	if (dialog->priv->current_entry != NULL) {
		rhythmdb_entry_unref (dialog->priv->current_entry);
		dialog->priv->current_entry = NULL;
	}

	G_OBJECT_CLASS (rb_station_properties_dialog_parent_class)->finalize (object);
}

static void
rb_station_properties_dialog_set_property (GObject *object,
					   guint prop_id,
					   const GValue *value,
					   GParamSpec *pspec)
{
	RBStationPropertiesDialog *dialog = RB_STATION_PROPERTIES_DIALOG (object);

	switch (prop_id) {
	case PROP_ENTRY_VIEW:
		g_clear_object (&dialog->priv->db);

		dialog->priv->entry_view = g_value_get_object (value);

		if (dialog->priv->entry_view != NULL) {
			g_object_get (dialog->priv->entry_view,
				      "db", &dialog->priv->db, NULL);
		}
		break;
	case PROP_PLUGIN:
		dialog->priv->plugin = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_station_properties_dialog_get_property (GObject *object,
					   guint prop_id,
					   GValue *value,
					   GParamSpec *pspec)
{
	RBStationPropertiesDialog *dialog = RB_STATION_PROPERTIES_DIALOG (object);

	switch (prop_id) {
	case PROP_ENTRY_VIEW:
		g_value_set_object (value, dialog->priv->entry_view);
		break;
	case PROP_PLUGIN:
		g_value_set_object (value, dialog->priv->plugin);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GtkWidget *
rb_station_properties_dialog_new (GObject *plugin, RBEntryView *entry_view)
{
	RBStationPropertiesDialog *dialog;

	g_return_val_if_fail (RB_IS_ENTRY_VIEW (entry_view), NULL);

	dialog = g_object_new (RB_TYPE_STATION_PROPERTIES_DIALOG,
			       "plugin", plugin,
			       "entry-view", entry_view,
			       NULL);

	rb_station_properties_dialog_setup (dialog);

	if (!rb_station_properties_dialog_get_current_entry (dialog)) {
		g_object_unref (G_OBJECT (dialog));
		return NULL;
	}

	rb_station_properties_dialog_update (dialog);

	return GTK_WIDGET (dialog);
}

static gboolean
rb_station_properties_dialog_get_current_entry (RBStationPropertiesDialog *dialog)
{
	GList *selected_entries;

	/* get the entry */
	selected_entries = rb_entry_view_get_selected_entries (dialog->priv->entry_view);

	if ((selected_entries == NULL) ||
	    (selected_entries->data == NULL)) {
		dialog->priv->current_entry = NULL;
		return FALSE;
	}

	if (dialog->priv->current_entry != NULL) {
		rhythmdb_entry_unref (dialog->priv->current_entry);
	}

	dialog->priv->current_entry = rhythmdb_entry_ref (selected_entries->data);

	g_list_foreach (selected_entries, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (selected_entries);

	return TRUE;
}

static void
rb_station_properties_dialog_update (RBStationPropertiesDialog *dialog)
{
	rb_station_properties_dialog_update_title (dialog);

	if (dialog->priv->current_entry) {
		rb_station_properties_dialog_update_location (dialog);
		rb_station_properties_dialog_update_title_entry (dialog);
		rb_station_properties_dialog_update_genre (dialog);
	}

	rb_station_properties_dialog_update_play_count (dialog);
	rb_station_properties_dialog_update_bitrate (dialog);
	rb_station_properties_dialog_update_last_played (dialog);
	rb_station_properties_dialog_update_rating (dialog);
	rb_station_properties_dialog_update_playback_error (dialog);
}

static void
rb_station_properties_dialog_update_title (RBStationPropertiesDialog *dialog)
{
	const char *name;
	char *tmp;

	if (dialog->priv->current_entry) {
		name = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
		tmp = g_strdup_printf (_("%s Properties"), name);
		adw_dialog_set_title (ADW_DIALOG (dialog), tmp);
		g_free (tmp);
	} else {
		adw_dialog_set_title (ADW_DIALOG (dialog), _("New Internet Radio Station"));
	}
}

static void
rb_station_properties_dialog_update_title_entry (RBStationPropertiesDialog *dialog)
{
	const char *title;

	title = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
	gtk_editable_set_text (GTK_EDITABLE (dialog->priv->title), title);
}

static void
rb_station_properties_dialog_update_genre (RBStationPropertiesDialog *dialog)
{
	const char *genre;

	genre = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_GENRE);
	gtk_editable_set_text (GTK_EDITABLE (dialog->priv->genre), genre);
}

static void
rb_station_properties_dialog_update_location (RBStationPropertiesDialog *dialog)
{
	const char *location;
	char *unescaped;

	location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LOCATION);
	unescaped = g_uri_unescape_string (location, NULL);
	gtk_editable_set_text (GTK_EDITABLE (dialog->priv->location), unescaped);
	g_free (unescaped);
}

static void
rb_station_properties_dialog_rated_cb (RBRating *rating,
				       double score,
				       RBStationPropertiesDialog *dialog)
{
	GValue value = {0, };

	g_return_if_fail (RB_IS_RATING (rating));
	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (dialog));
	g_return_if_fail (score >= 0 && score <= 5 );

	if (!dialog->priv->current_entry)
		return;

	g_value_init (&value, G_TYPE_DOUBLE);
	g_value_set_double (&value, score);

	/* set the new value for the song */
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry,
			    RHYTHMDB_PROP_RATING,
			    &value);
	g_value_unset (&value);
	rhythmdb_commit (dialog->priv->db);

	g_object_set (G_OBJECT (dialog->priv->rating), "rating", score, NULL);
}

static void
rb_station_properties_dialog_update_play_count (RBStationPropertiesDialog *dialog)
{
	char *text;
	long int count = 0;

	if (dialog->priv->current_entry)
		count = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_PLAY_COUNT);

	text = g_strdup_printf ("%ld", count);
	gtk_label_set_text (GTK_LABEL (dialog->priv->playcount), text);
	g_free (text);
}

static void
rb_station_properties_dialog_update_bitrate (RBStationPropertiesDialog *dialog)
{
	gulong val = 0;
	char *text;

	if (dialog->priv->current_entry)
		val = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_BITRATE);

	if (val == 0)
		text = g_strdup (_("Unknown"));
	else
		text = g_strdup_printf (_("%lu kbps"), val);

	gtk_label_set_text (GTK_LABEL (dialog->priv->bitrate), text);
	g_free (text);
}

static void
rb_station_properties_dialog_update_last_played (RBStationPropertiesDialog *dialog)
{
	const char *last_played = _("Never");

	if (dialog->priv->current_entry)
		last_played = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LAST_PLAYED_STR);
	gtk_label_set_text (GTK_LABEL (dialog->priv->lastplayed), last_played);
}

static void
rb_station_properties_dialog_update_rating (RBStationPropertiesDialog *dialog)
{
	gdouble rating = 0.0;
	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (dialog));

	if (dialog->priv->current_entry)
		rating = rhythmdb_entry_get_double (dialog->priv->current_entry, RHYTHMDB_PROP_RATING);

	g_object_set (G_OBJECT (dialog->priv->rating), "rating", rating, NULL);
}

static void
rb_station_properties_dialog_update_playback_error (RBStationPropertiesDialog *dialog)
{
	const char *error;

	g_return_if_fail (RB_IS_STATION_PROPERTIES_DIALOG (dialog));

	if (dialog->priv->current_entry == NULL) {
		gtk_widget_set_visible (dialog->priv->playback_error_box, FALSE);
		return;
	}

	error = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_PLAYBACK_ERROR);
	if (error) {
		gtk_label_set_text (GTK_LABEL (dialog->priv->playback_error), error);
		gtk_widget_set_visible (dialog->priv->playback_error_box, TRUE);
	} else {
		gtk_label_set_text (GTK_LABEL (dialog->priv->playback_error), "");
		gtk_widget_set_visible (dialog->priv->playback_error_box, FALSE);
	}
}

static void
rb_station_properties_dialog_sync_entries (RBStationPropertiesDialog *dialog)
{
	const char *title;
	const char *genre;
	const char *location;
	const char *string;
	GValue val = {0,};
	gboolean changed = FALSE;
	RhythmDBEntry *entry = dialog->priv->current_entry;

	title = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->title));
	genre = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->genre));
	location = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->location));

	string = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
	if (strcmp (title, string)) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, title);
		rhythmdb_entry_set (dialog->priv->db, entry,
				    RHYTHMDB_PROP_TITLE,
				    &val);
		g_value_unset (&val);
		changed = TRUE;
	}

	string = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE);
	if (strcmp (genre, string)) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, genre);
		rhythmdb_entry_set (dialog->priv->db, entry,
				    RHYTHMDB_PROP_GENRE, &val);
		g_value_unset (&val);
		changed = TRUE;
	}

	string = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	if (strcmp (location, string)) {
		if (rhythmdb_entry_lookup_by_location (dialog->priv->db, location) == NULL) {
			g_value_init (&val, G_TYPE_STRING);
			g_value_set_string (&val, location);
			rhythmdb_entry_set (dialog->priv->db, entry,
					    RHYTHMDB_PROP_LOCATION, &val);
			g_value_unset (&val);
			changed = TRUE;
		} else {
			rb_error_dialog (NULL, _("Unable to change station property"), _("Unable to change station URI to %s, as that station already exists"), location);
		}
	}

	if (changed)
		rhythmdb_commit (dialog->priv->db);
}

void
_rb_station_properties_dialog_register_type (GTypeModule *module)
{
	rb_station_properties_dialog_register_type (module);
}
