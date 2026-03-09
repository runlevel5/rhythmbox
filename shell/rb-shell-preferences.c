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
 * The preferences dialog is built around a #GtkNotebook widget, with two built-in
 * pages and additional pages for various sources.
 *
 * The 'general' preferences page controls the set of browser views that are visible
 * (artist and album; genre and artist; or genre, artist, and album), the columns
 * that are visible, and the appearance of buttons in the main toolbar.  The browser
 * and column settings apply to all sources.
 *
 * The 'playback' preferences page controls whether the crossfading player backend is used,
 * and if enabled, the crossfade duration and network buffer size.
 *
 * Currently, the library and podcast sources add pages to the notebook, for configuring the
 * location and layout of the library and the podcast download location and update frequency.
 */

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

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
static gboolean rb_shell_preferences_window_close_request_cb (GtkWindow *window,
				                               RBShellPreferences *shell_preferences);
static void rb_shell_preferences_response_cb (GtkDialog *dialog,
				              int response_id,
				              RBShellPreferences *shell_preferences);

void rb_shell_preferences_column_check_changed_cb (GtkCheckButton *butt,
						   RBShellPreferences *shell_preferences);
void rb_shell_preferences_browser_views_activated_cb (GtkWidget *widget,
						      RBShellPreferences *shell_preferences);

static void column_check_toggled_cb (GtkWidget *widget, RBShellPreferences *preferences);

static void player_settings_changed_cb (GSettings *settings, const char *key, RBShellPreferences *preferences);
static void source_settings_changed_cb (GSettings *settings, const char *key, RBShellPreferences *preferences);
static void transition_time_changed_cb (GtkRange *range, RBShellPreferences *preferences);
static void plugin_check_toggled_cb (GtkCheckButton *check, PeasEngine *engine);

enum
{
	PROP_0,
};

#define COLUMN_CHECK_PROP_NAME	"rb-column-prop-name"

struct {
	const char *widget;
	RhythmDBPropType prop;
} column_checks[] = {
	{ "track_check",	RHYTHMDB_PROP_TRACK_NUMBER },
	{ "artist_check",	RHYTHMDB_PROP_ARTIST },
	{ "composer_check",	RHYTHMDB_PROP_COMPOSER },
	{ "album_check",	RHYTHMDB_PROP_ALBUM },
	{ "year_check",		RHYTHMDB_PROP_DATE },
	{ "last_played_check",	RHYTHMDB_PROP_LAST_PLAYED },
	{ "genre_check",	RHYTHMDB_PROP_GENRE },
	{ "first_seen_check",	RHYTHMDB_PROP_FIRST_SEEN },
	{ "play_count_check",	RHYTHMDB_PROP_PLAY_COUNT },
	{ "comment_check",	RHYTHMDB_PROP_COMMENT },
	{ "bpm_check",		RHYTHMDB_PROP_BPM },
	{ "rating_check",	RHYTHMDB_PROP_RATING },
	{ "duration_check",	RHYTHMDB_PROP_DURATION },
	{ "location_check",	RHYTHMDB_PROP_LOCATION },
	{ "quality_check",	RHYTHMDB_PROP_BITRATE }
};


struct RBShellPreferencesPrivate
{
	GtkWidget *notebook;

	GHashTable *column_checks;
	GtkWidget *general_prefs_plugin_box;

	GtkWidget *xfade_backend_check;
	GtkWidget *transition_duration;
	GtkWidget *playback_prefs_plugin_box;

	GSList *browser_views_group;

	gboolean applying_settings;

	GSettings *main_settings;
	GSettings *source_settings;
	GSettings *player_settings;
};


G_DEFINE_TYPE_WITH_PRIVATE (RBShellPreferences, rb_shell_preferences, GTK_TYPE_DIALOG)

static void
rb_shell_preferences_class_init (RBShellPreferencesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = impl_finalize;
	object_class->dispose = impl_dispose;

}

static void
help_cb (GtkWidget *widget,
	 RBShellPreferences *shell_preferences)
{
	GtkWindow *window = GTK_WINDOW (gtk_widget_get_root (widget));
	gtk_show_uri (window, "help:rhythmbox/prefs", GDK_CURRENT_TIME);
}

