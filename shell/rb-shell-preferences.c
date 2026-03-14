/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@debian.org>
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

/**
 * SECTION:rbshellpreferences
 * @short_description: preferences dialog
 *
 * The preferences dialog uses #AdwPreferencesDialog with pages for General,
 * Playback, source-specific settings, and Plugins.
 */

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <adwaita.h>

#include "rb-file-helpers.h"
#include "rb-shell-preferences.h"
#include "rb-source.h"
#include "rb-builder-helpers.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-util.h"
#include <libpeas.h>

static void rb_shell_preferences_class_init (RBShellPreferencesClass *klass);
static void rb_shell_preferences_init (RBShellPreferences *shell_preferences);
static void impl_finalize (GObject *object);
static void impl_dispose (GObject *object);

enum
{
	PROP_0,
};

#define COLUMN_CHECK_PROP_NAME	"rb-column-prop-name"

struct {
	const char *label;
	RhythmDBPropType prop;
} column_checks[] = {
	{ N_("Track Number"),	RHYTHMDB_PROP_TRACK_NUMBER },
	{ N_("Artist"),		RHYTHMDB_PROP_ARTIST },
	{ N_("Composer"),	RHYTHMDB_PROP_COMPOSER },
	{ N_("Album"),		RHYTHMDB_PROP_ALBUM },
	{ N_("Year"),		RHYTHMDB_PROP_DATE },
	{ N_("Last Played"),	RHYTHMDB_PROP_LAST_PLAYED },
	{ N_("Genre"),		RHYTHMDB_PROP_GENRE },
	{ N_("Date Added"),	RHYTHMDB_PROP_FIRST_SEEN },
	{ N_("Play Count"),	RHYTHMDB_PROP_PLAY_COUNT },
	{ N_("Comment"),	RHYTHMDB_PROP_COMMENT },
	{ N_("BPM"),		RHYTHMDB_PROP_BPM },
	{ N_("Rating"),		RHYTHMDB_PROP_RATING },
	{ N_("Time"),		RHYTHMDB_PROP_DURATION },
	{ N_("Location"),	RHYTHMDB_PROP_LOCATION },
	{ N_("Quality"),	RHYTHMDB_PROP_BITRATE }
};

struct RBShellPreferencesPrivate
{
	AdwPreferencesPage *general_page;
	AdwPreferencesPage *playback_page;

	/* General page */
	AdwComboRow *browser_views_row;
	GHashTable *column_switches;	/* prop_name -> AdwSwitchRow */

	/* Playback page */
	AdwSwitchRow *xfade_row;
	GtkScale *transition_scale;
	AdwPreferencesGroup *playback_plugin_group;
	AdwPreferencesGroup *general_plugin_group;

	gboolean applying_settings;

	GSettings *main_settings;
	GSettings *source_settings;
	GSettings *player_settings;
};


G_DEFINE_TYPE_WITH_PRIVATE (RBShellPreferences, rb_shell_preferences, ADW_TYPE_PREFERENCES_DIALOG)

static void
rb_shell_preferences_class_init (RBShellPreferencesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = impl_finalize;
	object_class->dispose = impl_dispose;
}

/* ---- General page: browser views ---- */

static void
browser_views_row_changed_cb (GObject *object, GParamSpec *pspec, RBShellPreferences *prefs)
{
	guint selected;

	if (prefs->priv->applying_settings)
		return;

	selected = adw_combo_row_get_selected (prefs->priv->browser_views_row);
	g_settings_set_enum (prefs->priv->source_settings, "browser-views", selected);
}

/* ---- General page: visible columns ---- */

