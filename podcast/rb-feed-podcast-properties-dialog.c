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

#include <config.h>

#include <string.h>
#include <time.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <adwaita.h>
/* GStreamer happens to have some language name functions */
#include <gst/gst.h>
#include <gst/tag/tag.h>

#include "rb-feed-podcast-properties-dialog.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-cut-and-paste-code.h"
#include "rhythmdb.h"
#include "rb-debug.h"

static void rb_feed_podcast_properties_dialog_class_init (RBFeedPodcastPropertiesDialogClass *klass);
static void rb_feed_podcast_properties_dialog_init (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_finalize (GObject *object);
static void rb_feed_podcast_properties_dialog_setup (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_title (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_title_label (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_location (RBFeedPodcastPropertiesDialog *dialog);

static void rb_feed_podcast_properties_dialog_update (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_author (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_language (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_last_update (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_last_episode (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_copyright (RBFeedPodcastPropertiesDialog *dialog);
static void rb_feed_podcast_properties_dialog_update_summary (RBFeedPodcastPropertiesDialog *dialog);
static gchar* rb_feed_podcast_properties_dialog_parse_time (gulong time);

struct RBFeedPodcastPropertiesDialogPrivate
{
	RhythmDB *db;
	RhythmDBEntry *current_entry;

	GtkWidget   *title;
	GtkWidget   *author;
	GtkWidget   *location;
	GtkWidget   *language;
	GtkWidget   *last_update;
	GtkWidget   *last_episode;
	GtkWidget   *copyright;
	GtkWidget   *summary;
};

#define RB_FEED_PODCAST_PROPERTIES_DIALOG_GET_PRIVATE(o) (rb_feed_podcast_properties_dialog_get_instance_private (o))

enum
{
	PROP_0,
	PROP_BACKEND
};

G_DEFINE_TYPE_WITH_PRIVATE (RBFeedPodcastPropertiesDialog, rb_feed_podcast_properties_dialog, ADW_TYPE_DIALOG)

static void
rb_feed_podcast_properties_dialog_class_init (RBFeedPodcastPropertiesDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_feed_podcast_properties_dialog_finalize;

}

static void
rb_feed_podcast_properties_dialog_init (RBFeedPodcastPropertiesDialog *dialog)
{
	dialog->priv = RB_FEED_PODCAST_PROPERTIES_DIALOG_GET_PRIVATE (dialog);
}

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
				break;
			}
			break;
		}
		p++;
	}
	*o++ = '\0';
	return out;
}

/* Helper: add a bold label + value label row to a grid */
static void
add_label_row (GtkGrid *grid, int row, const char *desc_text,
	       GtkWidget **value_widget, gboolean selectable, gboolean ellipsize)
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
	gtk_label_set_selectable (GTK_LABEL (*value_widget), selectable);
	if (ellipsize)
		gtk_label_set_ellipsize (GTK_LABEL (*value_widget), PANGO_ELLIPSIZE_END);

	gtk_grid_attach (grid, *value_widget, 1, row, 1, 1);
}

static void
rb_feed_podcast_properties_dialog_setup (RBFeedPodcastPropertiesDialog *dialog)
{
	GtkWidget *toolbar_view;
	GtkWidget *header_bar;
	GtkWidget *stack;
	GtkWidget *switcher;
	GtkWidget *grid;
	GtkWidget *desc_label;
	GtkWidget *scroll;
	GtkWidget *viewport;
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
	adw_dialog_set_content_height (ADW_DIALOG (dialog), 400);

	/* ---- Basic page ---- */
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_widget_set_margin_start (grid, 12);
	gtk_widget_set_margin_end (grid, 12);
	gtk_widget_set_margin_top (grid, 12);
	gtk_widget_set_margin_bottom (grid, 12);
	row = 0;

	add_label_row (GTK_GRID (grid), row++, _("Title:"),
		       &dialog->priv->title, TRUE, TRUE);
	add_label_row (GTK_GRID (grid), row++, _("Author:"),
		       &dialog->priv->author, TRUE, TRUE);
	add_label_row (GTK_GRID (grid), row++, _("Last updated:"),
		       &dialog->priv->last_update, TRUE, FALSE);
	add_label_row (GTK_GRID (grid), row++, _("Last episode:"),
		       &dialog->priv->last_episode, FALSE, FALSE);

	/* Summary / Description with scroll */
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

	dialog->priv->summary = gtk_label_new ("-");
	gtk_label_set_wrap (GTK_LABEL (dialog->priv->summary), TRUE);
	gtk_label_set_selectable (GTK_LABEL (dialog->priv->summary), TRUE);
	gtk_label_set_xalign (GTK_LABEL (dialog->priv->summary), 0.0);
	gtk_label_set_yalign (GTK_LABEL (dialog->priv->summary), 0.0);

	viewport = gtk_viewport_new (NULL, NULL);
	gtk_viewport_set_child (GTK_VIEWPORT (viewport), dialog->priv->summary);

	scroll = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
					GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), viewport);
	gtk_widget_set_vexpand (scroll, TRUE);
	gtk_widget_set_hexpand (scroll, TRUE);
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

	add_label_row (GTK_GRID (grid), row++, _("Source:"),
		       &dialog->priv->location, TRUE, FALSE);
	gtk_label_set_ellipsize (GTK_LABEL (dialog->priv->location), PANGO_ELLIPSIZE_MIDDLE);
	add_label_row (GTK_GRID (grid), row++, _("Language:"),
		       &dialog->priv->language, TRUE, FALSE);
	add_label_row (GTK_GRID (grid), row++, _("Copyright:"),
		       &dialog->priv->copyright, TRUE, TRUE);

	gtk_stack_add_titled (GTK_STACK (stack), grid, "details", _("Details"));
}

static void
rb_feed_podcast_properties_dialog_finalize (GObject *object)
{
	RBFeedPodcastPropertiesDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_FEED_PODCAST_PROPERTIES_DIALOG (object));

	dialog = RB_FEED_PODCAST_PROPERTIES_DIALOG (object);

	g_return_if_fail (dialog->priv != NULL);

	G_OBJECT_CLASS (rb_feed_podcast_properties_dialog_parent_class)->finalize (object);
}

GtkWidget *
rb_feed_podcast_properties_dialog_new (RhythmDBEntry *entry)
{
	RBFeedPodcastPropertiesDialog *dialog;

	dialog = g_object_new (RB_TYPE_FEED_PODCAST_PROPERTIES_DIALOG, NULL);

	rb_feed_podcast_properties_dialog_setup (dialog);

	dialog->priv->current_entry = entry;

	rb_feed_podcast_properties_dialog_update (dialog);

	return GTK_WIDGET (dialog);
}

static void
rb_feed_podcast_properties_dialog_update (RBFeedPodcastPropertiesDialog *dialog)
{
	g_return_if_fail (dialog->priv->current_entry != NULL);

	rb_feed_podcast_properties_dialog_update_location (dialog);
	rb_feed_podcast_properties_dialog_update_title (dialog);
	rb_feed_podcast_properties_dialog_update_title_label (dialog);
	rb_feed_podcast_properties_dialog_update_author (dialog);
	rb_feed_podcast_properties_dialog_update_language (dialog);
	rb_feed_podcast_properties_dialog_update_last_update (dialog);
	rb_feed_podcast_properties_dialog_update_last_episode (dialog);
	rb_feed_podcast_properties_dialog_update_copyright (dialog);
	rb_feed_podcast_properties_dialog_update_summary (dialog);
}

static void
rb_feed_podcast_properties_dialog_update_title (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *name;
	char *tmp;
	name = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
	tmp = g_strdup_printf (_("%s Properties"), name);
	adw_dialog_set_title (ADW_DIALOG (dialog), tmp);
	g_free (tmp);
}

static void
rb_feed_podcast_properties_dialog_update_title_label (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *title;

	title = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_TITLE);
	gtk_label_set_text (GTK_LABEL (dialog->priv->title), title);
}

