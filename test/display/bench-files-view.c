/*
 * Folder loading benchmark for NautilusFilesView.
 *
 * BENCH_FILES   number of regular files (default 20000)
 * BENCH_DIRS    number of subdirectories, each with one file (default 2000)
 * BENCH_CHANGES number of files touched after load (default 2000)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define G_LOG_DOMAIN "bench-files-view"

#include <glib/gstdio.h>
#include <sys/resource.h>
#include <unistd.h>
#include <test-utilities.h>

#include <nautilus-application.h>
#include <nautilus-directory.h>
#include <nautilus-file.h>
#include <nautilus-files-view.h>
#include <nautilus-global-preferences.h>
#include <nautilus-file-utilities.h>
#include <nautilus-resources.h>
#include <nautilus-tag-manager.h>
#include <nautilus-view-info.h>
#include <nautilus-view-model.h>
#include <nautilus-window-slot.h>

typedef struct
{
    gint64 start;
    gint64 total;
    guint count;
} Timer;

static void
end_changes_before (Timer *timer)
{
    timer->start = g_get_monotonic_time ();
}

static void
end_changes_after (Timer *timer)
{
    timer->total += g_get_monotonic_time () - timer->start;
    timer->count++;
}

static void
count_changed (NautilusFilesView *view,
               NautilusFile      *file,
               NautilusDirectory *directory,
               guint             *count)
{
    (*count)++;
}

static guint
env_uint (const char *name,
          guint       fallback)
{
    const char *value = g_getenv (name);

    return value != NULL ? (guint) g_ascii_strtoull (value, NULL, 10) : fallback;
}

static double
cpu_ms (void)
{
    struct rusage usage;

    getrusage (RUSAGE_THREAD, &usage);
    return usage.ru_utime.tv_sec * 1000.0 + usage.ru_utime.tv_usec / 1000.0 +
           usage.ru_stime.tv_sec * 1000.0 + usage.ru_stime.tv_usec / 1000.0;
}

static double
ms_since (gint64 start)
{
    return (g_get_monotonic_time () - start) / 1000.0;
}

#define ITERATE_WHILE(CONDITION) \
    while (CONDITION) { g_main_context_iteration (NULL, TRUE); }

/* Run the main loop until nothing has happened for quiet_ms. */
static void
iterate_until_quiet (guint quiet_ms,
                     guint max_ms)
{
    gint64 start = g_get_monotonic_time ();
    gint64 last_event = start;

    while (ms_since (start) < max_ms)
    {
        if (g_main_context_iteration (NULL, FALSE))
        {
            last_event = g_get_monotonic_time ();
        }
        else if (ms_since (last_event) >= quiet_ms)
        {
            break;
        }
        else
        {
            g_usleep (1000);
        }
    }
}