static void
rb_shell_preferences_init (RBShellPreferences *shell_preferences)
{
	GtkWidget *tmp;
	GtkWidget *content_area;
	GtkBuilder *builder;
	int i;

	shell_preferences->priv = rb_shell_preferences_get_instance_private (shell_preferences);

	g_signal_connect_object (shell_preferences,
				 "close-request",
				 G_CALLBACK (rb_shell_preferences_window_close_request_cb),
				 shell_preferences, 0);
	g_signal_connect_object (shell_preferences,
				 "response",
				 G_CALLBACK (rb_shell_preferences_response_cb),
				 shell_preferences, 0);

	gtk_dialog_add_button (GTK_DIALOG (shell_preferences),
			       _("_Close"),
			       GTK_RESPONSE_CLOSE);
	tmp = gtk_dialog_add_button (GTK_DIALOG (shell_preferences),
			              _("_Help"),
			              GTK_RESPONSE_HELP);
	g_signal_connect_object (tmp, "clicked",
				 G_CALLBACK (help_cb), shell_preferences, 0);
	gtk_dialog_set_default_response (GTK_DIALOG (shell_preferences),
					 GTK_RESPONSE_CLOSE);

	gtk_window_set_title (GTK_WINDOW (shell_preferences), _("Rhythmbox Preferences"));
	gtk_window_set_resizable (GTK_WINDOW (shell_preferences), FALSE);

	shell_preferences->priv->notebook = GTK_WIDGET (gtk_notebook_new ());

	content_area = gtk_dialog_get_content_area (GTK_DIALOG (shell_preferences));
	gtk_box_append (GTK_BOX (content_area),
			shell_preferences->priv->notebook);

	gtk_box_set_spacing (GTK_BOX (content_area), 2);

	shell_preferences->priv->source_settings = g_settings_new ("org.gnome.rhythmbox.sources");

	builder = rb_builder_load ("general-prefs.ui", shell_preferences);

	rb_builder_boldify_label (builder, "visible_columns_label");

	/* Columns */
	shell_preferences->priv->column_checks = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < G_N_ELEMENTS (column_checks); i++) {
		GtkWidget *widget;
		const char *name;

		widget = GTK_WIDGET (gtk_builder_get_object (builder, column_checks[i].widget));
		/* XXX kind of nasty, we know rhythmdb_nice_elt_name_from_propid doesn't actually use the db */
		name = (const char *)rhythmdb_nice_elt_name_from_propid (NULL, column_checks[i].prop);
		g_assert (name != NULL);

		g_signal_connect_object (widget, "toggled", G_CALLBACK (column_check_toggled_cb), shell_preferences, 0);
		g_object_set_data (G_OBJECT (widget), COLUMN_CHECK_PROP_NAME, (gpointer)name);

		g_hash_table_insert (shell_preferences->priv->column_checks, (gpointer)name, widget);
	}

	/* browser options */
	rb_builder_boldify_label (builder, "browser_views_label");

	/* In GTK4, radio buttons are GtkCheckButton with set_group.
	 * Build the group list manually from builder objects. */
	shell_preferences->priv->browser_views_group = NULL;
	shell_preferences->priv->browser_views_group =
		g_slist_append (shell_preferences->priv->browser_views_group,
			GTK_WIDGET (gtk_builder_get_object (builder, "library_browser_views_radio")));
	shell_preferences->priv->browser_views_group =
		g_slist_append (shell_preferences->priv->browser_views_group,
			GTK_WIDGET (gtk_builder_get_object (builder, "radio2")));
	shell_preferences->priv->browser_views_group =
		g_slist_append (shell_preferences->priv->browser_views_group,
			GTK_WIDGET (gtk_builder_get_object (builder, "radio3")));

	gtk_notebook_append_page (GTK_NOTEBOOK (shell_preferences->priv->notebook),
				  GTK_WIDGET (gtk_builder_get_object (builder, "general_vbox")),
				  gtk_label_new (_("General")));

	g_signal_connect_object (shell_preferences->priv->source_settings,
				 "changed",
				 G_CALLBACK (source_settings_changed_cb),
				 shell_preferences, 0);
	source_settings_changed_cb (shell_preferences->priv->source_settings,
				    "visible-columns",
				    shell_preferences);
	source_settings_changed_cb (shell_preferences->priv->source_settings,
				    "browser-views",
				    shell_preferences);

	shell_preferences->priv->main_settings = g_settings_new ("org.gnome.rhythmbox");

	/* box for stuff added by plugins */
	shell_preferences->priv->general_prefs_plugin_box =
		GTK_WIDGET (gtk_builder_get_object (builder, "plugin_box"));

	g_object_unref (builder);
	builder = rb_builder_load ("playback-prefs.ui", shell_preferences);

	/* playback preferences */
	rb_builder_boldify_label (builder, "backend_label");
	rb_builder_boldify_label (builder, "duration_label");

	shell_preferences->priv->xfade_backend_check =
		GTK_WIDGET (gtk_builder_get_object (builder, "use_xfade_backend"));
	shell_preferences->priv->transition_duration =
		GTK_WIDGET (gtk_builder_get_object (builder, "duration"));
	shell_preferences->priv->playback_prefs_plugin_box =
		GTK_WIDGET (gtk_builder_get_object (builder, "plugin_box"));


	shell_preferences->priv->player_settings = g_settings_new ("org.gnome.rhythmbox.player");
	g_signal_connect_object (shell_preferences->priv->player_settings,
				 "changed",
				 G_CALLBACK (player_settings_changed_cb),
				 shell_preferences, 0);
	player_settings_changed_cb (shell_preferences->priv->player_settings,
				    "transition-time",
				    shell_preferences);


	g_settings_bind (shell_preferences->priv->player_settings,
			 "use-xfade-backend",
			 shell_preferences->priv->xfade_backend_check,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);

	/* unfortunately the GtkRange value can't be bound to a GSettings key.. */
	g_settings_bind (shell_preferences->priv->player_settings,
			 "use-xfade-backend",
			 shell_preferences->priv->transition_duration,
			 "sensitive",
			 G_SETTINGS_BIND_GET | G_SETTINGS_BIND_SET | G_SETTINGS_BIND_NO_SENSITIVITY);

	g_signal_connect_object (gtk_builder_get_object (builder, "duration"),
				 "value-changed",
				 G_CALLBACK (transition_time_changed_cb),
				 shell_preferences, 0);

	gtk_notebook_append_page (GTK_NOTEBOOK (shell_preferences->priv->notebook),
				  GTK_WIDGET (gtk_builder_get_object (builder, "playback_prefs_box")),
				  gtk_label_new (_("Playback")));
	g_object_unref (builder);
}

