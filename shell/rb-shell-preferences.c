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
 * The preferences dialog uses #AdwDialog with an #AdwViewStack and
 * #AdwViewSwitcher in the header bar for top-positioned page tabs.
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
#include "rb-peas-gtk-configurable.h"

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
	AdwViewStack *stack;

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


G_DEFINE_TYPE_WITH_PRIVATE (RBShellPreferences, rb_shell_preferences, ADW_TYPE_DIALOG)

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

/* ---- Helper: wrap content in a scrollable AdwPreferencesPage ---- */

static GtkWidget *
wrap_in_preferences_page (void)
{
	return adw_preferences_page_new ();
}

/* ---- Build General page ---- */

static GtkWidget *
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

	page = ADW_PREFERENCES_PAGE (wrap_in_preferences_page ());

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

	return GTK_WIDGET (page);
}

/* ---- Build Playback page ---- */

static GtkWidget *
build_playback_page (RBShellPreferences *prefs)
{
	AdwPreferencesPage *page;
	AdwPreferencesGroup *xfade_group;
	AdwActionRow *duration_row;
	GtkAdjustment *adj;

	page = ADW_PREFERENCES_PAGE (wrap_in_preferences_page ());

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

	return GTK_WIDGET (page);
}

/* ---- Plugins page ---- */

static gboolean
plugin_is_configurable (PeasEngine *engine, PeasPluginInfo *info)
{
	if (info == NULL || !peas_plugin_info_is_loaded (info))
		return FALSE;

	return peas_engine_provides_extension (engine, info,
					       PEAS_GTK_TYPE_CONFIGURABLE);
}

static void
plugin_switch_toggled_cb (GObject *object, GParamSpec *pspec, PeasEngine *engine)
{
	PeasPluginInfo *info;
	GtkWidget *configure_button;
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

	/* update configure button visibility and sensitivity */
	configure_button = g_object_get_data (G_OBJECT (object), "configure-button");
	if (configure_button != NULL) {
		if (active && plugin_is_configurable (engine, info)) {
			gtk_widget_set_visible (configure_button, TRUE);
			gtk_widget_set_sensitive (configure_button, TRUE);
		} else if (active) {
			/* loaded but not configurable */
			gtk_widget_set_visible (configure_button, FALSE);
		} else {
			/* plugin disabled — keep visible if it was shown, but disable */
			if (gtk_widget_get_visible (configure_button))
				gtk_widget_set_sensitive (configure_button, FALSE);
		}
	}
}

static void
plugin_configure_button_cb (GtkButton *button, gpointer user_data)
{
	PeasPluginInfo *info;
	PeasEngine *engine;
	GObject *exten;
	GtkWidget *widget;
	AdwDialog *dialog;
	const char *name;

	info = g_object_get_data (G_OBJECT (button), "peas-plugin-info");
	if (info == NULL || !peas_plugin_info_is_loaded (info))
		return;

	engine = peas_engine_get_default ();
	exten = peas_engine_create_extension (engine, info,
					      PEAS_GTK_TYPE_CONFIGURABLE,
					      NULL);
	if (exten == NULL)
		return;

	widget = peas_gtk_configurable_create_configure_widget (
			PEAS_GTK_CONFIGURABLE (exten));
	g_object_unref (exten);

	if (widget == NULL)
		return;

	name = peas_plugin_info_get_name (info);

	dialog = adw_dialog_new ();
	adw_dialog_set_title (dialog, name ? name : "");
	adw_dialog_set_content_width (dialog, 400);
	adw_dialog_set_content_height (dialog, 300);

	AdwToolbarView *toolbar_view = ADW_TOOLBAR_VIEW (adw_toolbar_view_new ());
	adw_toolbar_view_add_top_bar (toolbar_view, adw_header_bar_new ());
	adw_toolbar_view_set_content (toolbar_view, widget);
	adw_dialog_set_child (dialog, GTK_WIDGET (toolbar_view));

	adw_dialog_present (dialog, GTK_WIDGET (button));
}

