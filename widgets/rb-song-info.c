/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Olivier Martin <olive.martin@gmail.com>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

/*
 * Yes, this code is ugly.
 */

#include "config.h"

#include <string.h>
#include <time.h>
#include <math.h>

#define EPSILON 0.0001

#include <glib/gi18n.h>
#include <adwaita.h>

#include "rhythmdb.h"
#include "rhythmdb-property-model.h"
#include "rb-song-info.h"
#include "rb-dialog.h"
#include "rb-rating.h"
#include "rb-source.h"
#include "rb-shell.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

static void rb_song_info_class_init (RBSongInfoClass *klass);
static void rb_song_info_init (RBSongInfo *song_info);
static void rb_song_info_constructed (GObject *object);
static void rb_song_info_setup (RBSongInfo *song_info);

static void rb_song_info_closed_cb (AdwDialog *dialog,
				    RBSongInfo *song_info);
static void rb_song_info_dispose (GObject *object);
static void rb_song_info_finalize (GObject *object);
static void rb_song_info_set_property (GObject *object,
				       guint prop_id,
				       const GValue *value,
				       GParamSpec *pspec);
static void rb_song_info_get_property (GObject *object,
				       guint prop_id,
				       GValue *value,
				       GParamSpec *pspec);
static void rb_song_info_populate_dialog (RBSongInfo *song_info);
static void rb_song_info_populate_dialog_multiple (RBSongInfo *song_info);
static void rb_song_info_update_duration (RBSongInfo *song_info);
static void rb_song_info_update_location (RBSongInfo *song_info);
static void rb_song_info_update_filesize (RBSongInfo *song_info);
static void rb_song_info_update_play_count (RBSongInfo *song_info);
static void rb_song_info_update_last_played (RBSongInfo *song_info);
static void rb_song_info_update_bitrate (RBSongInfo *song_info);
static void rb_song_info_update_buttons (RBSongInfo *song_info);
static void rb_song_info_update_rating (RBSongInfo *song_info);
static void rb_song_info_update_year (RBSongInfo *song_info);
static void rb_song_info_update_date_added (RBSongInfo *song_info);
static void rb_song_info_update_playback_error (RBSongInfo *song_info);

static void rb_song_info_backward_clicked_cb (GtkWidget *button,
					      RBSongInfo *song_info);
static void rb_song_info_forward_clicked_cb (GtkWidget *button,
					     RBSongInfo *song_info);
static void rb_song_info_query_model_changed_cb (GObject *source,
						 GParamSpec *pspec,
						 RBSongInfo *song_info);
static void rb_song_info_base_query_model_changed_cb (GObject *source,
						      GParamSpec *pspec,
						      RBSongInfo *song_info);
static void rb_song_info_rated_cb (RBRating *rating,
				   double score,
				   RBSongInfo *song_info);
static void rb_song_info_mnemonic_cb (GtkWidget *target);
static void rb_song_info_sync_entries (RBSongInfo *dialog);

struct RBSongInfoPrivate
{
	RhythmDB *db;
	RBSource *source;
	RBEntryView *entry_view;
	RhythmDBQueryModel *query_model;
	RhythmDBQueryModel *base_query_model;

	/* information on the displayed song */
	RhythmDBEntry *current_entry;
	GList *selected_entries;

	gboolean editable;

	/* the dialog widgets */
	GtkWidget   *toolbar_view;
	GtkWidget   *header_bar;
	GtkWidget   *backward;
	GtkWidget   *forward;
	GtkStack    *stack;
	GtkWidget   *switcher;

	GtkWidget   *title;
	GtkWidget   *artist;
	GtkWidget   *album;
	GtkWidget   *album_artist;
	GtkWidget   *composer;
	GtkWidget   *genre;
	GtkWidget   *track_cur;
	GtkWidget   *track_total;
	GtkWidget   *disc_cur;
	GtkWidget   *disc_total;
	GtkWidget   *year;
	GtkWidget   *comment;
	GtkTextBuffer *comment_buffer;
	GtkWidget   *playback_error_box;
	GtkWidget   *playback_error_label;
	GtkWidget   *bpm;

	GtkWidget   *title_sortname;
	GtkWidget   *artist_sortname;
	GtkWidget   *album_sortname;
	GtkWidget   *album_artist_sortname;
	GtkWidget   *composer_sortname;

	GtkWidget   *bitrate;
	GtkWidget   *duration;
	GtkWidget   *name;
	GtkWidget   *location;
	GtkWidget   *filesize;
	GtkWidget   *date_added;
	GtkWidget   *play_count;
	GtkWidget   *last_played;
	GtkWidget   *rating;

	RhythmDBPropertyModel* albums;
	RhythmDBPropertyModel* artists;
	RhythmDBPropertyModel* genres;
};

#define RB_SONG_INFO_GET_PRIVATE(o) (rb_song_info_get_instance_private (o))

/**
 * SECTION:rbsonginfo
 * @short_description: song properties dialog
 *
 * Displays song properties and, if we know how to edit tags in the file,
 * allows the user to edit them.
 *
 * This class has two modes.  It can display and edit properties of a single
 * entry, in which case it uses a #GtkStack to split the properties across
 * 'basic', 'sorting', and 'details' pages, and it can display and edit
 * properties of multiple entries at a time, in which case a smaller set of
 * properties is displayed across 'basic' and 'sorting' pages.
 *
 * In single-entry mode, it is possible to add extra pages to the view stack
 * in the dialog.  The 'create-song-info' signal is emitted by the #RBShell
 * object, allowing signal handlers to add pages by calling #rb_song_info_append_page.
 * The lyrics plugin is currently the only place where this ability is used.
 * In this mode, the dialog features 'back' and 'forward' buttons that move to the
 * next or previous entries from the currently displayed track list.
 *
 * In multiple-entry mode, only the set of properties that can usefully be set
 * across multiple entries at once are displayed.
 *
 * When the dialog is closed, any changes made will be applied to the entry (or entries)
 * that were displayed in the dialog.  For songs in the library, this will result
 * in the song tags being updated on disk.  For other entry types, this only updates
 * the data store in the database.
 */

enum
{
	PRE_METADATA_CHANGE,
	POST_METADATA_CHANGE,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_SOURCE,
	PROP_ENTRY_VIEW,
	PROP_CURRENT_ENTRY,
	PROP_SELECTED_ENTRIES
};

static guint rb_song_info_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (RBSongInfo, rb_song_info, ADW_TYPE_DIALOG)

static void
rb_song_info_class_init (RBSongInfoClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = rb_song_info_set_property;
	object_class->get_property = rb_song_info_get_property;
	object_class->constructed = rb_song_info_constructed;

	/**
	 * RBSongInfo:source:
	 *
	 * The #RBSource that created the song properties window.  Used to update
	 * for track list changes, and to find the sets of albums, artist, and genres
	 * to use for tag edit completion.
	 */
	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
					                      "RBSource",
					                      "RBSource object",
					                      RB_TYPE_SOURCE,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBSongInfo:entry-view:
	 *
	 * The #RBEntryView for the source that created the song properties window.  Used
	 * find the set of selected entries, and to change the selection when the 'back' and
	 * 'forward' buttons are pressed.
	 */
	g_object_class_install_property (object_class,
					 PROP_ENTRY_VIEW,
					 g_param_spec_object ("entry-view",
					                      "RBEntryView",
					                      "RBEntryView object",
					                      RB_TYPE_ENTRY_VIEW,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBSongInfo:current-entry:
	 *
	 * The #RhythmDBEntry that is currently being displayed.  Will be NULL for
	 * multiple-entry song properties windows.
	 */
	g_object_class_install_property (object_class,
					 PROP_CURRENT_ENTRY,
					 g_param_spec_boxed ("current-entry",
					                     "RhythmDBEntry",
					                     "RhythmDBEntry object",
							     RHYTHMDB_TYPE_ENTRY,
					                     G_PARAM_READABLE));

	/**
	 * RBSongInfo:selected-entries:
	 *
	 * The set of #RhythmDBEntry objects currently being displayed.  Valid for both
	 * single-entry and multiple-entry song properties windows.
	 */
	g_object_class_install_property (object_class,
					 PROP_SELECTED_ENTRIES,
					 g_param_spec_boxed ("selected-entries",
							     "selected entries",
							     "List of selected entries, if this is a multiple-entry dialog",
							     G_TYPE_ARRAY,
							     G_PARAM_READABLE));

	object_class->dispose = rb_song_info_dispose;
	object_class->finalize = rb_song_info_finalize;

	/**
	 * RBSongInfo::pre-metadata-change:
	 * @song_info: the #RBSongInfo instance
	 * @entry: the #RhythmDBEntry being changed
	 *
	 * Emitted just before the changes made in the song properties window
	 * are applied to the database.  This is only emitted in the single-entry
	 * case.
	 */
	rb_song_info_signals[PRE_METADATA_CHANGE] =
		g_signal_new ("pre-metadata-change",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSongInfoClass, pre_metadata_change),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      RHYTHMDB_TYPE_ENTRY);

	/**
	 * RBSongInfo::post-metadata-change:
	 * @song_info: the #RBSongInfo instance
	 * @entry: the #RhythmDBEntry that was changed
	 *
	 * Emitted just after changes have been applied to the database.
	 * Probably useless.
	 */
	rb_song_info_signals[POST_METADATA_CHANGE] =
		g_signal_new ("post-metadata-change",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSongInfoClass, post_metadata_change),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      RHYTHMDB_TYPE_ENTRY);

}

