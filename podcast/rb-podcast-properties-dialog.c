/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho <renato.filho@indt.org>
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
#include <errno.h>

#include <glib/gi18n.h>
#include <adwaita.h>

#include "rb-podcast-properties-dialog.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-dialog.h"
#include "rb-rating.h"
#include "rb-util.h"
#include "rb-cut-and-paste-code.h"
#include "rb-debug.h"

static void rb_podcast_properties_dialog_class_init (RBPodcastPropertiesDialogClass *klass);
static void rb_podcast_properties_dialog_init (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_dispose (GObject *object);
static void rb_podcast_properties_dialog_finalize (GObject *object);
static void rb_podcast_properties_dialog_set_property (GObject *object,
						       guint prop_id,
						       const GValue *value,
						       GParamSpec *pspec);
static void rb_podcast_properties_dialog_get_property (GObject *object,
						       guint prop_id,
						       GValue *value,
						       GParamSpec *pspec);
static void rb_podcast_properties_dialog_setup (RBPodcastPropertiesDialog *dialog);
static gboolean rb_podcast_properties_dialog_get_current_entry (RBPodcastPropertiesDialog *dialog);

static void rb_podcast_properties_dialog_update (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_title (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_title_label (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_feed (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_location (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_download_location (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_duration (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_play_count (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_bitrate (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_last_played (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_rating (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_date (RBPodcastPropertiesDialog *dialog);
static void rb_podcast_properties_dialog_update_description (RBPodcastPropertiesDialog *dialog);
static gchar* rb_podcast_properties_dialog_parse_time (gulong time);
static void rb_podcast_properties_dialog_rated_cb (RBRating *rating,
						   double score,
						   RBPodcastPropertiesDialog *dialog);

struct RBPodcastPropertiesDialogPrivate
{
	RBEntryView *entry_view;
	RhythmDB *db;
	RhythmDBEntry *current_entry;

	GtkWidget   *title;
	GtkWidget   *feed;
	GtkWidget   *location;
	GtkWidget   *download_location;
	GtkWidget   *duration;
	GtkWidget   *lastplayed;
	GtkWidget   *playcount;
	GtkWidget   *bitrate;
	GtkWidget   *rating;
	GtkWidget   *date;
	GtkWidget   *description;
};

enum
{
	PROP_0,
	PROP_ENTRY_VIEW,
	PROP_BACKEND
};

G_DEFINE_TYPE_WITH_PRIVATE (RBPodcastPropertiesDialog, rb_podcast_properties_dialog, ADW_TYPE_DIALOG)

/* list of HTML-ish strings that we search for to distinguish plain text from HTML podcast
 * descriptions.  we don't really have anything else to go on - regular content type
 * sniffing only works for proper HTML documents, but these are just tiny fragments, usually
 * with some simple formatting tags.  if we find any of these in a podcast description,
 * we'll strip HTML tags and attempt to decode entities.
 */
static const char *html_clues[] = {
	"<p>",
	"<a ",
	"<b>",
	"<i>",
	"<ul>",
	"<br",
	"<div ",
	"<div>",
	"<img ",
	"&lt;",
	"&gt;",
	"&amp;",
	"&quot;",
	"&apos;",
	"&lsquo;",
	"&rsquo;",
	"&ldquo;",
	"&rdquo;",
	"&#",
};

static char *
unhtml (const char *str)
{
	const char *p;
	char *out, *o, *e;
	enum {
		NORMAL,
		TAG,
		ENTITY,
		BAD_ENTITY
	} state;
	char entity[6];
	int elen;

	out = g_malloc (strlen (str) + 1);

	p = str;
	o = out;
	state = NORMAL;
	e = entity;
	elen = 0;
	while (*p != '\0') {
		switch (state) {
		case TAG:
			if (*p == '>') {
				state = NORMAL;
			}
			break;

		case BAD_ENTITY:
			switch (*p) {
			case ';':
			case ' ':
				*o++ = '?';
				state = NORMAL;
				break;
			default:
				break;
			}
			break;

		case ENTITY:
			if (*p == ';' || *p == ' ') {
				*e++ = '\0';
				if (strncmp (entity, "amp", sizeof(entity)) == 0) {
					*o++ = '&';
				} else if (strncmp (entity, "lt", sizeof(entity)) == 0) {
					*o++ = '<';
				} else if (strncmp (entity, "gt", sizeof(entity)) == 0) {
					*o++ = '>';
				} else if (strncmp (entity, "quot", sizeof(entity)) == 0) {
					*o++ = '"';
				} else if (strncmp (entity, "nbsp", sizeof(entity)) == 0) {
					*o++ = ' ';
				} else if (strncmp (entity, "lrm", sizeof(entity)) == 0) {
					o += g_unichar_to_utf8 (0x200e, o);
				} else if (strncmp (entity, "rlm", sizeof(entity)) == 0) {
					o += g_unichar_to_utf8 (0x200f, o);
				} else if (strncmp (entity, "ndash", sizeof(entity)) == 0) {
					o += g_unichar_to_utf8 (0x2013, o);
				} else if (strncmp (entity, "mdash", sizeof(entity)) == 0) {
					o += g_unichar_to_utf8 (0x2014, o);
				} else if (strncmp (entity, "lsquo", sizeof(entity)) == 0) {
					o += g_unichar_to_utf8 (0x2018, o);
				} else if (strncmp (entity, "rsquo", sizeof(entity)) == 0) {
					o += g_unichar_to_utf8 (0x2019, o);
				} else if (strncmp (entity, "ldquo", sizeof(entity)) == 0) {
					o += g_unichar_to_utf8 (0x201c, o);
				} else if (strncmp (entity, "rdquo", sizeof(entity)) == 0) {
					o += g_unichar_to_utf8 (0x201d, o);
				} else if (entity[0] == '#') {
					int base = 10;
					char *str = entity + 1;
					char *end = NULL;
					gulong l;

					if (str[0] == 'x') {
						base = 16;
						str++;
					}

					errno = 0;
					l = strtoul (str, &end, base);
					if (end == str || errno != 0 || *end != '\0') {
						*o++ = '?';
					} else {
						o += g_unichar_to_utf8 (l, o);
					}
				} else if (elen == 0) {
					/* bare ampersand */
					*o++ = '&';
					*o++ = *p;
				} else {
					/* unsupported entity */
					*o++ = '?';
				}
				state = NORMAL;
				break;
			}
			elen++;
			if (elen == sizeof(entity)) {
				state = BAD_ENTITY;
				break;
			}
			*e++ = *p;
			break;

		case NORMAL:
			switch (*p) {
			case '<':
				state = TAG;
				break;
			case '&':
				state = ENTITY;
				e = entity;
				elen = 0;
				break;
			default:
				*o++ = *p;
			}
		}
		p++;
	}

	*o++ = '\0';
	return out;
}

static void
rb_podcast_properties_dialog_class_init (RBPodcastPropertiesDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = rb_podcast_properties_dialog_set_property;
	object_class->get_property = rb_podcast_properties_dialog_get_property;

	g_object_class_install_property (object_class,
					 PROP_ENTRY_VIEW,
					 g_param_spec_object ("entry-view",
					                      "RBEntryView",
					                      "RBEntryView object",
					                      RB_TYPE_ENTRY_VIEW,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	object_class->dispose = rb_podcast_properties_dialog_dispose;
	object_class->finalize = rb_podcast_properties_dialog_finalize;

}

static void
rb_podcast_properties_dialog_init (RBPodcastPropertiesDialog *dialog)
{
	dialog->priv = rb_podcast_properties_dialog_get_instance_private (dialog);
}

/* Helper: add a label row to a grid */
static void
add_label_row (GtkGrid *grid, int row, const char *desc_text, const char *desc_id,
	       GtkWidget **value_widget, gboolean selectable, gboolean wrap)
{
	GtkWidget *desc;

	desc = gtk_label_new (desc_text);
	gtk_label_set_xalign (GTK_LABEL (desc), 0.0);
	gtk_widget_set_halign (desc, GTK_ALIGN_START);

	/* boldify */
	PangoAttrList *attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes (GTK_LABEL (desc), attrs);
	pango_attr_list_unref (attrs);

	gtk_grid_attach (grid, desc, 0, row, 1, 1);

	*value_widget = gtk_label_new ("-");
	gtk_label_set_xalign (GTK_LABEL (*value_widget), 0.0);
	gtk_widget_set_halign (*value_widget, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (*value_widget, TRUE);
	gtk_label_set_selectable (GTK_LABEL (*value_widget), selectable);
	if (wrap)
		gtk_label_set_wrap (GTK_LABEL (*value_widget), TRUE);

	gtk_grid_attach (grid, *value_widget, 1, row, 1, 1);
}

static void
rb_podcast_properties_dialog_setup (RBPodcastPropertiesDialog *dialog)
{
	GtkWidget *toolbar_view;
	GtkWidget *header_bar;
	GtkWidget *stack;
	GtkWidget *switcher;
	GtkWidget *grid;
	GtkWidget *desc_label;
	GtkWidget *scroll;
	GtkWidget *viewport;
	GtkWidget *rating_box;
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
	adw_dialog_set_content_width (ADW_DIALOG (dialog), 500);
	adw_dialog_set_content_height (ADW_DIALOG (dialog), 450);

	/* ---- Basic page ---- */
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_widget_set_margin_start (grid, 12);
	gtk_widget_set_margin_end (grid, 12);
	gtk_widget_set_margin_top (grid, 12);
	gtk_widget_set_margin_bottom (grid, 12);
	row = 0;

	add_label_row (GTK_GRID (grid), row++, _("Title:"), "titleDescLabel",
		       &dialog->priv->title, TRUE, FALSE);
	add_label_row (GTK_GRID (grid), row++, _("Feed:"), "feedDescLabel",
		       &dialog->priv->feed, TRUE, FALSE);
	gtk_label_set_ellipsize (GTK_LABEL (dialog->priv->feed), PANGO_ELLIPSIZE_MIDDLE);
	add_label_row (GTK_GRID (grid), row++, _("Date:"), "dateDescLabel",
		       &dialog->priv->date, TRUE, FALSE);

	/* Description with scroll */
	desc_label = gtk_label_new (_("Description:"));
	gtk_label_set_xalign (GTK_LABEL (desc_label), 0.0);
	gtk_widget_set_halign (desc_label, GTK_ALIGN_START);
	gtk_widget_set_valign (desc_label, GTK_ALIGN_START);
	{
		PangoAttrList *attrs = pango_attr_list_new ();
		pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
		gtk_label_set_attributes (GTK_LABEL (desc_label), attrs);
		pango_attr_list_unref (attrs);
	}
	gtk_grid_attach (GTK_GRID (grid), desc_label, 0, row, 1, 1);

	dialog->priv->description = gtk_label_new (NULL);
	gtk_label_set_wrap (GTK_LABEL (dialog->priv->description), TRUE);
	gtk_label_set_selectable (GTK_LABEL (dialog->priv->description), TRUE);
	gtk_label_set_xalign (GTK_LABEL (dialog->priv->description), 0.0);
	gtk_label_set_yalign (GTK_LABEL (dialog->priv->description), 0.0);

	viewport = gtk_viewport_new (NULL, NULL);
	gtk_widget_set_size_request (viewport, 300, -1);
	gtk_viewport_set_child (GTK_VIEWPORT (viewport), dialog->priv->description);

	scroll = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
					GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), viewport);
	gtk_widget_set_vexpand (scroll, TRUE);
	gtk_grid_attach (GTK_GRID (grid), scroll, 1, row, 1, 1);

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

	add_label_row (GTK_GRID (grid), row++, _("Source:"), "locationDescLabel",
		       &dialog->priv->location, TRUE, FALSE);
	gtk_label_set_ellipsize (GTK_LABEL (dialog->priv->location), PANGO_ELLIPSIZE_MIDDLE);
	add_label_row (GTK_GRID (grid), row++, _("Download location:"), "downloadLocationDescLabel",
		       &dialog->priv->download_location, FALSE, FALSE);
	gtk_label_set_ellipsize (GTK_LABEL (dialog->priv->download_location), PANGO_ELLIPSIZE_MIDDLE);
	add_label_row (GTK_GRID (grid), row++, _("Duration:"), "durationDescLabel",
		       &dialog->priv->duration, FALSE, FALSE);
	add_label_row (GTK_GRID (grid), row++, _("Bitrate:"), "bitrateDescLabel",
		       &dialog->priv->bitrate, TRUE, FALSE);
	add_label_row (GTK_GRID (grid), row++, _("Last played:"), "lastplayedDescLabel",
		       &dialog->priv->lastplayed, TRUE, FALSE);
	add_label_row (GTK_GRID (grid), row++, _("Play count:"), "playcountDescLabel",
		       &dialog->priv->playcount, TRUE, FALSE);

	/* Rating row — bold label + RBRating widget */
	{
		GtkWidget *rating_desc = gtk_label_new (_("_Rating:"));
		gtk_label_set_use_underline (GTK_LABEL (rating_desc), TRUE);
		gtk_label_set_xalign (GTK_LABEL (rating_desc), 0.0);
		gtk_widget_set_halign (rating_desc, GTK_ALIGN_START);
		PangoAttrList *attrs = pango_attr_list_new ();
		pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
		gtk_label_set_attributes (GTK_LABEL (rating_desc), attrs);
		pango_attr_list_unref (attrs);
		gtk_grid_attach (GTK_GRID (grid), rating_desc, 0, row, 1, 1);

		dialog->priv->rating = GTK_WIDGET (rb_rating_new ());
		g_signal_connect_object (dialog->priv->rating,
					 "rated",
					 G_CALLBACK (rb_podcast_properties_dialog_rated_cb),
					 G_OBJECT (dialog), 0);

		rating_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		gtk_box_append (GTK_BOX (rating_box), dialog->priv->rating);
		gtk_grid_attach (GTK_GRID (grid), rating_box, 1, row, 1, 1);

		/* accessibility */
		gtk_accessible_update_relation (GTK_ACCESSIBLE (dialog->priv->rating),
						GTK_ACCESSIBLE_RELATION_LABELLED_BY,
						rating_desc,
						NULL,
						-1);
	}

	gtk_stack_add_titled (GTK_STACK (stack), grid, "details", _("Details"));
}

static void
rb_podcast_properties_dialog_dispose (GObject *object)
{
	RBPodcastPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PODCAST_PROPERTIES_DIALOG (object));

	dialog = RB_PODCAST_PROPERTIES_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	g_clear_object (&dialog->priv->db);

	G_OBJECT_CLASS (rb_podcast_properties_dialog_parent_class)->dispose (object);
}

static void
rb_podcast_properties_dialog_finalize (GObject *object)
{
	RBPodcastPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PODCAST_PROPERTIES_DIALOG (object));

	dialog = RB_PODCAST_PROPERTIES_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	G_OBJECT_CLASS (rb_podcast_properties_dialog_parent_class)->finalize (object);
}

static void
rb_podcast_properties_dialog_set_entry_view (RBPodcastPropertiesDialog *dialog,
					     RBEntryView               *view)
{
	g_clear_object (&dialog->priv->db);

	dialog->priv->entry_view = view;

	if (dialog->priv->entry_view != NULL) {
		g_object_get (dialog->priv->entry_view,
			      "db", &dialog->priv->db, NULL);
	}
}

static void
rb_podcast_properties_dialog_set_property (GObject *object,
					   guint prop_id,
					   const GValue *value,
					   GParamSpec *pspec)
{
	RBPodcastPropertiesDialog *dialog = RB_PODCAST_PROPERTIES_DIALOG (object);

	switch (prop_id) {
	case PROP_ENTRY_VIEW:
		rb_podcast_properties_dialog_set_entry_view (dialog, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_podcast_properties_dialog_get_property (GObject *object,
					   guint prop_id,
					   GValue *value,
					   GParamSpec *pspec)
{
	RBPodcastPropertiesDialog *dialog = RB_PODCAST_PROPERTIES_DIALOG (object);

	switch (prop_id) {
	case PROP_ENTRY_VIEW:
		g_value_set_object (value, dialog->priv->entry_view);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GtkWidget *
rb_podcast_properties_dialog_new (RBEntryView *entry_view)
{
	RBPodcastPropertiesDialog *dialog;

	g_return_val_if_fail (RB_IS_ENTRY_VIEW (entry_view), NULL);

	dialog = g_object_new (RB_TYPE_PODCAST_PROPERTIES_DIALOG,
			       "entry-view", entry_view, NULL);

	rb_podcast_properties_dialog_setup (dialog);

	if (!rb_podcast_properties_dialog_get_current_entry (dialog)) {
		g_object_unref (G_OBJECT (dialog));
		return NULL;
	}
	rb_podcast_properties_dialog_update (dialog);

	return GTK_WIDGET (dialog);
}

static gboolean
rb_podcast_properties_dialog_get_current_entry (RBPodcastPropertiesDialog *dialog)
{
	GList *selected_entries;

	/* get the entry */
	selected_entries = rb_entry_view_get_selected_entries (dialog->priv->entry_view);

	if ((selected_entries == NULL) ||
	    (selected_entries->data == NULL)) {
		dialog->priv->current_entry = NULL;
		return FALSE;
	}

	dialog->priv->current_entry = selected_entries->data;
	return TRUE;
}

static void
rb_podcast_properties_dialog_update (RBPodcastPropertiesDialog *dialog)
{
	g_return_if_fail (dialog->priv->current_entry != NULL);
	rb_podcast_properties_dialog_update_location (dialog);
	rb_podcast_properties_dialog_update_download_location (dialog);
	rb_podcast_properties_dialog_update_title (dialog);
	rb_podcast_properties_dialog_update_title_label (dialog);
	rb_podcast_properties_dialog_update_feed (dialog);
	rb_podcast_properties_dialog_update_duration (dialog);
	rb_podcast_properties_dialog_update_play_count (dialog);
	rb_podcast_properties_dialog_update_bitrate (dialog);
	rb_podcast_properties_dialog_update_last_played (dialog);
	rb_podcast_properties_dialog_update_rating (dialog);
	rb_podcast_properties_dialog_update_date (dialog);
	rb_podcast_properties_dialog_update_description (dialog);
}

static void
rb_podcast_properties_dialog_update_title (RBPodcastPropertiesDialog *dialog)
{
	const char *name;
	char *tmp;

	name = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
	tmp = g_strdup_printf (_("%s Properties"), name);
	adw_dialog_set_title (ADW_DIALOG (dialog), tmp);
	g_free (tmp);
}

static void
rb_podcast_properties_dialog_update_title_label (RBPodcastPropertiesDialog *dialog)
{
	const char *title;

	title = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
	gtk_label_set_text (GTK_LABEL (dialog->priv->title), title);
}

static void
rb_podcast_properties_dialog_update_feed (RBPodcastPropertiesDialog *dialog)
{
	const char *feed;

	feed = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_ALBUM);
	gtk_label_set_text (GTK_LABEL (dialog->priv->feed), feed);
}

static void
rb_podcast_properties_dialog_update_duration (RBPodcastPropertiesDialog *dialog)
{
        char *text;
        gulong duration = 0;

        duration = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_DURATION);

	text = rb_make_duration_string (duration);
        gtk_label_set_text (GTK_LABEL (dialog->priv->duration), text);
        g_free (text);
}

static void
rb_podcast_properties_dialog_update_location (RBPodcastPropertiesDialog *dialog)
{
	const char *location;
	char *display;

	location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (location == NULL)
		location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LOCATION);
	display = g_uri_unescape_string (location, NULL);
	gtk_label_set_text (GTK_LABEL (dialog->priv->location), display);
	g_free (display);
}

static void
rb_podcast_properties_dialog_update_download_location (RBPodcastPropertiesDialog *dialog)
{
	const char *location;

	location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (location != NULL && location[0] != '\0') {
		char *display;
		location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LOCATION);
		display = g_uri_unescape_string (location, NULL);
		gtk_label_set_text (GTK_LABEL (dialog->priv->download_location), display);
		g_free (display);
	} else {
		gtk_label_set_text (GTK_LABEL (dialog->priv->download_location), _("Not Downloaded"));
	}
}

static void
rb_podcast_properties_dialog_rated_cb (RBRating *rating,
				       double score,
				       RBPodcastPropertiesDialog *dialog)
{
	GValue value = { 0, };

	g_return_if_fail (RB_IS_RATING (rating));
	g_return_if_fail (RB_IS_PODCAST_PROPERTIES_DIALOG (dialog));
	g_return_if_fail (score >= 0 && score <= 5 );

	/* set the new value for the song */
	g_value_init (&value, G_TYPE_DOUBLE);
	g_value_set_double (&value, score);
	rhythmdb_entry_set (dialog->priv->db,
			    dialog->priv->current_entry,
			    RHYTHMDB_PROP_RATING,
			    &value);
	rhythmdb_commit (dialog->priv->db);
	g_value_unset (&value);

	g_object_set (G_OBJECT (dialog->priv->rating),
		      "rating", score,
		      NULL);
}

static void
rb_podcast_properties_dialog_update_play_count (RBPodcastPropertiesDialog *dialog)
{
	gulong count;
	char *text;

	count = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_PLAY_COUNT);
	text = g_strdup_printf ("%ld", count);
	gtk_label_set_text (GTK_LABEL (dialog->priv->playcount), text);
	g_free (text);
}

static void
rb_podcast_properties_dialog_update_bitrate (RBPodcastPropertiesDialog *dialog)
{
        char *tmp = NULL;
        gulong bitrate = 0;

	bitrate = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_BITRATE);
        if (bitrate > 0)
                tmp = g_strdup_printf (_("%lu kbps"), bitrate);
        else
                tmp = g_strdup (_("Unknown"));

	gtk_label_set_text (GTK_LABEL (dialog->priv->bitrate), tmp);
	g_free (tmp);
}

static void
rb_podcast_properties_dialog_update_last_played (RBPodcastPropertiesDialog *dialog)
{
	const char *str;

	str = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LAST_PLAYED_STR);
	gtk_label_set_text (GTK_LABEL (dialog->priv->lastplayed), str);
}

static void
rb_podcast_properties_dialog_update_rating (RBPodcastPropertiesDialog *dialog)
{
	double rating;

	rating = rhythmdb_entry_get_double (dialog->priv->current_entry, RHYTHMDB_PROP_RATING);
	g_object_set (G_OBJECT (dialog->priv->rating), "rating", rating, NULL);
}

static void
rb_podcast_properties_dialog_update_date (RBPodcastPropertiesDialog *dialog)
{
	gulong post_time;
	char *time;

	post_time = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_POST_TIME);
	time = rb_podcast_properties_dialog_parse_time (post_time);

	gtk_label_set_text (GTK_LABEL (dialog->priv->date), time);
	g_free (time);
}

static void
rb_podcast_properties_dialog_update_description (RBPodcastPropertiesDialog *dialog)
{
	int i;
	const char *desc;

	desc = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_DESCRIPTION);
	for (i = 0; i < G_N_ELEMENTS (html_clues); i++) {
		if (g_strstr_len (desc, -1, html_clues[i]) != NULL) {
			char *text;

			text = unhtml (desc);
			gtk_label_set_text (GTK_LABEL (dialog->priv->description), text);
			g_free (text);
			return;
		}
	}

	gtk_label_set_text (GTK_LABEL (dialog->priv->description), desc);
}

static char *
rb_podcast_properties_dialog_parse_time (gulong value)
{
	char *str;

	if (0 == value) {
		str = g_strdup (_("Unknown"));
	} else {
		str = rb_utf_friendly_time ((time_t)value);
	}

	return str;
}
