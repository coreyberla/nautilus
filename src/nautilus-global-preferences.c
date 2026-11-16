/*
 * SPDX-FileCopyrightText: 1999, 2000, 2001 Eazel, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Authors: Ramiro Estrugo <ramiro@eazel.com>
 */

#include <config.h>
#include "nautilus-global-preferences.h"

#define G_SETTINGS_ENABLE_BACKEND
#include <gio/gsettingsbackend.h>
#include <libportal/portal.h>
#include <libportal/settings.h>

#include "nautilus-application.h"
#include "nautilus-file-utilities.h"

GSettings *nautilus_preferences;
GSettings *nautilus_compression_preferences;
GSettings *nautilus_icon_view_preferences;
GSettings *nautilus_list_view_preferences;
GSettings *nautilus_window_state;
GSettings *gtk_filechooser_preferences;
GSettings *gnome_lockdown_preferences;
GSettings *gnome_interface_preferences;
GSettings *gnome_privacy_preferences;
GSettings *gnome_user_share_preferences = NULL;
GSettings *localsearch_preferences;

#define FILE_SHARING_SCHEMA_ID "org.gnome.desktop.file-sharing"
#define FILE_SHARING_SERVICE_PATH "/org/gnome/settings-daemon/plugins/sharing/gnome-user-share-webdav/"
#define FILE_SHARING_SERVICE_SCHEMA_ID "org.gnome.settings-daemon.plugins.sharing.service"
#define UPPER_MOUSE_LIMIT 14
/* Forward and back buttons on the mouse */
static gboolean mouse_extra_buttons = TRUE;
static guint mouse_forward_button = 9;
static guint mouse_back_button = 8;

static void
use_extra_mouse_buttons_changed (gpointer callback_data)
{
    mouse_extra_buttons = g_settings_get_boolean (nautilus_preferences, NAUTILUS_PREFERENCES_MOUSE_USE_EXTRA_BUTTONS);
}

static void
mouse_back_button_changed (gpointer callback_data)
{
    int new_back_button;

    new_back_button = g_settings_get_uint (nautilus_preferences, NAUTILUS_PREFERENCES_MOUSE_BACK_BUTTON);

    /* Bounds checking */
    if (new_back_button < 6 || new_back_button > UPPER_MOUSE_LIMIT)
    {
        return;
    }

    mouse_back_button = new_back_button;
}

static void
mouse_forward_button_changed (gpointer callback_data)
{
    int new_forward_button;

    new_forward_button = g_settings_get_uint (nautilus_preferences, NAUTILUS_PREFERENCES_MOUSE_FORWARD_BUTTON);

    /* Bounds checking */
    if (new_forward_button < 6 || new_forward_button > UPPER_MOUSE_LIMIT)
    {
        return;
    }

    mouse_forward_button = new_forward_button;
}

static void
on_portal_setting_changed (XdpSettings *portal_settings,
                           const char  *namespace,
                           const char  *key,
                           GVariant    *value,
                           gpointer     user_data)
{
    GSettings *settings = user_data;
    g_autoptr (GSettingsSchema) schema = NULL;
    g_autoptr (GSettingsSchemaKey) schema_key = NULL;

    g_object_get (settings, "settings-schema", &schema, NULL);
    if (!g_str_equal (namespace, g_settings_schema_get_id (schema)) ||
        !g_settings_schema_has_key (schema, key))
    {
        return;
    }

    schema_key = g_settings_schema_get_key (schema, key);
    if (g_variant_is_of_type (value, g_settings_schema_key_get_value_type (schema_key)) &&
        g_settings_schema_key_range_check (schema_key, value))
    {
        g_settings_set_value (settings, key, value);
    }
}

/* In a sandbox, the desktop's settings are not readable directly, so keep
 * an in-memory copy of them, filled and kept up to date by the Settings
 * portal. Nautilus only reads these settings. */