static void
impl_dispose (GObject *object)
{
	RBShellPreferences *shell_preferences = RB_SHELL_PREFERENCES (object);

	if (shell_preferences->priv->main_settings != NULL) {
		g_object_unref (shell_preferences->priv->main_settings);
		shell_preferences->priv->main_settings = NULL;
	}

	if (shell_preferences->priv->source_settings != NULL) {
		g_object_unref (shell_preferences->priv->source_settings);
		shell_preferences->priv->source_settings = NULL;
	}

	if (shell_preferences->priv->player_settings != NULL) {
		rb_settings_delayed_sync (shell_preferences->priv->player_settings, NULL, NULL, NULL);
		g_object_unref (shell_preferences->priv->player_settings);
		shell_preferences->priv->player_settings = NULL;
	}

	G_OBJECT_CLASS (rb_shell_preferences_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	/*RBShellPreferences *shell_preferences = RB_SHELL_PREFERENCES (object);*/

	/* anything to do here? */

	G_OBJECT_CLASS (rb_shell_preferences_parent_class)->finalize (object);
}

/**
 * rb_shell_preferences_append_page:
 * @prefs: the #RBShellPreferences instance
 * @name: name of the page to append
 * @widget: the #GtkWidget to use as the contents of the page
 *
 * Appends a new page to the preferences dialog notebook.
 */
void
rb_shell_preferences_append_page (RBShellPreferences *prefs,
				  const char *name,
				  GtkWidget *widget)
{
	GtkWidget *label;

	label = gtk_label_new (name);
	gtk_notebook_append_page (GTK_NOTEBOOK (prefs->priv->notebook),
				  widget,
				  label);
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


static void
plugin_check_toggled_cb (GtkCheckButton *check, PeasEngine *engine)
{
	PeasPluginInfo *info;

	info = g_object_get_data (G_OBJECT (check), "peas-plugin-info");
	if (info == NULL)
		return;

	if (gtk_check_button_get_active (check)) {
		peas_engine_load_plugin (engine, info);
	} else {
		peas_engine_unload_plugin (engine, info);
	}
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
	RBShellPreferences *shell_preferences;

	shell_preferences = g_object_new (RB_TYPE_SHELL_PREFERENCES,
				          NULL, NULL);

	g_return_val_if_fail (shell_preferences->priv != NULL, NULL);

	for (; views; views = views->next)
	{
		char *name = NULL;
		g_object_get (views->data, "name", &name, NULL);
		if (name == NULL) {
			g_warning ("Page %p of type %s has no name",
				   views->data,
				   G_OBJECT_TYPE_NAME (views->data));
			continue;
		}
		rb_shell_preferences_append_view_page (shell_preferences,
						       name,
						       RB_DISPLAY_PAGE (views->data));
		g_free (name);
	}

	/* make sure this goes last */
	{
		GtkWidget *plugins_page;
		GtkWidget *plugins_scroll;
		GtkWidget *plugins_list;
		PeasEngine *engine;
		guint n_plugins;
		guint i;

		engine = peas_engine_get_default ();

		plugins_page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
		gtk_widget_set_margin_start (plugins_page, 12);
		gtk_widget_set_margin_end (plugins_page, 12);
		gtk_widget_set_margin_top (plugins_page, 12);
		gtk_widget_set_margin_bottom (plugins_page, 12);

		plugins_scroll = gtk_scrolled_window_new ();
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (plugins_scroll),
						GTK_POLICY_NEVER,
						GTK_POLICY_AUTOMATIC);
		gtk_widget_set_vexpand (plugins_scroll, TRUE);
		gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (plugins_scroll), 300);

		plugins_list = gtk_list_box_new ();
		gtk_list_box_set_selection_mode (GTK_LIST_BOX (plugins_list), GTK_SELECTION_NONE);
		gtk_widget_add_css_class (plugins_list, "boxed-list");

		n_plugins = g_list_model_get_n_items (G_LIST_MODEL (engine));
		for (i = 0; i < n_plugins; i++) {
			PeasPluginInfo *info;
			GtkWidget *row;
			GtkWidget *hbox;
			GtkWidget *check;
			GtkWidget *icon;
			GtkWidget *label_box;
			GtkWidget *name_label;
			GtkWidget *desc_label;
			const char *plugin_name;
			const char *plugin_desc;
			const char *icon_name;
			char *name_markup;

			info = g_list_model_get_item (G_LIST_MODEL (engine), i);
			if (peas_plugin_info_is_hidden (info)) {
				g_object_unref (info);
				continue;
			}

			plugin_name = peas_plugin_info_get_name (info);
			plugin_desc = peas_plugin_info_get_description (info);
			icon_name = peas_plugin_info_get_icon_name (info);

			row = gtk_list_box_row_new ();
			gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);

			hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
			gtk_widget_set_margin_start (hbox, 8);
			gtk_widget_set_margin_end (hbox, 8);
			gtk_widget_set_margin_top (hbox, 8);
			gtk_widget_set_margin_bottom (hbox, 8);

			check = gtk_check_button_new ();
			gtk_check_button_set_active (GTK_CHECK_BUTTON (check),
						     peas_plugin_info_is_loaded (info));
			gtk_widget_set_valign (check, GTK_ALIGN_CENTER);
			if (peas_plugin_info_is_builtin (info))
				gtk_widget_set_sensitive (check, FALSE);

			g_object_set_data (G_OBJECT (check), "peas-plugin-info", info);
			g_signal_connect_object (check, "toggled",
						 G_CALLBACK (plugin_check_toggled_cb),
						 engine, 0);

			icon = gtk_image_new_from_icon_name (
				(icon_name != NULL) ? icon_name : "application-x-addon");
			gtk_image_set_pixel_size (GTK_IMAGE (icon), 32);
			gtk_widget_set_valign (icon, GTK_ALIGN_CENTER);

			label_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
			gtk_widget_set_hexpand (label_box, TRUE);
			gtk_widget_set_valign (label_box, GTK_ALIGN_CENTER);

			name_markup = g_markup_printf_escaped ("<b>%s</b>",
							       plugin_name ? plugin_name : "");
			name_label = gtk_label_new (NULL);
			gtk_label_set_markup (GTK_LABEL (name_label), name_markup);
			gtk_label_set_xalign (GTK_LABEL (name_label), 0.0);
			gtk_label_set_ellipsize (GTK_LABEL (name_label), PANGO_ELLIPSIZE_END);
			g_free (name_markup);

			desc_label = gtk_label_new (plugin_desc);
			gtk_label_set_xalign (GTK_LABEL (desc_label), 0.0);
			gtk_label_set_ellipsize (GTK_LABEL (desc_label), PANGO_ELLIPSIZE_END);
			gtk_widget_add_css_class (desc_label, "dim-label");
			gtk_widget_add_css_class (desc_label, "caption");

			gtk_box_append (GTK_BOX (label_box), name_label);
			if (plugin_desc != NULL && plugin_desc[0] != '\0')
				gtk_box_append (GTK_BOX (label_box), desc_label);

			gtk_box_append (GTK_BOX (hbox), check);
			gtk_box_append (GTK_BOX (hbox), icon);
			gtk_box_append (GTK_BOX (hbox), label_box);

			gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), hbox);
			gtk_list_box_append (GTK_LIST_BOX (plugins_list), row);

			g_object_unref (info);
		}

		gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (plugins_scroll), plugins_list);
		gtk_box_append (GTK_BOX (plugins_page), plugins_scroll);

		gtk_notebook_append_page (GTK_NOTEBOOK (shell_preferences->priv->notebook),
					  plugins_page,
					  gtk_label_new (_("Plugins")));
	}

	return GTK_WIDGET (shell_preferences);
}