int
main (int   argc,
      char *argv[])
{
    g_autoptr (NautilusTagManager) tag_manager = NULL;
    guint n_files = env_uint ("BENCH_FILES", 20000);
    guint n_dirs = env_uint ("BENCH_DIRS", 2000);
    guint n_changes = env_uint ("BENCH_CHANGES", 2000);
    Timer end_changes = { 0 };
    guint changed = 0;
    gint64 start;
    double cpu_start;

    gtk_init ();
    g_log_set_debug_enabled (g_getenv ("BENCH_DEBUG") != NULL);
    nautilus_register_resource ();
    nautilus_ensure_extension_points ();
    nautilus_global_preferences_init ();
    tag_manager = nautilus_tag_manager_new_dummy ();
    test_init_config_dir ();

    g_autoptr (NautilusApplication) app = nautilus_application_new ();
    const char *tmp_dir = test_get_tmp_dir ();
    g_autoptr (GFile) tmp_location = g_file_new_for_path (tmp_dir);

    /* Populate the folder. */
    for (guint i = 0; i < n_files; i++)
    {
        g_autofree char *path = g_strdup_printf ("%s/file_%06u.txt", tmp_dir, i);
        g_file_set_contents (path, "x", 1, NULL);
    }
    for (guint i = 0; i < n_dirs; i++)
    {
        g_autofree char *dir = g_strdup_printf ("%s/dir_%06u", tmp_dir, i);
        g_autofree char *inner = g_build_filename (dir, "inner", NULL);

        g_mkdir (dir, 0755);
        g_file_set_contents (inner, "x", 1, NULL);
    }

    NautilusWindowSlot *slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    g_autoptr (GFile) home = g_file_new_for_path (g_get_home_dir ());
    nautilus_window_slot_open_location_full (slot, home, NULL);
    ITERATE_WHILE (nautilus_window_slot_get_current_view (slot) == NULL);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (slot);
    ITERATE_WHILE (nautilus_files_view_get_loading (view));
    NautilusViewModel *model = nautilus_files_view_get_private_model (view);

    g_signal_connect_swapped (view, "end-file-changes", G_CALLBACK (end_changes_before), &end_changes);
    g_signal_connect_data (view, "end-file-changes", G_CALLBACK (end_changes_after), &end_changes,
                           NULL, G_CONNECT_SWAPPED | G_CONNECT_AFTER);
    g_signal_connect (view, "file-changed", G_CALLBACK (count_changed), &changed);

    /* 1. Cold load */
    start = g_get_monotonic_time ();
    nautilus_files_view_set_location (view, tmp_location);
    ITERATE_WHILE (nautilus_files_view_get_loading (view));
    g_print ("load:            %8.1f ms  (%u items, end-file-changes %.1f ms in %u calls)\n",
             ms_since (start), g_list_model_get_n_items (G_LIST_MODEL (model)),
             end_changes.total / 1000.0, end_changes.count);

    /* 2. Post-load settling: subfolder item counts etc. trickle in. */
    end_changes = (Timer) { 0 };
    changed = 0;
    start = g_get_monotonic_time ();
    cpu_start = cpu_ms ();
    iterate_until_quiet (3000, 120000);
    g_print ("settle:          %8.1f ms  (main thread CPU %.1f ms, %u file-changed, end-file-changes %.1f ms in %u calls)\n",
             ms_since (start) - 3000, cpu_ms () - cpu_start, changed, end_changes.total / 1000.0, end_changes.count);

    /* 3. Touch files after load. */
    end_changes = (Timer) { 0 };
    changed = 0;
    start = g_get_monotonic_time ();
    for (guint i = 0; i < MIN (n_changes, n_files); i++)
    {
        g_autofree char *path = g_strdup_printf ("%s/file_%06u.txt", tmp_dir, i * (n_files / MAX (n_changes, 1)));
        g_file_set_contents (path, "yy", 2, NULL);
    }
    iterate_until_quiet (3000, 120000);
    g_print ("touch %5u:     %8.1f ms  (%u file-changed, end-file-changes %.1f ms in %u calls)\n",
             n_changes, ms_since (start) - 3000, changed, end_changes.total / 1000.0, end_changes.count);

    /* 4. Select half of the files. */
    g_autolist (NautilusFile) selection = NULL;
    for (guint i = 0; i < n_files; i += 2)
    {
        g_autofree char *path = g_strdup_printf ("%s/file_%06u.txt", tmp_dir, i);
        g_autoptr (GFile) location = g_file_new_for_path (path);

        selection = g_list_prepend (selection, nautilus_file_get (location));
    }
    start = g_get_monotonic_time ();
    nautilus_files_view_set_selection (view, selection, NAUTILUS_SELECTION_SOURCE_MANUAL);
    g_print ("select %6u:    %8.1f ms\n", g_list_length (selection), ms_since (start));

    /* 5. Revisit while the directory is still cached */
    g_autoptr (NautilusDirectory) cached = nautilus_directory_get (tmp_location);
    nautilus_files_view_set_location (view, home);
    ITERATE_WHILE (nautilus_files_view_get_loading (view));
    end_changes = (Timer) { 0 };
    start = g_get_monotonic_time ();
    nautilus_files_view_set_location (view, tmp_location);
    ITERATE_WHILE (nautilus_files_view_get_loading (view));
    g_print ("revisit:         %8.1f ms  (end-file-changes %.1f ms in %u calls)\n",
             ms_since (start), end_changes.total / 1000.0, end_changes.count);

    /* 6. Refresh (F5): everything gets refetched. */
    g_printerr ("REFRESH-START %d\n", getpid ());
    iterate_until_quiet (1000, 120000);
    end_changes = (Timer) { 0 };
    changed = 0;
    start = g_get_monotonic_time ();
    cpu_start = cpu_ms ();
    /* Same as the slot's reload action. */
    g_autoptr (NautilusFile) directory_as_file = nautilus_file_get (tmp_location);
    nautilus_file_invalidate_all_attributes (directory_as_file);
    nautilus_directory_force_reload (cached);
    nautilus_files_view_set_location (view, tmp_location);
    ITERATE_WHILE (nautilus_files_view_get_loading (view));
    double load_ms = ms_since (start);
    iterate_until_quiet (3000, 120000);
    g_print ("refresh:         %8.1f ms  (settled after %.1f ms, main thread CPU %.1f ms, %u file-changed, end-file-changes %.1f ms in %u calls)\n",
             load_ms, ms_since (start) - 3000, cpu_ms () - cpu_start, changed,
             end_changes.total / 1000.0, end_changes.count);

    test_clear_tmp_dir ();

    return 0;
}