static void
column_switch_toggled_cb (GObject *object, GParamSpec *pspec, RBShellPreferences *prefs)
{
	AdwSwitchRow *row = ADW_SWITCH_ROW (object);
	const char *prop_name;
	const char *column;
	GVariantBuilder *b;
	GVariantIter *iter;
	GVariant *v;

	prop_name = (const char *)g_object_get_data (G_OBJECT (row), COLUMN_CHECK_PROP_NAME);
	g_assert (prop_name);

	v = g_settings_get_value (prefs->priv->source_settings, "visible-columns");

	b = g_variant_builder_new (G_VARIANT_TYPE ("as"));
	iter = g_variant_iter_new (v);
	while (g_variant_iter_loop (iter, "s", &column)) {
		if (g_strcmp0 (column, prop_name) != 0) {
			g_variant_builder_add (b, "s", column);
		}
	}
	g_variant_iter_free (iter);
	g_variant_unref (v);

	if (adw_switch_row_get_active (row)) {
		g_variant_builder_add (b, "s", prop_name);
	}

	v = g_variant_builder_end (b);
	g_settings_set_value (prefs->priv->source_settings, "visible-columns", v);
	g_variant_builder_unref (b);
}

/* ---- Settings change callbacks ---- */

static void
source_settings_changed_cb (GSettings *settings, const char *key, RBShellPreferences *prefs)
{
	if (g_strcmp0 (key, "browser-views") == 0) {
		int view = g_settings_get_enum (prefs->priv->source_settings, "browser-views");
		prefs->priv->applying_settings = TRUE;
		adw_combo_row_set_selected (prefs->priv->browser_views_row, view);
		prefs->priv->applying_settings = FALSE;

	} else if (g_strcmp0 (key, "visible-columns") == 0) {
		char **columns;
		GHashTableIter iter;
		gpointer name_ptr;
		gpointer widget_ptr;

		columns = g_settings_get_strv (prefs->priv->source_settings, "visible-columns");

		g_hash_table_iter_init (&iter, prefs->priv->column_switches);
		while (g_hash_table_iter_next (&iter, &name_ptr, &widget_ptr)) {
			gboolean enabled = rb_str_in_strv (name_ptr, (const char **)columns);
			adw_switch_row_set_active (ADW_SWITCH_ROW (widget_ptr), enabled);
		}

		g_strfreev (columns);
	}
}

/* ---- Playback page ---- */

static void
player_settings_changed_cb (GSettings *settings, const char *key, RBShellPreferences *prefs)
{
	if (g_strcmp0 (key, "transition-time") == 0) {
		gtk_range_set_value (GTK_RANGE (prefs->priv->transition_scale),
				     g_settings_get_double (settings, key));
	}
}

static void
sync_transition_time (GSettings *settings, GtkRange *range)
{
	g_settings_set_double (settings,
			       "transition-time",
			       gtk_range_get_value (range));
}

static void
transition_time_changed_cb (GtkRange *range, RBShellPreferences *prefs)
{
	rb_settings_delayed_sync (prefs->priv->player_settings,
				  (RBDelayedSyncFunc) sync_transition_time,
				  g_object_ref (range),
				  g_object_unref);
}

static void
xfade_row_changed_cb (GObject *object, GParamSpec *pspec, RBShellPreferences *prefs)
{
	gboolean active = adw_switch_row_get_active (prefs->priv->xfade_row);
	gtk_widget_set_sensitive (GTK_WIDGET (prefs->priv->transition_scale), active);
}

/* ---- Build General page ---- */

