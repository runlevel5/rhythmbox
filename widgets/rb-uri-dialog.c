/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho - INdT <renato.filho@indt.org.br>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <libsoup/soup.h>

#include "rb-uri-dialog.h"
#include "rb-debug.h"

/**
 * SECTION:rburidialog
 * @short_description: simple URI entry dialog using AdwAlertDialog
 * @include: rb-uri-dialog.h
 *
 * A simple dialog used to request a single URI from the user,
 * presented as an AdwAlertDialog with proper GNOME styling.
 */

static void rb_uri_dialog_class_init (RBURIDialogClass *klass);
static void rb_uri_dialog_init (RBURIDialog *dialog);
static void rb_uri_dialog_response_cb (AdwAlertDialog *alert_dialog,
				       const char *response,
				       RBURIDialog *dialog);
static void rb_uri_dialog_text_changed (GtkEditable *buffer,
					RBURIDialog *dialog);
static void rb_uri_dialog_clipboard_yank_url (GObject *source_object,
					      GAsyncResult *result,
					      gpointer data);

struct RBURIDialogPrivate
{
	GtkWidget   *url;
};

#define RB_URI_DIALOG_GET_PRIVATE(o) (rb_uri_dialog_get_instance_private (o))

enum
{
	LOCATION_ADDED,
	LAST_SIGNAL
};

static guint rb_uri_dialog_signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (RBURIDialog, rb_uri_dialog, ADW_TYPE_ALERT_DIALOG)

static void
rb_uri_dialog_class_init (RBURIDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/**
	 * RBURIDialog::location-added:
	 * @dialog: the #RBURIDialog
	 * @uri: URI entered
	 *
	 * Emitted when the user has entered a URI into the dialog.
	 */
	rb_uri_dialog_signals [LOCATION_ADDED] =
		g_signal_new ("location-added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBURIDialogClass, location_added),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
}

static void
rb_uri_dialog_init (RBURIDialog *dialog)
{
	dialog->priv = RB_URI_DIALOG_GET_PRIVATE (dialog);

	/* set up responses */
	adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "cancel", _("_Cancel"));
	adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "add", _("_Add"));
	adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
						  "add", ADW_RESPONSE_SUGGESTED);
	adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "add");
	adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

	/* start with Add disabled until text is entered */
	adw_alert_dialog_set_response_enabled (ADW_ALERT_DIALOG (dialog), "add", FALSE);

	/* create entry as extra child */
	dialog->priv->url = gtk_entry_new ();
	gtk_entry_set_activates_default (GTK_ENTRY (dialog->priv->url), TRUE);
	adw_alert_dialog_set_extra_child (ADW_ALERT_DIALOG (dialog), dialog->priv->url);

	g_signal_connect_object (G_OBJECT (dialog->priv->url),
				 "changed",
				 G_CALLBACK (rb_uri_dialog_text_changed),
				 dialog, 0);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (rb_uri_dialog_response_cb), dialog);

	/* try to auto-fill from clipboard */
	gdk_clipboard_read_text_async (gdk_display_get_clipboard (gdk_display_get_default ()),
				       NULL,
				       rb_uri_dialog_clipboard_yank_url,
				       dialog);
}

/**
 * rb_uri_dialog_new:
 * @title: Heading for the dialog
 * @label: Body text displayed in the dialog
 *
 * Creates a URI entry dialog using AdwAlertDialog.
 * Present with adw_dialog_present(ADW_DIALOG(dialog), parent).
 *
 * Returns: (transfer full): URI dialog instance.
 */
AdwDialog *
rb_uri_dialog_new (const char *title, const char *label)
{
	RBURIDialog *dialog;

	dialog = g_object_new (RB_TYPE_URI_DIALOG,
			       "heading", title,
			       "body", label,
			       NULL);
	return ADW_DIALOG (dialog);
}

static void
rb_uri_dialog_response_cb (AdwAlertDialog *alert_dialog,
			   const char *response,
			   RBURIDialog *dialog)
{
	char *valid_url;
	char *str;

	if (g_strcmp0 (response, "add") != 0)
		return;

	str = gtk_editable_get_chars (GTK_EDITABLE (dialog->priv->url), 0, -1);
	valid_url = g_strstrip (str);

	g_signal_emit (dialog, rb_uri_dialog_signals [LOCATION_ADDED], 0, valid_url);

	g_free (str);
}

static void
rb_uri_dialog_text_changed (GtkEditable *buffer,
			    RBURIDialog *dialog)
{
	char *text = gtk_editable_get_chars (buffer, 0, -1);
	gboolean has_text = ((text != NULL) && (*text != 0));

	g_free (text);

	adw_alert_dialog_set_response_enabled (ADW_ALERT_DIALOG (dialog), "add", has_text);
}

static void
rb_uri_dialog_clipboard_yank_url (GObject *source_object, GAsyncResult *result, gpointer data)
{
	RBURIDialog *dialog = RB_URI_DIALOG (data);
	GdkClipboard *clipboard = GDK_CLIPBOARD (source_object);
	char *text;
	GUri *uri;
	const char *scheme;

	text = gdk_clipboard_read_text_finish (clipboard, result, NULL);
	if (text == NULL) {
		return;
	}

	uri = g_uri_parse (text, SOUP_HTTP_URI_FLAGS, NULL);
	if (uri == NULL) {
		rb_debug ("did not autofill from clipboard: not a valid URL");
		g_free (text);
		return;
	}

	scheme = g_uri_get_scheme (uri);
	if ((g_strcmp0 (scheme, "http") == 0) || (g_strcmp0 (scheme, "https") == 0)) {
		gtk_editable_set_text (GTK_EDITABLE (dialog->priv->url), text);
		gtk_editable_select_region (GTK_EDITABLE (dialog->priv->url), 0, -1);
	}

	g_uri_unref (uri);
	g_free (text);
}