static void
plugin_about_button_cb (GtkButton *button, gpointer user_data)
{
	PeasPluginInfo *info;
	AdwDialog *about;
	const char *name;
	const char *description;
	const char * const *authors;
	const char *copyright;
	const char *version;
	const char *website;
	const char *icon_name;

	info = g_object_get_data (G_OBJECT (button), "peas-plugin-info");
	if (info == NULL)
		return;

	name = peas_plugin_info_get_name (info);
	description = peas_plugin_info_get_description (info);
	authors = peas_plugin_info_get_authors (info);
	copyright = peas_plugin_info_get_copyright (info);
	version = peas_plugin_info_get_version (info);
	website = peas_plugin_info_get_website (info);
	icon_name = peas_plugin_info_get_icon_name (info);

	about = adw_about_dialog_new ();
	adw_about_dialog_set_application_name (ADW_ABOUT_DIALOG (about),
					       name ? name : "");
	adw_about_dialog_set_application_icon (ADW_ABOUT_DIALOG (about),
					       icon_name ? icon_name : "application-x-addon");
	if (description != NULL)
		adw_about_dialog_set_comments (ADW_ABOUT_DIALOG (about), description);
	if (authors != NULL)
		adw_about_dialog_set_developers (ADW_ABOUT_DIALOG (about),
						 (const char **)authors);
	if (copyright != NULL)
		adw_about_dialog_set_copyright (ADW_ABOUT_DIALOG (about), copyright);
	if (version != NULL)
		adw_about_dialog_set_version (ADW_ABOUT_DIALOG (about), version);
	if (website != NULL)
		adw_about_dialog_set_website (ADW_ABOUT_DIALOG (about), website);

	adw_dialog_present (about, GTK_WIDGET (button));
}

static int
compare_plugin_info (gconstpointer a, gconstpointer b)
{
	PeasPluginInfo *info_a = *(PeasPluginInfo **)a;
	PeasPluginInfo *info_b = *(PeasPluginInfo **)b;
	const char *name_a = peas_plugin_info_get_name (info_a);
	const char *name_b = peas_plugin_info_get_name (info_b);
	return g_utf8_collate (name_a ? name_a : "", name_b ? name_b : "");
}