static void
rb_song_info_init (RBSongInfo *song_info)
{
	song_info->priv = RB_SONG_INFO_GET_PRIVATE (song_info);
}

/* ---- Helper: create a bold label for grid rows ---- */
static GtkWidget *
create_bold_label (const char *markup_text)
{
	GtkWidget *label = gtk_label_new (NULL);
	char *bold = g_strdup_printf ("<b>%s</b>", markup_text);
	gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), bold);
	g_free (bold);
	gtk_label_set_xalign (GTK_LABEL (label), 1.0f);
	return label;
}

/* ---- Helper: add a label + widget row to a grid ---- */
static void
add_grid_row (GtkGrid *grid, int row, const char *label_text,
	      GtkWidget *widget, int col_span)
{
	GtkWidget *label = create_bold_label (label_text);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget);
	gtk_grid_attach (grid, label, 0, row, 1, 1);
	gtk_grid_attach (grid, widget, 1, row, col_span, 1);
}

/* ---- Helper: create a read-only GtkLabel for info fields ---- */
static GtkWidget *
create_info_label (void)
{
	GtkWidget *label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_label_set_selectable (GTK_LABEL (label), FALSE);
	gtk_widget_set_hexpand (label, TRUE);
	return label;
}

/* ---- Helper: create a GtkEntry for editable fields ---- */
static GtkWidget *
create_entry (gboolean editable)
{
	GtkWidget *entry = gtk_entry_new ();
	gtk_editable_set_editable (GTK_EDITABLE (entry), editable);
	gtk_widget_set_hexpand (entry, TRUE);
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
	return entry;
}

/* ---- Helper: create a read-only GtkEntry (for name/location) ---- */
static GtkWidget *
create_readonly_entry (void)
{
	GtkWidget *entry = gtk_entry_new ();
	gtk_editable_set_editable (GTK_EDITABLE (entry), FALSE);
	gtk_widget_set_hexpand (entry, TRUE);
	return entry;
}

/* ---- Helper: wrap a GtkGrid in an AdwPreferencesPage ---- */
static GtkWidget *
wrap_grid_in_page (GtkWidget *grid)
{
	AdwPreferencesPage *page;
	AdwPreferencesGroup *group;

	page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());
	group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	adw_preferences_group_add (group, grid);
	adw_preferences_page_add (page, group);
	return GTK_WIDGET (page);
}