static void
build_general_page (RBShellPreferences *prefs)
{
	AdwPreferencesPage *page;
	AdwPreferencesGroup *browser_group;
	AdwPreferencesGroup *columns_group;
	GtkStringList *browser_model;
	const char *browser_options[] = {
		N_("Artists and Albums"),
		N_("Genres and Artists"),
		N_("Genres, Artists, and Albums"),
		NULL
	};

	page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());
	adw_preferences_page_set_title (page, _("General"));
	adw_preferences_page_set_icon_name (page, "preferences-other-symbolic");

	/* Browser Views group */
	browser_group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	adw_preferences_group_set_title (browser_group, _("Browser Views"));

	browser_model = gtk_string_list_new (NULL);
	for (int i = 0; browser_options[i] != NULL; i++)
		gtk_string_list_append (browser_model, _(browser_options[i]));

	prefs->priv->browser_views_row = ADW_COMBO_ROW (adw_combo_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (prefs->priv->browser_views_row),
				       _("View layout"));
	adw_combo_row_set_model (prefs->priv->browser_views_row, G_LIST_MODEL (browser_model));
	g_object_unref (browser_model);

	g_signal_connect (prefs->priv->browser_views_row, "notify::selected",
			  G_CALLBACK (browser_views_row_changed_cb), prefs);

	adw_preferences_group_add (browser_group, GTK_WIDGET (prefs->priv->browser_views_row));
	adw_preferences_page_add (page, browser_group);

	/* Visible Columns group */
	columns_group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	adw_preferences_group_set_title (columns_group, _("Visible Columns"));

	prefs->priv->column_switches = g_hash_table_new (g_str_hash, g_str_equal);

	for (int i = 0; i < G_N_ELEMENTS (column_checks); i++) {
		AdwSwitchRow *row;
		const char *prop_name;

		row = ADW_SWITCH_ROW (adw_switch_row_new ());
		adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _(column_checks[i].label));

		prop_name = (const char *)rhythmdb_nice_elt_name_from_propid (NULL, column_checks[i].prop);
		g_assert (prop_name != NULL);

		g_object_set_data (G_OBJECT (row), COLUMN_CHECK_PROP_NAME, (gpointer)prop_name);
		g_signal_connect (row, "notify::active",
				  G_CALLBACK (column_switch_toggled_cb), prefs);

		g_hash_table_insert (prefs->priv->column_switches, (gpointer)prop_name, row);
		adw_preferences_group_add (columns_group, GTK_WIDGET (row));
	}

	adw_preferences_page_add (page, columns_group);

	/* Plugin extension group (hidden until a plugin adds widgets) */
	prefs->priv->general_plugin_group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	gtk_widget_set_visible (GTK_WIDGET (prefs->priv->general_plugin_group), FALSE);
	adw_preferences_page_add (page, prefs->priv->general_plugin_group);

	prefs->priv->general_page = page;
	adw_preferences_dialog_add (ADW_PREFERENCES_DIALOG (prefs), page);
}

/* ---- Build Playback page ---- */

static void
build_playback_page (RBShellPreferences *prefs)
{
	AdwPreferencesPage *page;
	AdwPreferencesGroup *xfade_group;
	AdwActionRow *duration_row;
	GtkAdjustment *adj;

	page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());
	adw_preferences_page_set_title (page, _("Playback"));
	adw_preferences_page_set_icon_name (page, "media-playback-start-symbolic");

	/* Crossfade group */
	xfade_group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	adw_preferences_group_set_title (xfade_group, _("Crossfade"));

	prefs->priv->xfade_row = ADW_SWITCH_ROW (adw_switch_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (prefs->priv->xfade_row),
				       _("Crossfade between tracks"));
	adw_preferences_group_add (xfade_group, GTK_WIDGET (prefs->priv->xfade_row));

	/* Duration row with a scale */
	duration_row = ADW_ACTION_ROW (adw_action_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (duration_row),
				       _("Duration (seconds)"));

	adj = gtk_adjustment_new (0, 0, 60, 0.1, 1.0, 0);
	prefs->priv->transition_scale = GTK_SCALE (gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, adj));
	gtk_widget_set_size_request (GTK_WIDGET (prefs->priv->transition_scale), 200, -1);
	gtk_widget_set_hexpand (GTK_WIDGET (prefs->priv->transition_scale), TRUE);
	gtk_widget_set_valign (GTK_WIDGET (prefs->priv->transition_scale), GTK_ALIGN_CENTER);
	gtk_scale_set_draw_value (prefs->priv->transition_scale, TRUE);
	gtk_scale_set_value_pos (prefs->priv->transition_scale, GTK_POS_LEFT);

	adw_action_row_add_suffix (duration_row, GTK_WIDGET (prefs->priv->transition_scale));
	adw_preferences_group_add (xfade_group, GTK_WIDGET (duration_row));

	adw_preferences_page_add (page, xfade_group);

	/* Plugin extension group (hidden until a plugin adds widgets) */
	prefs->priv->playback_plugin_group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	gtk_widget_set_visible (GTK_WIDGET (prefs->priv->playback_plugin_group), FALSE);
	adw_preferences_page_add (page, prefs->priv->playback_plugin_group);

	prefs->priv->playback_page = page;
	adw_preferences_dialog_add (ADW_PREFERENCES_DIALOG (prefs), page);
}