static GtkWidget *
build_plugins_page (RBShellPreferences *prefs)
{
	AdwPreferencesPage *page;
	AdwPreferencesGroup *group;
	PeasEngine *engine;
	guint n_plugins;

	page = ADW_PREFERENCES_PAGE (wrap_in_preferences_page ());

	group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	adw_preferences_group_set_title (group, _("Extensions"));
	adw_preferences_group_set_description (group, _("Enable or disable plugins"));

	engine = peas_engine_get_default ();
	n_plugins = g_list_model_get_n_items (G_LIST_MODEL (engine));

	/* collect non-hidden plugins and sort alphabetically */
	GPtrArray *plugins = g_ptr_array_new_with_free_func (g_object_unref);
	for (guint i = 0; i < n_plugins; i++) {
		PeasPluginInfo *info = g_list_model_get_item (G_LIST_MODEL (engine), i);
		if (!peas_plugin_info_is_hidden (info))
			g_ptr_array_add (plugins, info);
		else
			g_object_unref (info);
	}
	g_ptr_array_sort (plugins, (GCompareFunc) compare_plugin_info);

	for (guint i = 0; i < plugins->len; i++) {
		PeasPluginInfo *info = g_ptr_array_index (plugins, i);
		AdwActionRow *row;
		const char *plugin_name;
		const char *plugin_desc;
		const char *icon_name;
		GtkWidget *icon;
		GtkWidget *configure_button;
		GtkWidget *about_button;
		gboolean builtin;
		gboolean loaded;

		plugin_name = peas_plugin_info_get_name (info);
		plugin_desc = peas_plugin_info_get_description (info);
		icon_name = peas_plugin_info_get_icon_name (info);
		builtin = peas_plugin_info_is_builtin (info);
		loaded = peas_plugin_info_is_loaded (info);

		/* use AdwSwitchRow for togglable plugins, plain AdwActionRow for builtins */
		if (builtin)
			row = ADW_ACTION_ROW (adw_action_row_new ());
		else
			row = ADW_ACTION_ROW (adw_switch_row_new ());

		adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row),
					       plugin_name ? plugin_name : "");
		if (plugin_desc != NULL && plugin_desc[0] != '\0')
			adw_action_row_set_subtitle (row, plugin_desc);

		/* icon as prefix */
		icon = gtk_image_new_from_icon_name (
			(icon_name != NULL) ? icon_name : "application-x-addon");
		gtk_image_set_pixel_size (GTK_IMAGE (icon), 32);
		adw_action_row_add_prefix (row, icon);

		/* configure button as suffix (before about) */
		configure_button = gtk_button_new_from_icon_name ("emblem-system-symbolic");
		gtk_widget_add_css_class (configure_button, "flat");
		gtk_widget_set_valign (configure_button, GTK_ALIGN_CENTER);
		gtk_widget_set_tooltip_text (configure_button, _("Configure plugin"));
		g_object_set_data (G_OBJECT (configure_button), "peas-plugin-info", info);
		g_signal_connect (configure_button, "clicked",
				  G_CALLBACK (plugin_configure_button_cb), NULL);
		adw_action_row_add_suffix (row, configure_button);

		if (builtin) {
			/* builtin plugins can't be configured */
			gtk_widget_set_visible (configure_button, FALSE);
		} else if (loaded && plugin_is_configurable (engine, info)) {
			gtk_widget_set_visible (configure_button, TRUE);
			gtk_widget_set_sensitive (configure_button, TRUE);
		} else if (loaded) {
			/* loaded but not configurable — hide entirely */
			gtk_widget_set_visible (configure_button, FALSE);
		} else {
			/* not loaded — we can't check, hide for now */
			gtk_widget_set_visible (configure_button, FALSE);
		}

		/* about button as suffix */
		about_button = gtk_button_new_from_icon_name ("help-about-symbolic");
		gtk_widget_add_css_class (about_button, "flat");
		gtk_widget_set_valign (about_button, GTK_ALIGN_CENTER);
		gtk_widget_set_tooltip_text (about_button, _("About this plugin"));
		g_object_set_data (G_OBJECT (about_button), "peas-plugin-info", info);
		g_signal_connect (about_button, "clicked",
				  G_CALLBACK (plugin_about_button_cb), NULL);
		adw_action_row_add_suffix (row, about_button);

		if (!builtin) {
			adw_switch_row_set_active (ADW_SWITCH_ROW (row),
						   loaded);
			g_object_set_data (G_OBJECT (row), "peas-plugin-info", info);
			g_object_set_data (G_OBJECT (row), "configure-button", configure_button);
			g_signal_connect (row, "notify::active",
					  G_CALLBACK (plugin_switch_toggled_cb), engine);
		}

		adw_preferences_group_add (group, GTK_WIDGET (row));
	}

	g_ptr_array_unref (plugins);
	adw_preferences_page_add (page, group);
	return GTK_WIDGET (page);
}

/* ---- Init / finalize ---- */