static GSettings *
new_desktop_settings (const char *schema_id)
{
    static XdpSettings *portal_settings = NULL;
    g_autoptr (GSettingsBackend) backend = NULL;
    GSettings *settings;
    const char *namespaces[] = { schema_id, NULL };
    g_autoptr (GVariant) all_values = NULL;
    g_autoptr (GVariant) values = NULL;
    GVariantIter iter;
    const char *key;
    GVariant *value;

    if (!nautilus_application_is_sandboxed ())
    {
        return g_settings_new (schema_id);
    }

    if (portal_settings == NULL)
    {
        g_autoptr (XdpPortal) portal = xdp_portal_new ();

        portal_settings = xdp_portal_get_settings (portal);
    }

    backend = g_memory_settings_backend_new ();
    settings = g_settings_new_with_backend (schema_id, backend);
    g_signal_connect_object (portal_settings, "changed",
                             G_CALLBACK (on_portal_setting_changed), settings, G_CONNECT_DEFAULT);

    all_values = xdp_settings_read_all_values (portal_settings, namespaces, NULL, NULL);
    values = all_values != NULL ? g_variant_lookup_value (all_values, schema_id,
                                                          G_VARIANT_TYPE_VARDICT) : NULL;
    if (values != NULL)
    {
        g_variant_iter_init (&iter, values);
        while (g_variant_iter_next (&iter, "{&sv}", &key, &value))
        {
            on_portal_setting_changed (portal_settings, schema_id, key, value, settings);
            g_variant_unref (value);
        }
    }

    return settings;
}

void
nautilus_global_preferences_init (void)
{
    static gboolean initialized = FALSE;

    if (initialized)
    {
        return;
    }

    initialized = TRUE;

    nautilus_preferences = g_settings_new ("org.gnome.nautilus.preferences");
    nautilus_compression_preferences = g_settings_new ("org.gnome.nautilus.compression");
    nautilus_window_state = g_settings_new ("org.gnome.nautilus.window-state");
    nautilus_icon_view_preferences = g_settings_new ("org.gnome.nautilus.icon-view");
    nautilus_list_view_preferences = g_settings_new ("org.gnome.nautilus.list-view");
    /* Some settings such as show hidden files are shared between Nautilus and GTK file chooser */
    gtk_filechooser_preferences = g_settings_new_with_path ("org.gtk.gtk4.Settings.FileChooser",
                                                            "/org/gtk/gtk4/settings/file-chooser/");
    gnome_lockdown_preferences = g_settings_new ("org.gnome.desktop.lockdown");
    gnome_interface_preferences = new_desktop_settings ("org.gnome.desktop.interface");
    gnome_privacy_preferences = new_desktop_settings ("org.gnome.desktop.privacy");
    localsearch_preferences = g_settings_new ("org.freedesktop.Tracker3.Miner.Files");

    if (check_schema_available (FILE_SHARING_SCHEMA_ID) &&
        check_schema_available (FILE_SHARING_SERVICE_SCHEMA_ID))
    {
        gnome_user_share_preferences = g_settings_new_with_path (FILE_SHARING_SERVICE_SCHEMA_ID,
                                                                 FILE_SHARING_SERVICE_PATH);
    }

    g_signal_connect_swapped (nautilus_preferences,
                              "changed::" NAUTILUS_PREFERENCES_MOUSE_BACK_BUTTON,
                              G_CALLBACK (mouse_back_button_changed),
                              NULL);

    g_signal_connect_swapped (nautilus_preferences,
                              "changed::" NAUTILUS_PREFERENCES_MOUSE_FORWARD_BUTTON,
                              G_CALLBACK (mouse_forward_button_changed),
                              NULL);

    g_signal_connect_swapped (nautilus_preferences,
                              "changed::" NAUTILUS_PREFERENCES_MOUSE_USE_EXTRA_BUTTONS,
                              G_CALLBACK (use_extra_mouse_buttons_changed),
                              NULL);
}

guint
nautilus_global_preferences_get_back_button (void)
{
    return mouse_back_button;
}

guint
nautilus_global_preferences_get_forward_button (void)
{
    return mouse_forward_button;
}

gboolean
nautilus_global_preferences_get_use_extra_buttons (void)
{
    return mouse_extra_buttons;
}