static gboolean
rb_shell_preferences_window_close_request_cb (GtkWindow *window,
					       RBShellPreferences *shell_preferences)
{
	gtk_widget_set_visible (GTK_WIDGET (shell_preferences), FALSE);

	return TRUE;
}

static void
rb_shell_preferences_response_cb (GtkDialog *dialog,
				  int response_id,
				  RBShellPreferences *shell_preferences)
{
	if (response_id == GTK_RESPONSE_CLOSE)
		gtk_widget_hide (GTK_WIDGET (shell_preferences));
}

static void
column_check_toggled_cb (GtkWidget *widget, RBShellPreferences *preferences)
{
	const char *prop_name;
	const char *column;
	GVariantBuilder *b;
	GVariantIter *iter;
	GVariant *v;

	prop_name = (const char *)g_object_get_data (G_OBJECT (widget), COLUMN_CHECK_PROP_NAME);
	g_assert (prop_name);

	v = g_settings_get_value (preferences->priv->source_settings, "visible-columns");

	/* remove from current column list */
	b = g_variant_builder_new (G_VARIANT_TYPE ("as"));
	iter = g_variant_iter_new (v);
	while (g_variant_iter_loop (iter, "s", &column)) {
		if (g_strcmp0 (column, prop_name) != 0) {
			g_variant_builder_add (b, "s", column);
		}
	}
	g_variant_unref (v);

	/* if enabled, add it */
	if (gtk_check_button_get_active (GTK_CHECK_BUTTON (widget))) {
		g_variant_builder_add (b, "s", prop_name);
	}

	v = g_variant_builder_end (b);

	g_settings_set_value (preferences->priv->source_settings, "visible-columns", v);

	g_variant_builder_unref (b);
}