static void
rb_feed_podcast_properties_dialog_update_author (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *artist;

	artist = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_ARTIST);
	gtk_label_set_text (GTK_LABEL (dialog->priv->author), artist);
}

static void
rb_feed_podcast_properties_dialog_update_location (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *location;
	char *unescaped;

	location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (location == NULL)
		location = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LOCATION);
	unescaped = g_uri_unescape_string (location, NULL);
	gtk_label_set_text (GTK_LABEL (dialog->priv->location), unescaped);
	g_free (unescaped);
}

static void
rb_feed_podcast_properties_dialog_update_copyright (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *copyright;

	copyright = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_COPYRIGHT);
	gtk_label_set_text (GTK_LABEL (dialog->priv->copyright), copyright);
}

static void
rb_feed_podcast_properties_dialog_update_language (RBFeedPodcastPropertiesDialog *dialog)
{
	const char *language;
	char *separator;
	char *iso636lang;
	const char *langname;

	language = rhythmdb_entry_get_string (dialog->priv->current_entry, RHYTHMDB_PROP_LANG);

	/* language tag is language[-subcode]; we only care about the language bit */
	iso636lang = g_strdup (language);
	separator = strchr (iso636lang, '-');
	if (separator != NULL) {
		*separator = '\0';
	}

	/* map the language code to a language name */
	langname = gst_tag_get_language_name (iso636lang);
	g_free (iso636lang);
	if (langname != NULL) {
		rb_debug ("mapped language code %s to %s", language, langname);
		gtk_label_set_text (GTK_LABEL (dialog->priv->language), langname);
		return;
	}

	gtk_label_set_text (GTK_LABEL (dialog->priv->language), language);
}