static void
rb_song_info_construct_single (RBSongInfo *song_info, gboolean editable)
{
	GtkWidget *grid;
	GtkWidget *page;
	GtkWidget *comment_scroll;
	GtkWidget *hbox;
	int row;

	/* Back/Forward buttons in the header bar */
	song_info->priv->backward = gtk_button_new_from_icon_name ("go-previous-symbolic");
	gtk_widget_set_tooltip_text (song_info->priv->backward, _("Back"));
	adw_header_bar_pack_start (ADW_HEADER_BAR (song_info->priv->header_bar),
				   song_info->priv->backward);
	g_signal_connect_object (G_OBJECT (song_info->priv->backward),
				 "clicked",
				 G_CALLBACK (rb_song_info_backward_clicked_cb),
				 song_info, 0);

	song_info->priv->forward = gtk_button_new_from_icon_name ("go-next-symbolic");
	gtk_widget_set_tooltip_text (song_info->priv->forward, _("Forward"));
	adw_header_bar_pack_start (ADW_HEADER_BAR (song_info->priv->header_bar),
				   song_info->priv->forward);
	g_signal_connect_object (G_OBJECT (song_info->priv->forward),
				 "clicked",
				 G_CALLBACK (rb_song_info_forward_clicked_cb),
				 song_info, 0);

	adw_dialog_set_title (ADW_DIALOG (song_info), _("Song Properties"));

	/* ---- Basic page ---- */
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	row = 0;

	song_info->priv->title = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("_Title:"), song_info->priv->title, 3);
	g_signal_connect_object (song_info->priv->title, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->artist = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("_Artist:"), song_info->priv->artist, 3);
	g_signal_connect_object (song_info->priv->artist, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->album = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Albu_m:"), song_info->priv->album, 3);
	g_signal_connect_object (song_info->priv->album, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->album_artist = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Album A_rtist:"), song_info->priv->album_artist, 3);
	g_signal_connect_object (song_info->priv->album_artist, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->composer = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("_Composer:"), song_info->priv->composer, 3);
	g_signal_connect_object (song_info->priv->composer, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->genre = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("_Genre:"), song_info->priv->genre, 3);
	g_signal_connect_object (song_info->priv->genre, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	/* Track number: cur / total in a horizontal box */
	song_info->priv->track_cur = create_entry (editable);
	gtk_widget_set_hexpand (song_info->priv->track_cur, TRUE);
	gtk_entry_set_max_length (GTK_ENTRY (song_info->priv->track_cur), 7);
	g_signal_connect_object (song_info->priv->track_cur, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->track_total = create_entry (editable);
	gtk_widget_set_hexpand (song_info->priv->track_total, TRUE);
	gtk_entry_set_max_length (GTK_ENTRY (song_info->priv->track_total), 7);
	g_signal_connect_object (song_info->priv->track_total, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append (GTK_BOX (hbox), song_info->priv->track_cur);
	gtk_box_append (GTK_BOX (hbox), gtk_label_new (_("of")));
	gtk_box_append (GTK_BOX (hbox), song_info->priv->track_total);
	gtk_widget_set_hexpand (hbox, TRUE);
	add_grid_row (GTK_GRID (grid), row++, _("Track _number:"), hbox, 3);

	/* Disc number: cur / total in a horizontal box */
	song_info->priv->disc_cur = create_entry (editable);
	gtk_widget_set_hexpand (song_info->priv->disc_cur, TRUE);
	gtk_entry_set_max_length (GTK_ENTRY (song_info->priv->disc_cur), 7);
	g_signal_connect_object (song_info->priv->disc_cur, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->disc_total = create_entry (editable);
	gtk_widget_set_hexpand (song_info->priv->disc_total, TRUE);
	gtk_entry_set_max_length (GTK_ENTRY (song_info->priv->disc_total), 7);
	g_signal_connect_object (song_info->priv->disc_total, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append (GTK_BOX (hbox), song_info->priv->disc_cur);
	gtk_box_append (GTK_BOX (hbox), gtk_label_new (_("of")));
	gtk_box_append (GTK_BOX (hbox), song_info->priv->disc_total);
	gtk_widget_set_hexpand (hbox, TRUE);
	add_grid_row (GTK_GRID (grid), row++, _("_Disc number:"), hbox, 3);

	song_info->priv->year = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("_Year:"), song_info->priv->year, 3);
	g_signal_connect_object (song_info->priv->year, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->bpm = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("_BPM:"), song_info->priv->bpm, 3);

	/* Comment (multiline) */
	song_info->priv->comment = gtk_text_view_new ();
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (song_info->priv->comment), GTK_WRAP_WORD);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (song_info->priv->comment), editable);
	song_info->priv->comment_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (song_info->priv->comment));
	g_signal_connect_object (song_info->priv->comment, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	comment_scroll = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (comment_scroll),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (comment_scroll), 60);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (comment_scroll),
				       song_info->priv->comment);
	gtk_widget_set_vexpand (comment_scroll, TRUE);
	add_grid_row (GTK_GRID (grid), row++, _("Co_mment:"), comment_scroll, 3);

	/* Playback error box (hidden by default) */
	song_info->priv->playback_error_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_visible (song_info->priv->playback_error_box, FALSE);
	song_info->priv->playback_error_label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (song_info->priv->playback_error_label), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (song_info->priv->playback_error_label), TRUE);
	gtk_box_append (GTK_BOX (song_info->priv->playback_error_box),
			gtk_image_new_from_icon_name ("dialog-warning-symbolic"));
	gtk_box_append (GTK_BOX (song_info->priv->playback_error_box),
			song_info->priv->playback_error_label);
	gtk_grid_attach (GTK_GRID (grid), song_info->priv->playback_error_box, 0, row++, 4, 1);

	page = wrap_grid_in_page (grid);
	gtk_stack_add_titled (song_info->priv->stack, page, "basic", _("Basic"));

	/* ---- Sorting page ---- */
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	row = 0;

	song_info->priv->title_sortname = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Title sort _key:"), song_info->priv->title_sortname, 1);
	g_signal_connect_object (song_info->priv->title_sortname, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->artist_sortname = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Artist sort ke_y:"), song_info->priv->artist_sortname, 1);
	g_signal_connect_object (song_info->priv->artist_sortname, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->album_sortname = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Album sort k_ey:"), song_info->priv->album_sortname, 1);
	g_signal_connect_object (song_info->priv->album_sortname, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->album_artist_sortname = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Album artist sort key:"), song_info->priv->album_artist_sortname, 1);
	g_signal_connect_object (song_info->priv->album_artist_sortname, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->composer_sortname = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Composer sort key:"), song_info->priv->composer_sortname, 1);
	g_signal_connect_object (song_info->priv->composer_sortname, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	page = wrap_grid_in_page (grid);
	gtk_stack_add_titled (song_info->priv->stack, page, "sorting", _("Sorting"));

	/* ---- Details page ---- */
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	row = 0;

	song_info->priv->name = create_readonly_entry ();
	add_grid_row (GTK_GRID (grid), row++, _("_File name:"), song_info->priv->name, 1);

	song_info->priv->location = create_readonly_entry ();
	add_grid_row (GTK_GRID (grid), row++, _("_Location:"), song_info->priv->location, 1);

	song_info->priv->filesize = create_info_label ();
	add_grid_row (GTK_GRID (grid), row++, _("File si_ze:"), song_info->priv->filesize, 1);

	song_info->priv->duration = create_info_label ();
	add_grid_row (GTK_GRID (grid), row++, _("Du_ration:"), song_info->priv->duration, 1);

	song_info->priv->bitrate = create_info_label ();
	add_grid_row (GTK_GRID (grid), row++, _("_Bitrate:"), song_info->priv->bitrate, 1);

	song_info->priv->date_added = create_info_label ();
	add_grid_row (GTK_GRID (grid), row++, _("Date _added:"), song_info->priv->date_added, 1);

	song_info->priv->last_played = create_info_label ();
	add_grid_row (GTK_GRID (grid), row++, _("Last _played:"), song_info->priv->last_played, 1);

	song_info->priv->play_count = create_info_label ();
	add_grid_row (GTK_GRID (grid), row++, _("Play _count:"), song_info->priv->play_count, 1);

	/* Rating */
	song_info->priv->rating = GTK_WIDGET (rb_rating_new ());
	g_signal_connect_object (song_info->priv->rating, "rated",
				 G_CALLBACK (rb_song_info_rated_cb),
				 G_OBJECT (song_info), 0);
	add_grid_row (GTK_GRID (grid), row++, _("_Rating:"), song_info->priv->rating, 1);

	page = wrap_grid_in_page (grid);
	gtk_stack_add_titled (song_info->priv->stack, page, "details", _("Details"));

	/* default focus */
	gtk_widget_grab_focus (song_info->priv->title);
}

static void
rb_song_info_construct_multiple (RBSongInfo *song_info, gboolean editable)
{
	GtkWidget *grid;
	GtkWidget *page;
	GtkWidget *hbox;
	int row;

	adw_dialog_set_title (ADW_DIALOG (song_info),
			      _("Multiple Song Properties"));

	/* ---- Basic page ---- */
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	row = 0;

	song_info->priv->artist = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("_Artist:"), song_info->priv->artist, 3);
	g_signal_connect_object (song_info->priv->artist, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->album = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Albu_m:"), song_info->priv->album, 3);
	g_signal_connect_object (song_info->priv->album, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->album_artist = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Album A_rtist:"), song_info->priv->album_artist, 3);
	g_signal_connect_object (song_info->priv->album_artist, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->composer = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("_Composer:"), song_info->priv->composer, 3);
	g_signal_connect_object (song_info->priv->composer, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->genre = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("_Genre:"), song_info->priv->genre, 3);
	g_signal_connect_object (song_info->priv->genre, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->year = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("_Year:"), song_info->priv->year, 3);
	g_signal_connect_object (song_info->priv->year, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	/* Rating */
	song_info->priv->rating = GTK_WIDGET (rb_rating_new ());
	g_signal_connect_object (song_info->priv->rating, "rated",
				 G_CALLBACK (rb_song_info_rated_cb),
				 G_OBJECT (song_info), 0);
	add_grid_row (GTK_GRID (grid), row++, _("_Rating:"), song_info->priv->rating, 3);

	/* Track total */
	song_info->priv->track_total = create_entry (editable);
	gtk_widget_set_hexpand (song_info->priv->track_total, FALSE);
	gtk_entry_set_max_length (GTK_ENTRY (song_info->priv->track_total), 4);
	add_grid_row (GTK_GRID (grid), row++, _("Track _total:"), song_info->priv->track_total, 3);
	g_signal_connect_object (song_info->priv->track_total, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	/* Disc number: cur / total */
	song_info->priv->disc_cur = create_entry (editable);
	gtk_widget_set_hexpand (song_info->priv->disc_cur, FALSE);
	gtk_entry_set_max_length (GTK_ENTRY (song_info->priv->disc_cur), 4);
	g_signal_connect_object (song_info->priv->disc_cur, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->disc_total = create_entry (editable);
	gtk_widget_set_hexpand (song_info->priv->disc_total, FALSE);
	gtk_entry_set_max_length (GTK_ENTRY (song_info->priv->disc_total), 4);
	g_signal_connect_object (song_info->priv->disc_total, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append (GTK_BOX (hbox), song_info->priv->disc_cur);
	gtk_box_append (GTK_BOX (hbox), gtk_label_new (_("of")));
	gtk_box_append (GTK_BOX (hbox), song_info->priv->disc_total);
	gtk_widget_set_hexpand (hbox, TRUE);
	add_grid_row (GTK_GRID (grid), row++, _("_Disc number:"), hbox, 3);

	page = wrap_grid_in_page (grid);
	gtk_stack_add_titled (song_info->priv->stack, page, "basic", _("Basic"));

	/* ---- Sorting page ---- */
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	row = 0;

	song_info->priv->artist_sortname = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Artist sort ke_y:"), song_info->priv->artist_sortname, 1);
	g_signal_connect_object (song_info->priv->artist_sortname, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->album_sortname = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Album sort k_ey:"), song_info->priv->album_sortname, 1);
	g_signal_connect_object (song_info->priv->album_sortname, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->album_artist_sortname = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Album artist sort key:"), song_info->priv->album_artist_sortname, 1);
	g_signal_connect_object (song_info->priv->album_artist_sortname, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	song_info->priv->composer_sortname = create_entry (editable);
	add_grid_row (GTK_GRID (grid), row++, _("Composer sort key:"), song_info->priv->composer_sortname, 1);
	g_signal_connect_object (song_info->priv->composer_sortname, "mnemonic-activate",
				 G_CALLBACK (rb_song_info_mnemonic_cb), NULL, 0);

	page = wrap_grid_in_page (grid);
	gtk_stack_add_titled (song_info->priv->stack, page, "sorting", _("Sorting"));

	/* default focus */
	gtk_widget_grab_focus (song_info->priv->artist);
}


static void
rb_song_info_constructed (GObject *object)
{
	RBSongInfo *song_info;
	GList *selected_entries;
	GList *tem;
	gboolean editable = TRUE;

	RB_CHAIN_GOBJECT_METHOD (rb_song_info_parent_class, constructed, object);

	song_info = RB_SONG_INFO (object);

	selected_entries = rb_entry_view_get_selected_entries (song_info->priv->entry_view);

	g_return_if_fail (selected_entries != NULL);

	for (tem = selected_entries; tem; tem = tem->next) {
		if (!rhythmdb_entry_can_sync_metadata (selected_entries->data)) {
			editable = FALSE;
			break;
		}
	}

	song_info->priv->editable = editable;

	if (selected_entries->next == NULL) {
		song_info->priv->current_entry = selected_entries->data;
		song_info->priv->selected_entries = NULL;

		g_list_foreach (selected_entries, (GFunc)rhythmdb_entry_unref, NULL);
		g_list_free (selected_entries);
	} else {
		song_info->priv->current_entry = NULL;
		song_info->priv->selected_entries = selected_entries;
	}
}

/**
 * rb_song_info_setup:
 *
 * Builds the dialog UI.  Called from rb_song_info_new() after g_object_new()
 * returns, so that widget-property notifications dispatched by AdwDialog
 * (content-width, content-height, child) are not trapped inside
 * g_object_new_internal()'s freeze/thaw cycle.
 */
static void
rb_song_info_setup (RBSongInfo *song_info)
{
	gboolean editable = song_info->priv->editable;
	RBShell *shell;

	/* Build the AdwToolbarView + AdwHeaderBar + GtkStackSwitcher + GtkStack */
	song_info->priv->stack = GTK_STACK (gtk_stack_new ());
	gtk_stack_set_transition_type (song_info->priv->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);

	song_info->priv->switcher = gtk_stack_switcher_new ();
	gtk_stack_switcher_set_stack (GTK_STACK_SWITCHER (song_info->priv->switcher),
				     song_info->priv->stack);

	song_info->priv->header_bar = adw_header_bar_new ();
	adw_header_bar_set_title_widget (ADW_HEADER_BAR (song_info->priv->header_bar),
					 song_info->priv->switcher);

	song_info->priv->toolbar_view = adw_toolbar_view_new ();
	adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (song_info->priv->toolbar_view),
				      song_info->priv->header_bar);
	adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (song_info->priv->toolbar_view),
				      GTK_WIDGET (song_info->priv->stack));

	adw_dialog_set_child (ADW_DIALOG (song_info), song_info->priv->toolbar_view);
	adw_dialog_set_content_width (ADW_DIALOG (song_info), 600);
	adw_dialog_set_content_height (ADW_DIALOG (song_info), 620);

	/* Build pages programmatically */
	if (song_info->priv->current_entry) {
		rb_song_info_construct_single (song_info, editable);
		rb_song_info_populate_dialog (song_info);
	} else {
		rb_song_info_construct_multiple (song_info, editable);
		rb_song_info_populate_dialog_multiple (song_info);
	}

	/* Let plugins add extra pages (e.g. lyrics, album art).
	 *
	 * RBSongInfo inherits from AdwDialog -> GtkWidget -> GInitiallyUnowned,
	 * so at this point the object has a floating reference (refcount 1).
	 * When the signal is marshalled to Python plugins via GObject
	 * Introspection, PyGObject calls g_object_ref_sink() on the
	 * parameter -- sinking the floating ref (refcount stays 1).  When the
	 * Python wrapper is released, PyGObject unrefs, dropping refcount to
	 * 0 and destroying the object.
	 *
	 * Fix: sink the floating ref ourselves before the emission (giving us
	 * a real owning reference at refcount 1, non-floating), then restore
	 * the floating state afterward so that adw_dialog_present() can sink
	 * it as expected by the normal AdwDialog ownership convention. */
	g_object_ref_sink (song_info);

	g_object_get (G_OBJECT (song_info->priv->source), "shell", &shell, NULL);
	g_signal_emit_by_name (G_OBJECT (shell), "create_song_info", song_info, (song_info->priv->current_entry == NULL));
	g_object_unref (G_OBJECT (shell));

	/* Restore floating state for adw_dialog_present() to sink. */
	g_object_force_floating (G_OBJECT (song_info));

	g_signal_connect (song_info, "closed",
			  G_CALLBACK (rb_song_info_closed_cb), song_info);

	rb_song_info_update_playback_error (song_info);
}

static void
rb_song_info_dispose (GObject *object)
{
	RBSongInfo *song_info;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SONG_INFO (object));

	song_info = RB_SONG_INFO (object);

	g_return_if_fail (song_info->priv != NULL);

	if (song_info->priv->albums != NULL) {
		g_object_unref (song_info->priv->albums);
		song_info->priv->albums = NULL;
	}
	if (song_info->priv->artists != NULL) {
		g_object_unref (song_info->priv->artists);
		song_info->priv->artists = NULL;
	}
	if (song_info->priv->genres != NULL) {
		g_object_unref (song_info->priv->genres);
		song_info->priv->genres = NULL;
	}

	if (song_info->priv->db != NULL) {
		g_object_unref (song_info->priv->db);
		song_info->priv->db = NULL;
	}
	if (song_info->priv->source != NULL) {
		g_signal_handlers_disconnect_by_func (song_info->priv->source,
						      G_CALLBACK (rb_song_info_query_model_changed_cb),
						      song_info);
		g_signal_handlers_disconnect_by_func (song_info->priv->source,
						      G_CALLBACK (rb_song_info_base_query_model_changed_cb),
						      song_info);
		g_object_unref (song_info->priv->source);
		song_info->priv->source = NULL;
	}
	if (song_info->priv->query_model != NULL) {
		g_object_unref (song_info->priv->query_model);
		song_info->priv->query_model = NULL;
	}

	G_OBJECT_CLASS (rb_song_info_parent_class)->dispose (object);
}

static void
rb_song_info_finalize (GObject *object)
{
	RBSongInfo *song_info;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SONG_INFO (object));

	song_info = RB_SONG_INFO (object);

	g_return_if_fail (song_info->priv != NULL);

	if (song_info->priv->selected_entries != NULL) {
		g_list_foreach (song_info->priv->selected_entries, (GFunc)rhythmdb_entry_unref, NULL);
		g_list_free (song_info->priv->selected_entries);
	}

	G_OBJECT_CLASS (rb_song_info_parent_class)->finalize (object);
}

static void
rb_song_info_set_source_internal (RBSongInfo *song_info,
				  RBSource   *source)
{
	if (song_info->priv->source != NULL) {
		g_signal_handlers_disconnect_by_func (song_info->priv->source,
						      rb_song_info_query_model_changed_cb,
						      song_info);
		g_signal_handlers_disconnect_by_func (song_info->priv->source,
						      rb_song_info_base_query_model_changed_cb,
						      song_info);
		g_object_unref (song_info->priv->source);
		g_object_unref (song_info->priv->query_model);
		g_object_unref (song_info->priv->db);
	}

	song_info->priv->source = source;

	g_object_ref (song_info->priv->source);

	g_object_get (G_OBJECT (song_info->priv->source), "query-model", &song_info->priv->query_model, NULL);

	g_signal_connect_object (G_OBJECT (song_info->priv->source),
				 "notify::query-model",
				 G_CALLBACK (rb_song_info_query_model_changed_cb),
				 song_info, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->source),
				 "notify::base-query-model",
				 G_CALLBACK (rb_song_info_base_query_model_changed_cb),
				 song_info, 0);

	g_object_get (G_OBJECT (song_info->priv->query_model), "db", &song_info->priv->db, NULL);

	rb_song_info_query_model_changed_cb (G_OBJECT (song_info->priv->source), NULL, song_info);
	rb_song_info_base_query_model_changed_cb (G_OBJECT (song_info->priv->source), NULL, song_info);
}

static void
rb_song_info_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBSongInfo *song_info = RB_SONG_INFO (object);

	switch (prop_id) {
	case PROP_SOURCE:
		rb_song_info_set_source_internal (song_info, g_value_get_object (value));
		break;
	case PROP_ENTRY_VIEW:
		song_info->priv->entry_view = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_song_info_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBSongInfo *song_info = RB_SONG_INFO (object);

	switch (prop_id) {
	case PROP_SOURCE:
		g_value_set_object (value, song_info->priv->source);
		break;
	case PROP_ENTRY_VIEW:
		g_value_set_object (value, song_info->priv->entry_view);
		break;
	case PROP_CURRENT_ENTRY:
		g_value_set_boxed (value, song_info->priv->current_entry);
		break;
	case PROP_SELECTED_ENTRIES:
		if (song_info->priv->selected_entries) {
			GArray *value_array;
			GValue entry_value = { 0, };
			GList *entry_list;

			value_array = g_array_sized_new (FALSE, TRUE, sizeof (GValue), 1);
			g_array_set_clear_func (value_array, (GDestroyNotify) g_value_unset);
			g_value_init (&entry_value, RHYTHMDB_TYPE_ENTRY);
			for (entry_list = song_info->priv->selected_entries; entry_list; entry_list = entry_list->next) {
				g_value_set_boxed (&entry_value, entry_list->data);
				g_array_append_val (value_array, entry_value);
			}
			g_value_unset (&entry_value);
			g_value_take_boxed (value, value_array);
		} else {
			g_value_set_boxed (value, NULL);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_song_info_new:
 * @source: #RBSource creating the song properties window
 * @entry_view: the #RBEntryView to get selection data from
 *
 * Creates a new #RBSongInfo for the selected entry or entries in
 * the specified entry view.
 *
 * Return value: the new song properties window
 */
GtkWidget *
rb_song_info_new (RBSource *source, RBEntryView *entry_view)
{
	RBSongInfo *song_info;

        g_return_val_if_fail (RB_IS_SOURCE (source), NULL);
	if (entry_view == NULL) {
		entry_view = rb_source_get_entry_view (source);
		if (entry_view == NULL) {
			return NULL;
		}
	}

	if (rb_entry_view_have_selection (entry_view) == FALSE)
		return NULL;

	/* create the dialog */
	song_info = g_object_new (RB_TYPE_SONG_INFO,
				  "source", source,
				  "entry-view", entry_view,
				  NULL);

	g_return_val_if_fail (song_info->priv != NULL, NULL);

	/* Build the UI outside of g_object_new() so that AdwDialog property
	 * notifications (child, content-width, content-height) and the
	 * create_song_info plugin signal are not trapped inside
	 * g_object_new_internal()'s freeze/thaw cycle. */
	rb_song_info_setup (song_info);

	return GTK_WIDGET (song_info);
}

/**
 * rb_song_info_append_page:
 * @info: a #RBSongInfo
 * @title: the title of the new page
 * @page: the page #GtkWidget
 *
 * Adds a new page to the song properties window.  Should be called
 * in a handler connected to the #RBShell 'create-song-info' signal.
 *
 * Return value: the page number
 */
guint
rb_song_info_append_page (RBSongInfo *info, const char *title, GtkWidget *page)
{
	AdwPreferencesPage *pref_page;
	AdwPreferencesGroup *group;
	GtkStackPage *stack_page;

	/* Wrap the plugin widget in an AdwPreferencesPage for consistent styling */
	pref_page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());
	group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	gtk_widget_set_vexpand (page, TRUE);
	gtk_widget_set_hexpand (page, TRUE);
	adw_preferences_group_add (group, page);
	adw_preferences_page_add (pref_page, group);

	stack_page = gtk_stack_add_titled (info->priv->stack,
					   GTK_WIDGET (pref_page),
					   NULL,
					   title);
	/* Return an arbitrary page index — plugins don't really use the return value */
	return gtk_stack_page_get_name (stack_page) ? 1 : 0;
}

typedef void (*RBSongInfoSelectionFunc)(RBSongInfo *info,
					RhythmDBEntry *entry,
					void *data);

static void
rb_song_info_selection_for_each (RBSongInfo *info, RBSongInfoSelectionFunc func,
				 void *data)
{
	if (info->priv->current_entry)
		func (info, info->priv->current_entry, data);
	else {
		GList *tem;
		for (tem = info->priv->selected_entries; tem ; tem = tem->next)
			func (info, tem->data, data);
	}
}

static void
rb_song_info_closed_cb (AdwDialog *dialog,
			RBSongInfo *song_info)
{
	rb_song_info_sync_entries (song_info);
}

static void
rb_song_info_set_entry_rating (RBSongInfo *info,
			       RhythmDBEntry *entry,
			       void *data)
{
	GValue value = {0, };
	double trouble = *((double*) data);

	/* set the new value for the song */
	g_value_init (&value, G_TYPE_DOUBLE);
	g_value_set_double (&value, trouble);
	rhythmdb_entry_set (info->priv->db, entry, RHYTHMDB_PROP_RATING, &value);
	g_value_unset (&value);
}

static void
rb_song_info_rated_cb (RBRating *rating,
		       double score,
		       RBSongInfo *song_info)
{
	g_return_if_fail (RB_IS_RATING (rating));
	g_return_if_fail (RB_IS_SONG_INFO (song_info));
	g_return_if_fail (score >= 0 && score <= 5 );

	rb_song_info_selection_for_each (song_info,
					 rb_song_info_set_entry_rating,
					 &score);
	rhythmdb_commit (song_info->priv->db);

	g_object_set (G_OBJECT (song_info->priv->rating),
		      "rating", score,
		      NULL);
}

static void
rb_song_info_mnemonic_cb (GtkWidget *target)
{
	g_return_if_fail (GTK_IS_EDITABLE (target) || GTK_IS_TEXT_VIEW (target));

	gtk_widget_grab_focus (target);

	if (GTK_IS_EDITABLE (target)) {
		gtk_editable_select_region (GTK_EDITABLE (target), 0, -1);
	} else { /* GtkTextViews need special treatment */
		g_signal_emit_by_name (G_OBJECT (target), "select-all");
	}
}

static void
rb_song_info_populate_num_field (GtkEntry *field, gulong num)
{
	char *tmp;
	if (num > 0)
		tmp = g_strdup_printf ("%.2ld", num);
	else
		tmp = g_strdup (_("Unknown"));
	gtk_editable_set_text (GTK_EDITABLE (field), tmp);
	g_free (tmp);
}

static void
rb_song_info_populate_dnum_field (GtkEntry *field, gdouble num)
{
	char *tmp;
	if (num > 0)
		tmp = g_strdup_printf ("%.2f", num);
	else
		tmp = g_strdup (_("Unknown"));
	gtk_editable_set_text (GTK_EDITABLE (field), tmp);
	g_free (tmp);
}

static void
rb_song_info_populate_dialog_multiple (RBSongInfo *song_info)
{
	gboolean mixed_artists = FALSE;
	gboolean mixed_albums = FALSE;
	gboolean mixed_album_artists = FALSE;
	gboolean mixed_composers = FALSE;
	gboolean mixed_genres = FALSE;
	gboolean mixed_years = FALSE;
	gboolean mixed_track_totals = FALSE;
	gboolean mixed_disc_numbers = FALSE;
	gboolean mixed_disc_totals = FALSE;
	gboolean mixed_ratings = FALSE;
	gboolean mixed_artist_sortnames = FALSE;
	gboolean mixed_album_sortnames = FALSE;
	gboolean mixed_album_artist_sortnames = FALSE;
	gboolean mixed_composer_sortnames = FALSE;
	const char *artist = NULL;
	const char *album = NULL;
	const char *album_artist = NULL;
	const char *composer = NULL;
	const char *genre = NULL;
	int year = 0;
	int track_total = 0;
	int disc_number = 0;
	int disc_total = 0;
	double rating = 0.0; /* Zero is used for both "unrated" and "mixed ratings" too */
	const char *artist_sortname = NULL;
	const char *album_sortname = NULL;
	const char *album_artist_sortname = NULL;
	const char *composer_sortname = NULL;
	GList *l;

	g_assert (song_info->priv->selected_entries);

	for (l = song_info->priv->selected_entries; l != NULL; l = g_list_next (l)) {
		RhythmDBEntry *entry;
		const char *entry_artist;
		const char *entry_album;
		const char *entry_album_artist;
		const char *entry_composer;
		const char *entry_genre;
		int entry_year;
		int entry_track_total;
		int entry_disc_number;
		int entry_disc_total;
		double entry_rating;
		const char *entry_artist_sortname;
		const char *entry_album_sortname;
		const char *entry_album_artist_sortname;
		const char *entry_composer_sortname;

		entry = (RhythmDBEntry*)l->data;
		entry_artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
		entry_album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
		entry_album_artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM_ARTIST);
		entry_composer = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_COMPOSER);
		entry_genre = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE);
		entry_year = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_YEAR);
		entry_track_total = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_TOTAL);
		entry_disc_number = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER);
		entry_disc_total = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_TOTAL);
		entry_rating = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_RATING);
		entry_artist_sortname = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST_SORTNAME);
		entry_album_sortname = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM_SORTNAME);
		entry_album_artist_sortname = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME);
		entry_composer_sortname = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_COMPOSER_SORTNAME);

		/* grab first valid values */
		if (artist == NULL)
			artist = entry_artist;
		if (album == NULL)
			album = entry_album;
		if (album_artist == NULL)
			album_artist = entry_album_artist;
		if (composer == NULL)
			composer = entry_composer;
		if (genre == NULL)
			genre = entry_genre;
		if (year == 0)
			year = entry_year;
		if (track_total == 0)
			track_total = entry_track_total;
		if (disc_number == 0)
			disc_number = entry_disc_number;
		if (disc_total == 0)
			disc_total = entry_disc_total;
		if (fabs(rating) < EPSILON)
			rating = entry_rating;
		if (artist_sortname == NULL)
			artist_sortname = entry_artist_sortname;
		if (album_sortname == NULL)
			album_sortname = entry_album_sortname;
		if (album_artist_sortname == NULL)
			album_artist_sortname = entry_album_artist_sortname;
		if (composer_sortname == NULL)
			composer_sortname = entry_composer_sortname;

		/* locate mixed values */
		if (artist != entry_artist)
			mixed_artists = TRUE;
		if (album != entry_album)
			mixed_albums = TRUE;
		if (album_artist != entry_album_artist)
			mixed_album_artists = TRUE;
		if (composer != entry_composer)
			mixed_composers = TRUE;
		if (genre != entry_genre)
			mixed_genres = TRUE;
		if (year != entry_year)
			mixed_years = TRUE;
		if (track_total != entry_track_total)
			mixed_track_totals = TRUE;
		if (disc_number != entry_disc_number)
			mixed_disc_numbers = TRUE;
		if (disc_total != entry_disc_total)
			mixed_disc_totals = TRUE;
		if (fabs(rating - entry_rating) >= EPSILON)
			mixed_ratings = TRUE;
		if (artist_sortname != entry_artist_sortname)
			mixed_artist_sortnames = TRUE;
		if (album_sortname != entry_album_sortname)
			mixed_album_sortnames = TRUE;
		if (album_artist_sortname != entry_album_artist_sortname)
			mixed_album_artist_sortnames = TRUE;
		if (composer_sortname != entry_composer_sortname)
			mixed_composer_sortnames = TRUE;

		/* don't continue search if everything is mixed */
		if (mixed_artists && mixed_albums && mixed_album_artists &&
		    mixed_composers && mixed_genres && mixed_years &&
		    mixed_track_totals && mixed_disc_numbers &&
		    mixed_disc_totals && mixed_ratings &&
		    mixed_artist_sortnames && mixed_album_sortnames &&
		    mixed_album_artist_sortnames && mixed_composer_sortnames)
			break;
	}

	if (!mixed_artists && artist != NULL)
		gtk_editable_set_text (GTK_EDITABLE (song_info->priv->artist), artist);
	if (!mixed_albums && album != NULL)
		gtk_editable_set_text (GTK_EDITABLE (song_info->priv->album), album);
	if (!mixed_album_artists && album_artist != NULL)
		gtk_editable_set_text (GTK_EDITABLE (song_info->priv->album_artist), album_artist);
	if (!mixed_composers && composer != NULL)
		gtk_editable_set_text (GTK_EDITABLE (song_info->priv->composer), composer);
	if (!mixed_genres && genre != NULL)
		gtk_editable_set_text (GTK_EDITABLE (song_info->priv->genre), genre);
	if (!mixed_years && year != 0)
		rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->year), year);
	if (!mixed_track_totals && track_total != 0)
		rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->track_total), track_total);
	if (!mixed_disc_numbers && disc_number != 0)
		rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->disc_cur), disc_number);
	if (!mixed_disc_totals && disc_total != 0)
		rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->disc_total), disc_total);
	if (!mixed_ratings && fabs(rating) >= EPSILON)
		g_object_set (G_OBJECT (song_info->priv->rating), "rating", rating, NULL);
	if (!mixed_artist_sortnames && artist_sortname != NULL)
		gtk_editable_set_text (GTK_EDITABLE (song_info->priv->artist_sortname), artist_sortname);
	if (!mixed_album_sortnames && album_sortname != NULL)
		gtk_editable_set_text (GTK_EDITABLE (song_info->priv->album_sortname), album_sortname);
	if (!mixed_album_artist_sortnames && album_artist_sortname != NULL)
		gtk_editable_set_text (GTK_EDITABLE (song_info->priv->album_artist_sortname), album_artist_sortname);
	if (!mixed_composer_sortnames && composer_sortname != NULL)
		gtk_editable_set_text (GTK_EDITABLE (song_info->priv->composer_sortname), composer_sortname);
}

