/*
 * nautilus-module.h - Interface to nautilus extensions
 *
 * SPDX-FileCopyrightText: 2003 Novell, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Author: Dave Camp <dave@ximian.com>
 */

#include <config.h>
#include "nautilus-module.h"

#include <gmodule.h>

#define NAUTILUS_TYPE_MODULE            (nautilus_module_get_type ())
#define NAUTILUS_MODULE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NAUTILUS_TYPE_MODULE, NautilusModule))
#define NAUTILUS_MODULE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NAUTILUS_TYPE_MODULE, NautilusModule))
#define NAUTILUS_IS_MODULE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NAUTILUS_TYPE_MODULE))
#define NAUTILUS_IS_MODULE_CLASS(klass) (G_TYPE_CLASS_CHECK_CLASS_TYPE ((klass), NAUTILUS_TYPE_MODULE))

typedef struct _NautilusModule NautilusModule;
typedef struct _NautilusModuleClass NautilusModuleClass;

struct _NautilusModule
{
    GTypeModule parent;

    GModule *library;

    char *path;

    void (*initialize) (GTypeModule *module);
    void (*shutdown)   (void);

    void (*list_types) (const GType **types,
                        int          *num_types);
};

struct _NautilusModuleClass
{
    GTypeModuleClass parent;
};

static GList *module_objects = NULL;
static GList *installed_modules = NULL;

static GType nautilus_module_get_type (void);

G_DEFINE_TYPE (NautilusModule, nautilus_module, G_TYPE_TYPE_MODULE);

static gboolean
nautilus_module_load (GTypeModule *gmodule)
{
    NautilusModule *module;

    module = NAUTILUS_MODULE (gmodule);

    module->library = g_module_open (module->path, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);

    if (!module->library)
    {
        g_warning ("%s", g_module_error ());
        return FALSE;
    }

    if (!g_module_symbol (module->library,
                          "nautilus_module_initialize",
                          (gpointer *) &module->initialize) ||
        !g_module_symbol (module->library,
                          "nautilus_module_shutdown",
                          (gpointer *) &module->shutdown) ||
        !g_module_symbol (module->library,
                          "nautilus_module_list_types",
                          (gpointer *) &module->list_types))
    {
        g_warning ("%s", g_module_error ());
        g_module_close (module->library);

        return FALSE;
    }

    g_module_make_resident (module->library);
    module->initialize (gmodule);

    return TRUE;
}

static void
nautilus_module_finalize (GObject *object)
{
    NautilusModule *module;

    module = NAUTILUS_MODULE (object);

    g_free (module->path);

    G_OBJECT_CLASS (nautilus_module_parent_class)->finalize (object);
}

static void
nautilus_module_init (NautilusModule *module)
{
}

static void
nautilus_module_class_init (NautilusModuleClass *class)
{
    G_OBJECT_CLASS (class)->finalize = nautilus_module_finalize;
    G_TYPE_MODULE_CLASS (class)->load = nautilus_module_load;
}

static void
module_object_weak_notify (gpointer  user_data,
                           GObject  *object)
{
    module_objects = g_list_remove (module_objects, object);
}

static void
add_module_objects (NautilusModule *module)
{
    const GType *types;
    int num_types;
    int i;

    module->list_types (&types, &num_types);

    for (i = 0; i < num_types; i++)
    {
        if (types[i] == 0)           /* Work around broken extensions */
        {
            break;
        }
        nautilus_module_add_type (types[i]);
    }
}

static void
nautilus_module_load_file (const char *filename)
{
    NautilusModule *module;

    module = g_object_new (NAUTILUS_TYPE_MODULE, NULL);
    module->path = g_strdup (filename);

    if (g_type_module_use (G_TYPE_MODULE (module)))
    {
        add_module_objects (module);
        installed_modules = g_list_prepend (installed_modules, module);
        return;
    }
    else
    {
        g_object_unref (module);
    }
}