/* ---- Plugins page ---- */

static void
plugin_switch_toggled_cb (GObject *object, GParamSpec *pspec, PeasEngine *engine)
{
	PeasPluginInfo *info;
	gboolean active;

	info = g_object_get_data (G_OBJECT (object), "peas-plugin-info");
	if (info == NULL)
		return;

	active = adw_switch_row_get_active (ADW_SWITCH_ROW (object));
	if (active) {
		peas_engine_load_plugin (engine, info);
	} else {
		peas_engine_unload_plugin (engine, info);
	}
}

static void
build_plugins_page (RBShellPreferences *prefs)
{
	AdwPreferencesPage *page;
	AdwPreferencesGroup *group;
	PeasEngine *engine;
	guint n_plugins;

	page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());
	adw_preferences_page_set_title (page, _("Plugins"));
	adw_preferences_page_set_icon_name (page, "application-x-addon-symbolic");

	group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	adw_preferences_group_set_title (group, _("Extensions"));
	adw_preferences_group_set_description (group, _("Enable or disable plugins"));

	engine = peas_engine_get_default ();
	n_plugins = g_list_model_get_n_items (G_LIST_MODEL (engine));

	for (guint i = 0; i < n_plugins; i++) {
		PeasPluginInfo *info;
		AdwSwitchRow *row;
		const char *plugin_name;
		const char *plugin_desc;
		const char *icon_name;
		GtkWidget *icon;

		info = g_list_model_get_item (G_LIST_MODEL (engine), i);
		if (peas_plugin_info_is_hidden (info)) {
			g_object_unref (info);
			continue;
		}

		plugin_name = peas_plugin_info_get_name (info);
		plugin_desc = peas_plugin_info_get_description (info);
		icon_name = peas_plugin_info_get_icon_name (info);

		row = ADW_SWITCH_ROW (adw_switch_row_new ());
		adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row),
					       plugin_name ? plugin_name : "");
		if (plugin_desc != NULL && plugin_desc[0] != '\0')
			adw_action_row_set_subtitle (ADW_ACTION_ROW (row), plugin_desc);

		/* icon as prefix */
		icon = gtk_image_new_from_icon_name (
			(icon_name != NULL) ? icon_name : "application-x-addon");
		gtk_image_set_pixel_size (GTK_IMAGE (icon), 32);
		adw_action_row_add_prefix (ADW_ACTION_ROW (row), icon);

		adw_switch_row_set_active (row, peas_plugin_info_is_loaded (info));
		if (peas_plugin_info_is_builtin (info))
			gtk_widget_set_sensitive (GTK_WIDGET (row), FALSE);

		g_object_set_data (G_OBJECT (row), "peas-plugin-info", info);
		g_signal_connect (row, "notify::active",
				  G_CALLBACK (plugin_switch_toggled_cb), engine);

		adw_preferences_group_add (group, GTK_WIDGET (row));
		g_object_unref (info);
	}

	adw_preferences_page_add (page, group);
	adw_preferences_dialog_add (ADW_PREFERENCES_DIALOG (prefs), page);
}

/* ---- Init / finalize ---- */