/**
 * rb_shell_preferences_browser_views_activated_cb:
 * @widget: the radio button that was selected
 * @shell_preferences: the #RBShellPreferences instance
 *
 * Signal handler used for the radio buttons used to configure the
 * visible browser views.
 */
void
rb_shell_preferences_browser_views_activated_cb (GtkWidget *widget,
						 RBShellPreferences *shell_preferences)
{
	int index;

	if (shell_preferences->priv->applying_settings)
		return;

	index = g_slist_index (shell_preferences->priv->browser_views_group, widget);

	g_settings_set_enum (shell_preferences->priv->source_settings, "browser-views", index);
}

static void
source_settings_changed_cb (GSettings *settings, const char *key, RBShellPreferences *preferences)
{
	if (g_strcmp0 (key, "browser-views") == 0) {
		int view;
		GtkWidget *widget;

		view = g_settings_get_enum (preferences->priv->source_settings, "browser-views");
		widget = GTK_WIDGET (g_slist_nth_data (preferences->priv->browser_views_group, view));
		preferences->priv->applying_settings = TRUE;
		gtk_check_button_set_active (GTK_CHECK_BUTTON (widget), TRUE);
		preferences->priv->applying_settings = FALSE;

	} else if (g_strcmp0 (key, "visible-columns") == 0) {
		char **columns;
		GHashTableIter iter;
		gpointer name_ptr;
		gpointer widget_ptr;

		columns = g_settings_get_strv (preferences->priv->source_settings, "visible-columns");

		g_hash_table_iter_init (&iter, preferences->priv->column_checks);
		while (g_hash_table_iter_next (&iter, &name_ptr, &widget_ptr)) {
			gboolean enabled;

			enabled = rb_str_in_strv (name_ptr, (const char **)columns);
			gtk_check_button_set_active (GTK_CHECK_BUTTON (widget_ptr), enabled);
		}

		g_strfreev (columns);
	}
}