static void
rb_song_info_populate_dialog (RBSongInfo *song_info)
{
	const char *text;
	char *tmp;
	gulong num;
	gdouble dnum;

	g_assert (song_info->priv->current_entry);

	/* update the buttons sensitivity */
	rb_song_info_update_buttons (song_info);

	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_TITLE);
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->title), text);

	tmp = g_strdup_printf (_("%s Properties"), text);
	adw_dialog_set_title (ADW_DIALOG (song_info), tmp);
	g_free (tmp);

	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ARTIST);
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->artist), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ALBUM);
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->album), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ALBUM_ARTIST);
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->album_artist), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_COMPOSER);
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->composer), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_GENRE);
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->genre), text);

	num = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_TRACK_NUMBER);
	rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->track_cur), num);
	num = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_TRACK_TOTAL);
	rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->track_total), num);
	num = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_DISC_NUMBER);
	rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->disc_cur), num);
	num = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_DISC_TOTAL);
	rb_song_info_populate_num_field (GTK_ENTRY (song_info->priv->disc_total), num);
	dnum = rhythmdb_entry_get_double (song_info->priv->current_entry, RHYTHMDB_PROP_BPM);
	rb_song_info_populate_dnum_field (GTK_ENTRY (song_info->priv->bpm), dnum);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_COMMENT);
	gtk_text_buffer_set_text (song_info->priv->comment_buffer, text, -1);

	rb_song_info_update_duration (song_info);
	rb_song_info_update_location (song_info);
	rb_song_info_update_filesize (song_info);
	rb_song_info_update_date_added (song_info);
	rb_song_info_update_play_count (song_info);
	rb_song_info_update_last_played (song_info);
	rb_song_info_update_bitrate (song_info);
	rb_song_info_update_rating (song_info);
	rb_song_info_update_year (song_info);
	rb_song_info_update_playback_error (song_info);

	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_TITLE_SORTNAME);
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->title_sortname), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ARTIST_SORTNAME);
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->artist_sortname), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ALBUM_SORTNAME);
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->album_sortname), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME);
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->album_artist_sortname), text);
	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_COMPOSER_SORTNAME);
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->composer_sortname), text);
}