static void
rb_shell_preferences_init (RBShellPreferences *prefs)
{
	prefs->priv = rb_shell_preferences_get_instance_private (prefs);

	prefs->priv->source_settings = g_settings_new ("org.gnome.rhythmbox.sources");
	prefs->priv->player_settings = g_settings_new ("org.gnome.rhythmbox.player");
	prefs->priv->main_settings = g_settings_new ("org.gnome.rhythmbox");

	/* Build pages */
	build_general_page (prefs);
	build_playback_page (prefs);

	/* Connect GSettings signals */
	g_signal_connect_object (prefs->priv->source_settings, "changed",
				 G_CALLBACK (source_settings_changed_cb), prefs, 0);
	source_settings_changed_cb (prefs->priv->source_settings, "visible-columns", prefs);
	source_settings_changed_cb (prefs->priv->source_settings, "browser-views", prefs);

	/* Playback: bind xfade toggle to GSettings */
	g_settings_bind (prefs->priv->player_settings, "use-xfade-backend",
			 prefs->priv->xfade_row, "active",
			 G_SETTINGS_BIND_DEFAULT);

	g_signal_connect (prefs->priv->xfade_row, "notify::active",
			  G_CALLBACK (xfade_row_changed_cb), prefs);
	xfade_row_changed_cb (G_OBJECT (prefs->priv->xfade_row), NULL, prefs);

	/* Playback: transition time */
	g_signal_connect_object (prefs->priv->player_settings, "changed",
				 G_CALLBACK (player_settings_changed_cb), prefs, 0);
	player_settings_changed_cb (prefs->priv->player_settings, "transition-time", prefs);

	g_signal_connect_object (prefs->priv->transition_scale, "value-changed",
				 G_CALLBACK (transition_time_changed_cb), prefs, 0);
}

static void
impl_dispose (GObject *object)
{
	RBShellPreferences *prefs = RB_SHELL_PREFERENCES (object);

	if (prefs->priv->main_settings != NULL) {
		g_object_unref (prefs->priv->main_settings);
		prefs->priv->main_settings = NULL;
	}

	if (prefs->priv->source_settings != NULL) {
		g_object_unref (prefs->priv->source_settings);
		prefs->priv->source_settings = NULL;
	}

	if (prefs->priv->player_settings != NULL) {
		rb_settings_delayed_sync (prefs->priv->player_settings, NULL, NULL, NULL);
		g_object_unref (prefs->priv->player_settings);
		prefs->priv->player_settings = NULL;
	}

	G_OBJECT_CLASS (rb_shell_preferences_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RBShellPreferences *prefs = RB_SHELL_PREFERENCES (object);

	g_clear_pointer (&prefs->priv->column_switches, g_hash_table_destroy);

	G_OBJECT_CLASS (rb_shell_preferences_parent_class)->finalize (object);
}

/* ---- Public API ---- */

/**
 * rb_shell_preferences_append_page:
 * @prefs: the #RBShellPreferences instance
 * @name: name of the page to append
 * @widget: the #GtkWidget to use as the contents of the page
 *
 * Wraps a widget in an AdwPreferencesPage and adds it to the dialog.
 */
void
rb_shell_preferences_append_page (RBShellPreferences *prefs,
				  const char *name,
				  GtkWidget *widget)
{
	AdwPreferencesPage *page;
	AdwPreferencesGroup *group;

	page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());
	adw_preferences_page_set_title (page, name);
	adw_preferences_page_set_icon_name (page, "folder-music-symbolic");

	group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	adw_preferences_group_add (group, widget);
	adw_preferences_page_add (page, group);

	adw_preferences_dialog_add (ADW_PREFERENCES_DIALOG (prefs), page);
}

static void
rb_shell_preferences_append_view_page (RBShellPreferences *prefs,
				       const char *name,
				       RBDisplayPage *page)
{
	GtkWidget *widget;

	g_return_if_fail (RB_IS_SHELL_PREFERENCES (prefs));
	g_return_if_fail (RB_IS_DISPLAY_PAGE (page));

	widget = rb_display_page_get_config_widget (page, prefs);
	if (!widget)
		return;

	rb_shell_preferences_append_page (prefs, name, widget);
}