static void
player_settings_changed_cb (GSettings *settings, const char *key, RBShellPreferences *preferences)
{
	if (g_strcmp0 (key, "transition-time") == 0) {
		gtk_range_set_value (GTK_RANGE (preferences->priv->transition_duration),
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
transition_time_changed_cb (GtkRange *range, RBShellPreferences *preferences)
{
	rb_settings_delayed_sync (preferences->priv->player_settings,
				  (RBDelayedSyncFunc) sync_transition_time,
				  g_object_ref (range),
				  g_object_unref);
}

static GtkWidget *
get_box_for_location (RBShellPreferences *prefs, RBShellPrefsUILocation location)
{
	switch (location) {
	case RB_SHELL_PREFS_UI_LOCATION_GENERAL:
		return prefs->priv->general_prefs_plugin_box;
	case RB_SHELL_PREFS_UI_LOCATION_PLAYBACK:
		return prefs->priv->playback_prefs_plugin_box;
	default:
		g_assert_not_reached();
	}
}

/**
 * rb_shell_preferences_add_widget:
 * @prefs: the #RBShellPreferences
 * @widget: the #GtkWidget to insert into the preferences window
 * @location: the location at which to insert the widget
 * @expand: whether the widget should be given extra space
 * @fill: whether the widget should fill all space allocated to it
 *
 * Adds a widget to the preferences window.  See #gtk_box_pack_start for
 * details on how the expand and fill parameters work.  This function can be
 * used to add widgets to the 'general' and 'playback' pages.
 */
void
rb_shell_preferences_add_widget (RBShellPreferences *prefs,
				 GtkWidget *widget,
				 RBShellPrefsUILocation location,
				 gboolean expand,
				 gboolean fill)
{
	GtkWidget *box;

	box = get_box_for_location (prefs, location);
	gtk_box_append (GTK_BOX (box), widget);
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
	GtkWidget *box;

	box = get_box_for_location (prefs, location);
	gtk_box_remove (GTK_BOX (box), widget);
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

/**
 * RBShellPrefsUILocation:
 * @RB_SHELL_PREFS_UI_LOCATION_GENERAL: The "general" preferences page
 * @RB_SHELL_PREFS_UI_LOCATION_PLAYBACK: THe "playback" preferences page
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