static void
rb_song_info_update_playback_error (RBSongInfo *song_info)
{
	char *message = NULL;

	if (!song_info->priv->current_entry)
		return;

	message = rhythmdb_entry_dup_string (song_info->priv->current_entry, RHYTHMDB_PROP_PLAYBACK_ERROR);

	if (message) {
		gtk_label_set_text (GTK_LABEL (song_info->priv->playback_error_label),
				    message);
		gtk_widget_set_visible (song_info->priv->playback_error_box, TRUE);
	} else {
		gtk_label_set_text (GTK_LABEL (song_info->priv->playback_error_label),
				    "No errors");
		gtk_widget_set_visible (song_info->priv->playback_error_box, FALSE);
	}

	g_free (message);
}

static void
rb_song_info_update_bitrate (RBSongInfo *song_info)
{
	char *tmp;
	gulong bitrate;

	bitrate = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_BITRATE);

	if (rhythmdb_entry_is_lossless (song_info->priv->current_entry)) {
		tmp = g_strdup (_("Lossless"));
	} else if (bitrate == 0) {
		tmp = g_strdup (_("Unknown"));
	} else {
		tmp = g_strdup_printf (_("%lu kbps"), bitrate);
	}

	gtk_label_set_text (GTK_LABEL (song_info->priv->bitrate),
			    tmp);
	g_free (tmp);
}