/**
 * rb_shell_preferences_new:
 * @views: (element-type RB.Source) (transfer none): list of sources to check for preferences pages
 *
 * Creates the #RBShellPreferences instance, populating it with the
 * preferences pages for the sources in the list.
 *
 * Return value: the #RBShellPreferences instance
 */
GtkWidget *
rb_shell_preferences_new (GList *views)
{
	RBShellPreferences *prefs;

	prefs = g_object_new (RB_TYPE_SHELL_PREFERENCES, NULL);

	g_return_val_if_fail (prefs->priv != NULL, NULL);

	/* Add source-specific pages (Library, Podcasts, etc.) */
	for (; views; views = views->next) {
		char *name = NULL;
		g_object_get (views->data, "name", &name, NULL);
		if (name == NULL) {
			g_warning ("Page %p of type %s has no name",
				   views->data,
				   G_OBJECT_TYPE_NAME (views->data));
			continue;
		}
		rb_shell_preferences_append_view_page (prefs, name, RB_DISPLAY_PAGE (views->data));
		g_free (name);
	}

	/* Plugins page goes last */
	build_plugins_page (prefs);

	return GTK_WIDGET (prefs);
}

/**
 * rb_shell_preferences_add_widget:
 * @prefs: the #RBShellPreferences
 * @widget: the #GtkWidget to insert into the preferences window
 * @location: the location at which to insert the widget
 * @expand: whether the widget should be given extra space (unused with Adw)
 * @fill: whether the widget should fill all space allocated to it (unused with Adw)
 *
 * Adds a widget to the General or Playback preferences page plugin group.
 */
void
rb_shell_preferences_add_widget (RBShellPreferences *prefs,
				 GtkWidget *widget,
				 RBShellPrefsUILocation location,
				 gboolean expand,
				 gboolean fill)
{
	AdwPreferencesGroup *group;

	switch (location) {
	case RB_SHELL_PREFS_UI_LOCATION_GENERAL:
		group = prefs->priv->general_plugin_group;
		break;
	case RB_SHELL_PREFS_UI_LOCATION_PLAYBACK:
		group = prefs->priv->playback_plugin_group;
		break;
	default:
		g_assert_not_reached ();
	}

	adw_preferences_group_add (group, widget);
	gtk_widget_set_visible (GTK_WIDGET (group), TRUE);
}

/**
 * rb_shell_preferences_remove_widget:
 * @prefs: the #RBShellPreferences
 * @widget: the #GtkWidget to remove from the preferences window
 * @location: the UI location to which the widget was originally added
 *
 * Removes a widget added with #rb_shell_preferences_add_widget from the preferences window.
 */
void
rb_shell_preferences_remove_widget (RBShellPreferences *prefs,
				    GtkWidget *widget,
				    RBShellPrefsUILocation location)
{
	AdwPreferencesGroup *group;

	switch (location) {
	case RB_SHELL_PREFS_UI_LOCATION_GENERAL:
		group = prefs->priv->general_plugin_group;
		break;
	case RB_SHELL_PREFS_UI_LOCATION_PLAYBACK:
		group = prefs->priv->playback_plugin_group;
		break;
	default:
		g_assert_not_reached ();
	}

	adw_preferences_group_remove (group, widget);
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

/**
 * RBShellPrefsUILocation:
 * @RB_SHELL_PREFS_UI_LOCATION_GENERAL: The "general" preferences page
 * @RB_SHELL_PREFS_UI_LOCATION_PLAYBACK: The "playback" preferences page
 *
 * Locations available for adding new widgets to the preferences dialog.
 */
GType
rb_shell_prefs_ui_location_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_SHELL_PREFS_UI_LOCATION_GENERAL, "general"),
			ENUM_ENTRY (RB_SHELL_PREFS_UI_LOCATION_PLAYBACK, "playback"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBShellPrefsUILocation", values);
	}

	return etype;
}