static void
rb_shell_preferences_init (RBShellPreferences *prefs)
{
	AdwViewSwitcher *switcher;
	AdwToolbarView *toolbar_view;
	AdwHeaderBar *header_bar;
	GtkWidget *general_widget;
	GtkWidget *playback_widget;

	prefs->priv = rb_shell_preferences_get_instance_private (prefs);

	prefs->priv->source_settings = g_settings_new ("org.gnome.rhythmbox.sources");
	prefs->priv->player_settings = g_settings_new ("org.gnome.rhythmbox.player");
	prefs->priv->main_settings = g_settings_new ("org.gnome.rhythmbox");

	/* Create view stack */
	prefs->priv->stack = ADW_VIEW_STACK (adw_view_stack_new ());

	/* Build pages and add to stack */
	general_widget = build_general_page (prefs);
	adw_view_stack_add_titled_with_icon (prefs->priv->stack,
					     general_widget,
					     "general",
					     _("General"),
					     "preferences-other-symbolic");

	playback_widget = build_playback_page (prefs);
	adw_view_stack_add_titled_with_icon (prefs->priv->stack,
					     playback_widget,
					     "playback",
					     _("Playback"),
					     "media-playback-start-symbolic");

	/* Header bar with view switcher */
	switcher = ADW_VIEW_SWITCHER (adw_view_switcher_new ());
	adw_view_switcher_set_stack (switcher, prefs->priv->stack);
	adw_view_switcher_set_policy (switcher, ADW_VIEW_SWITCHER_POLICY_WIDE);

	header_bar = ADW_HEADER_BAR (adw_header_bar_new ());
	adw_header_bar_set_title_widget (header_bar, GTK_WIDGET (switcher));

	/* Toolbar view: header on top, stack as content */
	toolbar_view = ADW_TOOLBAR_VIEW (adw_toolbar_view_new ());
	adw_toolbar_view_add_top_bar (toolbar_view, GTK_WIDGET (header_bar));
	adw_toolbar_view_set_content (toolbar_view, GTK_WIDGET (prefs->priv->stack));

	adw_dialog_set_child (ADW_DIALOG (prefs), GTK_WIDGET (toolbar_view));
	adw_dialog_set_title (ADW_DIALOG (prefs), _("Preferences"));
	adw_dialog_set_content_width (ADW_DIALOG (prefs), 700);
	adw_dialog_set_content_height (ADW_DIALOG (prefs), 580);

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
 * @icon_name: icon name for the page tab
 * @widget: the #GtkWidget to use as the contents of the page
 *
 * Wraps a widget in an AdwPreferencesPage and adds it as a view stack page.
 */
void
rb_shell_preferences_append_page (RBShellPreferences *prefs,
				  const char *name,
				  const char *icon_name,
				  GtkWidget *widget)
{
	AdwPreferencesPage *page;
	AdwPreferencesGroup *group;

	page = ADW_PREFERENCES_PAGE (wrap_in_preferences_page ());

	group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	adw_preferences_group_add (group, widget);
	adw_preferences_page_add (page, group);

	adw_view_stack_add_titled_with_icon (prefs->priv->stack,
					     GTK_WIDGET (page),
					     NULL,
					     name,
					     icon_name);
}

static void
rb_shell_preferences_append_view_page (RBShellPreferences *prefs,
				       const char *name,
				       RBDisplayPage *page)
{
	GtkWidget *widget;
	GIcon *icon = NULL;
	const char *icon_name = "folder-music-symbolic";

	g_return_if_fail (RB_IS_SHELL_PREFERENCES (prefs));
	g_return_if_fail (RB_IS_DISPLAY_PAGE (page));

	widget = rb_display_page_get_config_widget (page, prefs);
	if (!widget)
		return;

	g_object_get (page, "icon", &icon, NULL);
	if (icon != NULL && G_IS_THEMED_ICON (icon)) {
		const char * const *names = g_themed_icon_get_names (G_THEMED_ICON (icon));
		if (names != NULL && names[0] != NULL)
			icon_name = names[0];
	}

	rb_shell_preferences_append_page (prefs, name, icon_name, widget);

	g_clear_object (&icon);
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
	GtkWidget *plugins_widget;

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
	plugins_widget = build_plugins_page (prefs);
	adw_view_stack_add_titled_with_icon (prefs->priv->stack,
					     plugins_widget,
					     "plugins",
					     _("Plugins"),
					     "application-x-addon-symbolic");

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