static void
rb_song_info_update_duration (RBSongInfo *song_info)
{
	char *text;
	long duration;

	duration = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_DURATION);

	text = rb_make_duration_string (duration);
	gtk_label_set_text (GTK_LABEL (song_info->priv->duration), text);
	g_free (text);
}

static void
rb_song_info_update_filesize (RBSongInfo *song_info)
{
	char *text = NULL;
	guint64 filesize = 0;
	filesize = rhythmdb_entry_get_uint64 (song_info->priv->current_entry, RHYTHMDB_PROP_FILE_SIZE);
	text = g_format_size (filesize);
	gtk_label_set_text (GTK_LABEL (song_info->priv->filesize), text);
	g_free (text);
}

static void
rb_song_info_update_location (RBSongInfo *song_info)
{
	const char *text;

	g_return_if_fail (song_info != NULL);

	text = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_LOCATION);

	if (text != NULL) {
		char *tmp;
		char *tmp_utf8;
		char *basename;

		basename = g_path_get_basename (text);
		tmp = g_uri_unescape_string (basename, NULL);
		g_free (basename);
		tmp_utf8 = g_filename_to_utf8 (tmp, -1, NULL, NULL, NULL);
		g_free (tmp);
		tmp = NULL;

		if (tmp_utf8 != NULL) {
			gtk_editable_set_text (GTK_EDITABLE (song_info->priv->name),
					    tmp_utf8);
		} else {
			gtk_editable_set_text (GTK_EDITABLE (song_info->priv->name),
					    _("Unknown file name"));
		}

		g_free (tmp_utf8);
		tmp_utf8 = NULL;

		if (rb_uri_is_local (text)) {
			const char *desktopdir;
			char *dir;

			/* for local files, convert to path, extract dirname, and convert to utf8 */
			tmp = g_filename_from_uri (text, NULL, NULL);

			dir = g_path_get_dirname (tmp);
			g_free (tmp);
			tmp_utf8 = g_filename_to_utf8 (dir, -1, NULL, NULL, NULL);
			g_free (dir);

			/* special case for files on the desktop */
			desktopdir = g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP);
			if (g_strcmp0 (tmp_utf8, desktopdir) == 0) {
				g_free (tmp_utf8);
				tmp_utf8 = g_strdup (_("On the desktop"));
			}
		} else {
			GFile *file;
			GFile *parent;
			char *parent_uri;

			/* get parent URI and unescape it */
			file = g_file_new_for_uri (text);
			parent = g_file_get_parent (file);
			parent_uri = g_file_get_uri (parent);
			g_object_unref (file);
			g_object_unref (parent);

			tmp_utf8 = g_uri_unescape_string (parent_uri, NULL);
			g_free (parent_uri);
		}

		if (tmp_utf8 != NULL) {
			gtk_editable_set_text (GTK_EDITABLE (song_info->priv->location),
					    tmp_utf8);
		} else {
			gtk_editable_set_text (GTK_EDITABLE (song_info->priv->location),
					    _("Unknown location"));
		}
		g_free (tmp_utf8);
	}
}