char *
nautilus_module_get_installed_module_names (void)
{
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
    g_auto (GStrv) names = NULL;

    for (GList *l = installed_modules; l != NULL; l = l->next)
    {
        NautilusModule *module = l->data;
        g_strv_builder_add (builder, module->path);
    }

    names = g_strv_builder_end (builder);

    return g_strjoinv ("\n", names);
}

static void
load_module_dir (const char *dirname,
                 GHashTable *loaded_names)
{
    g_autoptr (GDir) dir = g_dir_open (dirname, 0, NULL);
    const char *name;

    if (dir == NULL)
    {
        return;
    }

    while ((name = g_dir_read_name (dir)) != NULL)
    {
        g_autofree char *filename = NULL;

        if (!g_str_has_suffix (name, "." G_MODULE_SUFFIX))
        {
            continue;
        }

        /* Loading two copies of the same extension would register its types
         * twice, so the first directory providing a given file name wins. */
        if (g_hash_table_contains (loaded_names, name))
        {
            g_debug ("Skipping extension %s/%s: already loaded from another directory",
                     dirname, name);
            continue;
        }

        g_hash_table_add (loaded_names, g_strdup (name));

        filename = g_build_filename (dirname, name, NULL);
        nautilus_module_load_file (filename);
    }
}

void
nautilus_module_teardown (void)
{
    GList *l, *next;

    for (l = module_objects; l != NULL; l = next)
    {
        next = l->next;
        g_object_unref (l->data);
    }

    g_list_free (module_objects);

    for (l = installed_modules; l != NULL; l = l->next)
    {
        NautilusModule *module = l->data;
        module->shutdown ();
    }

    /* We can't actually free the modules themselves. */
    g_clear_pointer (&installed_modules, g_list_free);
}

void
nautilus_module_setup (void)
{
    static gboolean initialized = FALSE;
    const gchar *disable_plugins;

    disable_plugins = g_getenv ("NAUTILUS_DISABLE_PLUGINS");
    if (g_strcmp0 (disable_plugins, "TRUE") == 0)
    {
        /* Troublingshooting envvar is set to disable extensions */
        return;
    }

    if (!initialized)
    {
        initialized = TRUE;

        g_autoptr (GHashTable) loaded_names = g_hash_table_new_full (g_str_hash,
                                                                     g_str_equal,
                                                                     g_free,
                                                                     NULL);
        const char *extension_path = g_getenv ("NAUTILUS_EXTENSION_PATH");

        /* Additional directories, separated like PATH, are searched before the
         * built-in one. This lets sandboxed builds (e.g. Flatpak extension
         * points) provide extensions outside the installation prefix. */
        if (extension_path != NULL)
        {
            g_auto (GStrv) dirs = g_strsplit (extension_path, G_SEARCHPATH_SEPARATOR_S, -1);

            for (guint i = 0; dirs[i] != NULL; i++)
            {
                if (dirs[i][0] != '\0')
                {
                    load_module_dir (dirs[i], loaded_names);
                }
            }
        }

        load_module_dir (NAUTILUS_EXTENSIONDIR, loaded_names);
    }
}

GList *
nautilus_module_get_extensions_for_type (GType type)
{
    GList *l;
    GList *ret = NULL;

    for (l = module_objects; l != NULL; l = l->next)
    {
        if (G_TYPE_CHECK_INSTANCE_TYPE (G_OBJECT (l->data),
                                        type))
        {
            g_object_ref (l->data);
            ret = g_list_prepend (ret, l->data);
        }
    }

    return ret;
}

void
nautilus_module_extension_list_free (GList *extensions)
{
    GList *l, *next;

    for (l = extensions; l != NULL; l = next)
    {
        next = l->next;
        g_object_unref (l->data);
    }
    g_list_free (extensions);
}

void
nautilus_module_add_type (GType type)
{
    GObject *object;

    object = g_object_new (type, NULL);
    g_object_weak_ref (object,
                       (GWeakNotify) module_object_weak_notify,
                       NULL);

    module_objects = g_list_prepend (module_objects, object);
}