static void
rb_feed_podcast_properties_dialog_update_last_update (RBFeedPodcastPropertiesDialog *dialog)
{
	char *time_str;
	gulong time_val;

	time_val = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_LAST_SEEN);
	time_str = rb_feed_podcast_properties_dialog_parse_time (time_val);
	gtk_label_set_text (GTK_LABEL (dialog->priv->last_update), time_str);
	g_free (time_str);
}

static void
rb_feed_podcast_properties_dialog_update_last_episode (RBFeedPodcastPropertiesDialog *dialog)
{
	char *time_str;
	gulong time_val;

	time_val = rhythmdb_entry_get_ulong (dialog->priv->current_entry, RHYTHMDB_PROP_POST_TIME);
	time_str = rb_feed_podcast_properties_dialog_parse_time (time_val);
	gtk_label_set_text (GTK_LABEL (dialog->priv->last_episode), time_str);
	g_free (time_str);
}

static void
rb_feed_podcast_properties_dialog_update_summary (RBFeedPodcastPropertiesDialog *dialog)
{
	int i;
	const char *summary;

	summary = rhythmdb_entry_get_string (dialog->priv->current_entry,
					     RHYTHMDB_PROP_DESCRIPTION);
	if (summary == NULL || summary[0] == '\0') {
		summary = rhythmdb_entry_get_string (dialog->priv->current_entry,
						     RHYTHMDB_PROP_SUBTITLE);
	}

	for (i = 0; i < G_N_ELEMENTS (html_clues); i++) {
		if (g_strstr_len (summary, -1, html_clues[i]) != NULL) {
			char *text;

			text = unhtml (summary);
			gtk_label_set_text (GTK_LABEL (dialog->priv->summary), text);
			g_free (text);
			return;
		}
	}

	gtk_label_set_text (GTK_LABEL (dialog->priv->summary), summary);
}

static char *
rb_feed_podcast_properties_dialog_parse_time (gulong value)
{
	char *str;

	if (0 == value) {
		str = g_strdup (_("Unknown"));
	} else {
		str = rb_utf_friendly_time ((time_t)value);
	}

	return str;
}