static void
rb_song_info_backward_clicked_cb (GtkWidget *button,
				  RBSongInfo *song_info)
{
	RhythmDBEntry *new_entry;

	rb_song_info_sync_entries (RB_SONG_INFO (song_info));
	new_entry = rhythmdb_query_model_get_previous_from_entry (song_info->priv->query_model,
								  song_info->priv->current_entry);
	g_return_if_fail (new_entry != NULL);

	song_info->priv->current_entry = new_entry;
	rb_entry_view_select_entry (song_info->priv->entry_view, new_entry);
	rb_entry_view_scroll_to_entry (song_info->priv->entry_view, new_entry);

	rb_song_info_populate_dialog (song_info);
	g_object_notify (G_OBJECT (song_info), "current-entry");
	rhythmdb_entry_unref (new_entry);
}

static void
rb_song_info_forward_clicked_cb (GtkWidget *button,
				 RBSongInfo *song_info)
{
	RhythmDBEntry *new_entry;

	rb_song_info_sync_entries (RB_SONG_INFO (song_info));
	new_entry = rhythmdb_query_model_get_next_from_entry (song_info->priv->query_model,
							      song_info->priv->current_entry);
	g_return_if_fail (new_entry != NULL);

	song_info->priv->current_entry = new_entry;
	rb_entry_view_select_entry (song_info->priv->entry_view, new_entry);
	rb_entry_view_scroll_to_entry (song_info->priv->entry_view, new_entry);

	rb_song_info_populate_dialog (song_info);
	g_object_notify (G_OBJECT (song_info), "current-entry");

	rhythmdb_entry_unref (new_entry);
}

/*
 * rb_song_info_update_buttons: update back/forward sensitivity
 */
static void
rb_song_info_update_buttons (RBSongInfo *song_info)
{
	RhythmDBEntry *entry = NULL;

	g_return_if_fail (song_info != NULL);
	g_return_if_fail (song_info->priv->query_model != NULL);

	if (!song_info->priv->current_entry)
		return;

	/* backward */
	entry = rhythmdb_query_model_get_previous_from_entry (song_info->priv->query_model,
							      song_info->priv->current_entry);

	gtk_widget_set_sensitive (song_info->priv->backward, entry != NULL);
	if (entry != NULL)
		rhythmdb_entry_unref (entry);

	/* forward */
	entry = rhythmdb_query_model_get_next_from_entry (song_info->priv->query_model,
							  song_info->priv->current_entry);

	gtk_widget_set_sensitive (song_info->priv->forward, entry != NULL);
	if (entry != NULL)
		rhythmdb_entry_unref (entry);
}

static void
rb_song_info_query_model_inserted_cb (RhythmDBQueryModel *model,
				      GtkTreePath *path,
				      GtkTreeIter *iter,
				      RBSongInfo *song_info)
{
	rb_song_info_update_buttons (song_info);
}

static void
rb_song_info_query_model_deleted_cb (RhythmDBQueryModel *model,
				     RhythmDBEntry*entry,
				     RBSongInfo *song_info)
{
	rb_song_info_update_buttons (song_info);
}

static void
rb_song_info_query_model_reordered_cb (RhythmDBQueryModel *model,
				       GtkTreePath *path,
				       GtkTreeIter *iter,
				       gpointer *map,
				       RBSongInfo *song_info)
{
	rb_song_info_update_buttons (song_info);
}

static void
rb_song_info_base_query_model_changed_cb (GObject *source,
					  GParamSpec *whatever,
					  RBSongInfo *song_info)
{
	RhythmDBQueryModel *base_query_model;

	g_object_get (source, "base-query-model", &base_query_model, NULL);

	if (song_info->priv->albums) {
		g_object_unref (song_info->priv->albums);
	}
	if (song_info->priv->artists) {
		g_object_unref (song_info->priv->artists);
	}
	if (song_info->priv->genres) {
		g_object_unref (song_info->priv->genres);
	}

	song_info->priv->albums  = rhythmdb_property_model_new (song_info->priv->db, RHYTHMDB_PROP_ALBUM);
	song_info->priv->artists = rhythmdb_property_model_new (song_info->priv->db, RHYTHMDB_PROP_ARTIST);
	song_info->priv->genres  = rhythmdb_property_model_new (song_info->priv->db, RHYTHMDB_PROP_GENRE);

	g_object_set (song_info->priv->albums,  "query-model", base_query_model, NULL);
	g_object_set (song_info->priv->artists, "query-model", base_query_model, NULL);
	g_object_set (song_info->priv->genres,  "query-model", base_query_model, NULL);

	g_object_unref (base_query_model);
}

static void
rb_song_info_query_model_changed_cb (GObject *source,
				     GParamSpec *whatever,
				     RBSongInfo *song_info)
{
	if (song_info->priv->query_model) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (song_info->priv->query_model),
						      G_CALLBACK (rb_song_info_query_model_inserted_cb),
						      song_info);
		g_signal_handlers_disconnect_by_func (G_OBJECT (song_info->priv->query_model),
						      G_CALLBACK (rb_song_info_query_model_deleted_cb),
						      song_info);
		g_signal_handlers_disconnect_by_func (G_OBJECT (song_info->priv->query_model),
						      G_CALLBACK (rb_song_info_query_model_reordered_cb),
						      song_info);

		g_object_unref (G_OBJECT (song_info->priv->query_model));
	}

	g_object_get (source, "query-model", &song_info->priv->query_model, NULL);

	g_signal_connect_object (G_OBJECT (song_info->priv->query_model),
				 "row-inserted", G_CALLBACK (rb_song_info_query_model_inserted_cb),
				 song_info, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->query_model),
				 "row-changed", G_CALLBACK (rb_song_info_query_model_inserted_cb),
				 song_info, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->query_model),
				 "post-entry-delete", G_CALLBACK (rb_song_info_query_model_deleted_cb),
				 song_info, 0);
	g_signal_connect_object (G_OBJECT (song_info->priv->query_model),
				 "rows-reordered", G_CALLBACK (rb_song_info_query_model_reordered_cb),
				 song_info, 0);

	/* update next button sensitivity */
	rb_song_info_update_buttons (song_info);
}

static void
rb_song_info_update_play_count (RBSongInfo *song_info)
{
	gulong num;
	char *text;

	num = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_PLAY_COUNT);
	text = g_strdup_printf ("%ld", num);
	gtk_label_set_text (GTK_LABEL (song_info->priv->play_count), text);
	g_free (text);
}

static void
rb_song_info_update_last_played (RBSongInfo *song_info)
{
	const char *str;
	str = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_LAST_PLAYED_STR);
	if (!strcmp ("", str))
		str = _("Never");
	gtk_label_set_text (GTK_LABEL (song_info->priv->last_played), str);
}

static void
rb_song_info_update_rating (RBSongInfo *song_info)
{
	double rating;

	g_return_if_fail (RB_IS_SONG_INFO (song_info));

	rating = rhythmdb_entry_get_double (song_info->priv->current_entry, RHYTHMDB_PROP_RATING);
	g_object_set (song_info->priv->rating,
		      "rating", rating,
		      NULL);
}

static void
rb_song_info_update_year (RBSongInfo *song_info)
{
	gulong year;
	char *text;

	year = rhythmdb_entry_get_ulong (song_info->priv->current_entry, RHYTHMDB_PROP_YEAR);
	if (year > 0) {
		text = g_strdup_printf ("%lu", year);
	} else {
		text = g_strdup (_("Unknown"));
	}
	gtk_editable_set_text (GTK_EDITABLE (song_info->priv->year), text);
	g_free (text);
}

static void
rb_song_info_update_date_added (RBSongInfo *song_info)
{
	const char *str;
	str = rhythmdb_entry_get_string (song_info->priv->current_entry, RHYTHMDB_PROP_FIRST_SEEN_STR);
	gtk_label_set_text (GTK_LABEL (song_info->priv->date_added), str);
}

static gboolean
sync_string_property_multiple (RBSongInfo *dialog, RhythmDBPropType property, GtkWidget *entry)
{
	const char *new_text;
	GValue val = {0,};
	GList *t;
	gboolean changed = FALSE;

	new_text = gtk_editable_get_text (GTK_EDITABLE (entry));
	if (strlen (new_text) == 0)
		return FALSE;

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, new_text);
	for (t = dialog->priv->selected_entries; t != NULL; t = t->next) {
		const char *entry_value;
		RhythmDBEntry *dbentry;

		dbentry = (RhythmDBEntry *)t->data;
		entry_value = rhythmdb_entry_get_string (dbentry, property);

		if (g_strcmp0 (new_text, entry_value) == 0)
			continue;
		rhythmdb_entry_set (dialog->priv->db, dbentry, property, &val);
		changed = TRUE;
	}
	g_value_unset (&val);
	return changed;
}

