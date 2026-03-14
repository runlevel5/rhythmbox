
#include <adwaita.h>
#include "rb-uri-dialog.h"
#include "rb-file-helpers.h"

static void
location_added (RBURIDialog *dialog,
		const char  *uri,
		gpointer     user_data)
{
	g_message ("URI selected was: %s", uri);
}

static void
activate (GtkApplication *app, gpointer user_data)
{
	GtkWidget *window;
	AdwDialog *dialog;

	window = gtk_application_window_new (app);
	gtk_window_set_default_size (GTK_WINDOW (window), 400, 300);
	gtk_window_present (GTK_WINDOW (window));

	dialog = rb_uri_dialog_new ("Dialog title", "dialog label");
	g_signal_connect (G_OBJECT (dialog), "location-added",
			  G_CALLBACK (location_added), NULL);

	adw_dialog_present (dialog, window);
}

int main (int argc, char **argv)
{
	GtkApplication *app;
	int status;

	rb_file_helpers_init ();

	app = gtk_application_new ("org.gnome.Rhythmbox.TestURIDialog", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
	status = g_application_run (G_APPLICATION (app), argc, argv);
	g_object_unref (app);

	return status;
}