static gboolean
sync_ulong_property_multiple (RBSongInfo *dialog, RhythmDBPropType property, GtkWidget *entry)
{
	const char *new_text;
	gint val_int;
	GValue val = {0,};
	GList *tem;
	gboolean changed = FALSE;
	char *endptr;

	new_text = gtk_editable_get_text (GTK_EDITABLE (entry));
	val_int = g_ascii_strtoull (new_text, &endptr, 10);

	if (endptr != new_text) {

		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, val_int);

		for (tem = dialog->priv->selected_entries; tem; tem = tem->next) {
			RhythmDBEntry *dbentry;
			gulong entry_num;

			dbentry = (RhythmDBEntry *)tem->data;
			entry_num = rhythmdb_entry_get_ulong (dbentry, property);

			if (val_int != entry_num) {
				rhythmdb_entry_set (dialog->priv->db, dbentry,
						    property, &val);
				changed = TRUE;
			}
		}
		g_value_unset (&val);
	}
	return changed;
}

static void
rb_song_info_sync_entries_multiple (RBSongInfo *dialog)
{
	const char *year_str = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->year));
	char *endptr;
	GValue val = {0,};
	GList *tem;
	gint year;
	gboolean changed = FALSE;
	RhythmDBEntry *entry;

	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ALBUM, dialog->priv->album);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ARTIST, dialog->priv->artist);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ALBUM_ARTIST, dialog->priv->album_artist);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_COMPOSER, dialog->priv->composer);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_GENRE, dialog->priv->genre);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ARTIST_SORTNAME, dialog->priv->artist_sortname);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ALBUM_SORTNAME, dialog->priv->album_sortname);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME, dialog->priv->album_artist_sortname);
	changed |= sync_string_property_multiple (dialog, RHYTHMDB_PROP_COMPOSER_SORTNAME, dialog->priv->composer_sortname);

	if (strlen (year_str) > 0) {
		GDate *date = NULL;
		GType type;

		/* note: this will reset the day-of-year to Jan 1 for all entries */
		year = g_ascii_strtoull (year_str, &endptr, 10);
		if (year > 0)
			date = g_date_new_dmy (1, G_DATE_JANUARY, year);

		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_DATE);

		g_value_init (&val, type);
		g_value_set_ulong (&val, (date ? g_date_get_julian (date) : 0));

		for (tem = dialog->priv->selected_entries; tem; tem = tem->next) {
			entry = (RhythmDBEntry *)tem->data;
			rhythmdb_entry_set (dialog->priv->db, entry,
					    RHYTHMDB_PROP_DATE, &val);
			changed = TRUE;
		}
		g_value_unset (&val);
		if (date)
			g_date_free (date);

	}

	changed |= sync_ulong_property_multiple (dialog, RHYTHMDB_PROP_TRACK_TOTAL, dialog->priv->track_total);
	changed |= sync_ulong_property_multiple (dialog, RHYTHMDB_PROP_DISC_NUMBER, dialog->priv->disc_cur);
	changed |= sync_ulong_property_multiple (dialog, RHYTHMDB_PROP_DISC_TOTAL, dialog->priv->disc_total);

	if (changed)
		rhythmdb_commit (dialog->priv->db);
}

static gboolean
sync_property_ulong_single (RBSongInfo *dialog,
			    RhythmDBEntry *entry,
			    guint property,
			    GtkWidget *w)
{
	char *endptr;

	const char *new_text = gtk_editable_get_text (GTK_EDITABLE (w));
	gulong prop_val = g_ascii_strtoull (new_text, &endptr, 10);
	gulong entry_val = rhythmdb_entry_get_ulong (entry, property);

	if ((endptr != new_text) && (prop_val != entry_val)) {
		GValue val = {0,};

		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, prop_val);
		rhythmdb_entry_set (dialog->priv->db, entry, property, &val);

		return TRUE;
	}
	return FALSE;
}

static gboolean
sync_property_string_single (RBSongInfo *dialog,
			     RhythmDBEntry *dbentry,
			     guint property,
			     const gchar *prop_val)
{
	const char *entry_string = rhythmdb_entry_get_string (dbentry,
							      property);
	if (g_strcmp0 (prop_val, entry_string)) {
		GValue val = {0,};

		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, prop_val);
		rhythmdb_entry_set (dialog->priv->db, dbentry,
				    property, &val);
		return TRUE;
	}
	return FALSE;
}

static void
rb_song_info_sync_entry_single (RBSongInfo *dialog)
{
	const char *title;
	const char *genre;
	const char *artist;
	const char *album;
	const char *album_artist;
	const char *composer;
	const char *year_str;
	const char *title_sortname;
	const char *artist_sortname;
	const char *album_sortname;
	const char *album_artist_sortname;
	const char *composer_sortname;
	const char *bpm_str;
	char *comment = NULL;
	char *endptr;
	GType type;
	gulong year;
	gulong entry_val;
	gdouble bpm;
	gdouble dentry_val;
	GValue val = {0,};
	gboolean changed = FALSE;
	RhythmDBEntry *entry = dialog->priv->current_entry;
	GtkTextIter start, end;

	title = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->title));
	genre = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->genre));
	artist = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->artist));
	album = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->album));
	album_artist = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->album_artist));
	composer = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->composer));
	year_str = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->year));
	title_sortname = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->title_sortname));
	artist_sortname = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->artist_sortname));
	album_sortname = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->album_sortname));
	album_artist_sortname = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->album_artist_sortname));
	composer_sortname = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->composer_sortname));

	/* Get comment text (string is allocated) */
	gtk_text_buffer_get_bounds (dialog->priv->comment_buffer, &start, &end);
	comment = gtk_text_buffer_get_text (dialog->priv->comment_buffer, &start, &end, FALSE);

	g_signal_emit (dialog, rb_song_info_signals[PRE_METADATA_CHANGE], 0,
		       entry);

	changed |= sync_property_ulong_single (dialog,
					       entry,
					       RHYTHMDB_PROP_TRACK_NUMBER,
					       dialog->priv->track_cur);
	changed |= sync_property_ulong_single (dialog,
					       entry,
					       RHYTHMDB_PROP_TRACK_TOTAL,
					       dialog->priv->track_total);
	changed |= sync_property_ulong_single (dialog,
					       entry,
					       RHYTHMDB_PROP_DISC_NUMBER,
					       dialog->priv->disc_cur);
	changed |= sync_property_ulong_single (dialog,
					       entry,
					       RHYTHMDB_PROP_DISC_TOTAL,
					       dialog->priv->disc_total);

	year = g_ascii_strtoull (year_str, &endptr, 10);
	entry_val = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_YEAR);
	if ((endptr != year_str) &&
	    (year != entry_val ||
	     (entry_val == 0 && year > 0))) {
		GDate *date = NULL;

		if (year > 0) {
			if (entry_val > 0) {
				gulong julian;

				julian = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE);
				date = g_date_new_julian (julian);
				g_date_set_year (date, year);
			} else {
				date = g_date_new_dmy (1, G_DATE_JANUARY, year);
			}
		}

		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_DATE);
		g_value_init (&val, type);
		g_value_set_ulong (&val, (date ? g_date_get_julian (date) : 0));
		rhythmdb_entry_set (dialog->priv->db, entry, RHYTHMDB_PROP_DATE, &val);
		changed = TRUE;

		g_value_unset (&val);
		if (date)
			g_date_free (date);
	}
	bpm_str = gtk_editable_get_text (GTK_EDITABLE (dialog->priv->bpm));
	bpm = g_strtod (bpm_str, &endptr);
	dentry_val = rhythmdb_entry_get_double (entry, RHYTHMDB_PROP_BPM);
	if ((endptr != bpm_str) && (bpm != dentry_val)) {
		type = rhythmdb_get_property_type (dialog->priv->db,
						   RHYTHMDB_PROP_BPM);
		g_value_init (&val, type);
		g_value_set_double (&val, bpm);
		rhythmdb_entry_set (dialog->priv->db, entry, RHYTHMDB_PROP_BPM, &val);
		g_value_unset (&val);
		changed = TRUE;
	}

	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_TITLE, title);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ALBUM, album);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ARTIST, artist);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ALBUM_ARTIST, album_artist);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_COMPOSER, composer);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_GENRE, genre);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_TITLE_SORTNAME, title_sortname);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ARTIST_SORTNAME, artist_sortname);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ALBUM_SORTNAME, album_sortname);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_COMMENT, comment);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME, album_artist_sortname);
	changed |= sync_property_string_single (dialog, entry, RHYTHMDB_PROP_COMPOSER_SORTNAME, composer_sortname);

	/* FIXME: when an entry is SYNCed, a changed signal is emitted, and
	 * this signal is also emitted, aren't they redundant?
	 */
	g_signal_emit (G_OBJECT (dialog), rb_song_info_signals[POST_METADATA_CHANGE], 0,
		       entry);

	if (changed)
		rhythmdb_commit (dialog->priv->db);

	g_free (comment);
}

static void
rb_song_info_sync_entries (RBSongInfo *dialog)
{
	if (!dialog->priv->editable)
		return;

	if (dialog->priv->current_entry)
		rb_song_info_sync_entry_single (dialog);
	else
		rb_song_info_sync_entries_multiple (dialog);
}
