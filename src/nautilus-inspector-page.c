/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* A page in the GTK Inspector showing statistics about Files. The view
 * statistics follow the view of the active tab in the most recently focused
 * window, or the view containing the widget picked in the Objects page.
 * Statistics are only collected while the inspector is shown, see
 * nautilus-inspector-stats.h. */

#define G_LOG_DOMAIN "nautilus-inspector-page"

#include "nautilus-inspector-page.h"

#include "nautilus-directory.h"
#include "nautilus-files-view.h"
#include "nautilus-image.h"
#include "nautilus-inspector-stats.h"
#include "nautilus-search-directory.h"
#include "nautilus-thumbnails.h"
#include "nautilus-window.h"
#include "nautilus-window-slot.h"

#include <gtk/gtk.h>
#include <string.h>

#define REFRESH_INTERVAL_MS 250
#define RATE_INTERVAL_US G_USEC_PER_SEC
#define DIRECTORY_LIST_INTERVAL_US G_USEC_PER_SEC

/* Search providers as named by their type, without the "NautilusSearchEngine" prefix. */
static const struct
{
    const char *type_suffix;
    const char *title;
} search_engines[] =
{
    { "Localsearch", "Indexer (LocalSearch)" },
    { "Model", "Current View" },
    { "Recent", "Recent Files" },
    { "Simple", "Folder Crawl" },
};

enum
{
    RATE_FILES_ADDED,
    RATE_FILES_CHANGED,
    RATE_FILES_REMOVED,
    RATE_FILES_MOVED,
    RATE_DEEP_COUNT_FILES,
    N_RATES,
};

typedef struct
{
    guint statuses[NAUTILUS_IMAGE_STATUS_FALLBACK + 1];
    guint sources[NAUTILUS_IMAGE_N_SOURCES];
    NautilusTimingStats load_times;
} OnScreenIcons;

typedef struct
{
    AdwPreferencesGroup *group;
    GtkLabel *total_label;
    GtkLabel *engine_labels[G_N_ELEMENTS (search_engines)];
} SearchWidgets;

struct _NautilusInspectorPage
{
    AdwBin parent_instance;

    GObject *object;
    guint refresh_id;
    GtkWidget *inspector_window;

    /* View */
    AdwPreferencesGroup *view_group;
    GtkLabel *location_label;
    GtkLabel *state_label;
    GtkLabel *items_label;
    GtkLabel *loads_label;

    AdwPreferencesGroup *load_group;
    GtkLabel *attributes_label;
    GtkLabel *first_items_label;
    GtkLabel *total_time_label;
    GtkLabel *batches_label;
    GtkLabel *sort_label;
    GtkLabel *splice_label;

    SearchWidgets view_search;

    GtkLabel *status_labels[NAUTILUS_IMAGE_STATUS_FALLBACK + 1];
    GtkLabel *source_labels[NAUTILUS_IMAGE_N_SOURCES];
    GtkLabel *time_to_thumbnail_label;

    /* App-wide */
    SearchWidgets shell_search;

    GtkLabel *rate_labels[N_RATES];
    guint64 rate_previous_counts[N_RATES];
    double rates[N_RATES];
    gint64 rate_time;

    GtkLabel *directories_label;
    GtkLabel *fully_loaded_label;
    GtkLabel *files_label;
    GtkLabel *watched_label;
    GtkLabel *monitors_label;
    GtkLabel *files_queued_label;
    GtkLabel *async_jobs_label;
    GtkLabel *waiting_for_job_label;
    AdwExpanderRow *directory_list_row;
    GPtrArray *directory_rows;
    gint64 directory_list_time;

    GtkLabel *deep_counting_label;
    GtkLabel *deep_counts_label;

    GtkLabel *thumbnails_queued_label;
    GtkLabel *thumbnails_running_label;
    GtkLabel *thumbnail_wait_label;
    GtkLabel *thumbnail_generation_label;
    GtkLabel *thumbnails_generated_label;
    GtkLabel *thumbnails_failed_label;
    GtkLabel *thumbnail_cache_label;
};

enum
{
    PROP_0,
    PROP_TITLE,
    PROP_OBJECT,
    N_PROPS,
};

G_DEFINE_FINAL_TYPE (NautilusInspectorPage, nautilus_inspector_page, ADW_TYPE_BIN)

static GtkLabel *
add_row (AdwPreferencesGroup *group,
         const char          *title)
{
    GtkWidget *row = adw_action_row_new ();
    GtkWidget *label = gtk_label_new (NULL);

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
    gtk_label_set_selectable (GTK_LABEL (label), TRUE);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class (label, "monospace");
    adw_action_row_add_suffix (ADW_ACTION_ROW (row), label);
    adw_preferences_group_add (group, row);

    return GTK_LABEL (label);
}

static void
set_row_subtitle (GtkLabel   *label,
                  const char *subtitle)
{
    GtkWidget *row = gtk_widget_get_ancestor (GTK_WIDGET (label), ADW_TYPE_ACTION_ROW);

    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
}

static AdwPreferencesGroup *
add_group (AdwPreferencesPage *page,
           const char         *title,
           const char         *description)
{
    GtkWidget *group = adw_preferences_group_new ();

    adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), title);
    adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group), description);
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    return ADW_PREFERENCES_GROUP (group);
}

static AdwPreferencesPage *
add_page (AdwViewStack *stack,
          const char   *name,
          const char   *title,
          const char   *icon_name)
{
    GtkWidget *page = adw_preferences_page_new ();

    adw_view_stack_add_titled_with_icon (stack, page, name, title, icon_name);

    return ADW_PREFERENCES_PAGE (page);
}

static void
set_label_uint (GtkLabel *label,
                guint64   value)
{
    g_autofree char *text = g_strdup_printf ("%" G_GUINT64_FORMAT, value);

    gtk_label_set_label (label, text);
}

/* While running and end is not reached yet, shows the time so far with an
 * ellipsis, so it keeps counting up. */
static void
set_label_duration (GtkLabel *label,
                    gint64    start,
                    gint64    end,
                    gboolean  running)
{
    if (start == 0 || (end == 0 && !running))
    {
        gtk_label_set_label (label, "—");
    }
    else
    {
        gboolean reached = end != 0;
        g_autofree char *text = g_strdup_printf ("%.1f ms%s",
                                                 ((reached ? end : g_get_monotonic_time ()) - start) / 1000.0,
                                                 reached ? "" : "…");

        gtk_label_set_label (label, text);
    }
}

static void
set_label_ms (GtkLabel *label,
              gint64    duration)
{
    g_autofree char *text = g_strdup_printf ("%.1f ms", duration / 1000.0);

    gtk_label_set_label (label, text);
}

static void
set_label_timing (GtkLabel                  *label,
                  const NautilusTimingStats *timing)
{
    if (timing == NULL || timing->n_calls == 0)
    {
        gtk_label_set_label (label, "—");
    }
    else
    {
        g_autofree char *text = g_strdup_printf ("%u × avg %.1f ms · max %.1f ms",
                                                 timing->n_calls,
                                                 timing->total_time / 1000.0 / timing->n_calls,
                                                 timing->max_time / 1000.0);

        gtk_label_set_label (label, text);
    }
}

static NautilusFilesView *
get_picked_view (NautilusInspectorPage *self)
{
    if (!GTK_IS_WIDGET (self->object))
    {
        return NULL;
    }

    GtkWidget *view = gtk_widget_get_ancestor (GTK_WIDGET (self->object), NAUTILUS_TYPE_FILES_VIEW);

    return view != NULL ? NAUTILUS_FILES_VIEW (view) : NULL;
}

static NautilusFilesView *
get_active_tab_view (void)
{
    GtkApplication *app = GTK_APPLICATION (g_application_get_default ());

    /* Windows are sorted by most recently focused. */
    for (GList *l = gtk_application_get_windows (app); l != NULL; l = l->next)
    {
        if (NAUTILUS_IS_WINDOW (l->data))
        {
            g_autoptr (NautilusWindowSlot) slot = NULL;

            g_object_get (l->data, "active-slot", &slot, NULL);

            return slot != NULL ? nautilus_window_slot_get_current_view (slot) : NULL;
        }
    }

    return NULL;
}

static void
count_on_screen_icons (GtkWidget     *widget,
                       OnScreenIcons *icons)
{
    if (!gtk_widget_get_mapped (widget))
    {
        return;
    }

    if (NAUTILUS_IS_IMAGE (widget))
    {
        NautilusImageSource source;
        gint64 load_duration;

        nautilus_image_get_debug_info (NAUTILUS_IMAGE (widget), &source, &load_duration);
        icons->statuses[nautilus_image_get_status (NAUTILUS_IMAGE (widget))] += 1;
        icons->sources[source] += 1;
        if (load_duration != 0)
        {
            nautilus_inspector_stats_add_timing (&icons->load_times, load_duration);
        }

        return;
    }

    for (GtkWidget *child = gtk_widget_get_first_child (widget);
         child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        count_on_screen_icons (child, icons);
    }
}

static void
update_view_stats (NautilusInspectorPage *self,
                   NautilusFilesView     *view,
                   gboolean               picked)
{
    const NautilusFilesViewLoadStats *stats = nautilus_files_view_get_load_stats (view);
    GFile *location = nautilus_files_view_get_location (view);
    g_autofree char *uri = location != NULL ? g_file_get_uri (location) : NULL;
    gboolean loading = nautilus_files_view_is_loading (view);

    adw_preferences_group_set_description (self->view_group,
                                           picked ?
                                           "Showing the view containing the selected object" :
                                           "Showing the view of the active tab");
    gtk_label_set_label (self->location_label, uri != NULL ? uri : "—");
    gtk_label_set_label (self->state_label,
                         loading ? "Loading" :
                                   stats->n_loads == 0 ? "Idle" :
                                                         stats->all_files_seen ? "Done" : "Stopped");
    set_label_uint (self->items_label, nautilus_files_view_get_n_items (view));
    set_label_uint (self->loads_label, stats->n_loads);

    adw_preferences_group_set_description (self->load_group,
                                           stats->n_loads > 0 && stats->start_time == 0 ?
                                           "Not timed, the inspector was hidden when it started" :
                                           "Time since the load started");

    set_label_duration (self->attributes_label, stats->start_time, stats->attributes_time, loading);
    set_label_duration (self->first_items_label, stats->start_time, stats->first_items_time, loading);
    set_label_duration (self->total_time_label, stats->start_time, loading ? 0 : stats->end_time, loading);

    if (stats->start_time != 0)
    {
        set_label_uint (self->batches_label, stats->n_batches);
        set_label_ms (self->sort_label, stats->sort_duration);
        set_label_ms (self->splice_label, stats->splice_duration);
    }
    else
    {
        gtk_label_set_label (self->batches_label, "—");
        gtk_label_set_label (self->sort_label, "—");
        gtk_label_set_label (self->splice_label, "—");
    }

    OnScreenIcons icons = { 0 };

    count_on_screen_icons (GTK_WIDGET (view), &icons);
    for (guint i = 0; i < G_N_ELEMENTS (self->status_labels); i++)
    {
        set_label_uint (self->status_labels[i], icons.statuses[i]);
    }
    for (guint i = 0; i < G_N_ELEMENTS (self->source_labels); i++)
    {
        set_label_uint (self->source_labels[i], icons.sources[i]);
    }
    set_label_timing (self->time_to_thumbnail_label, &icons.load_times);
}

static void
clear_view_stats (NautilusInspectorPage *self)
{
    GtkLabel *labels[] =
    {
        self->location_label, self->state_label, self->items_label, self->loads_label,
        self->attributes_label, self->first_items_label, self->total_time_label,
        self->batches_label, self->sort_label, self->splice_label,
        self->time_to_thumbnail_label,
    };

    adw_preferences_group_set_description (self->view_group, "No view found");
    for (guint i = 0; i < G_N_ELEMENTS (labels); i++)
    {
        gtk_label_set_label (labels[i], "—");
    }
    for (guint i = 0; i < G_N_ELEMENTS (self->status_labels); i++)
    {
        gtk_label_set_label (self->status_labels[i], "—");
    }
    for (guint i = 0; i < G_N_ELEMENTS (self->source_labels); i++)
    {
        gtk_label_set_label (self->source_labels[i], "—");
    }
}

static void
update_search_stats (SearchWidgets             *widgets,
                     const NautilusSearchStats *search,
                     const char                *no_search_description)
{
    if (search == NULL || search->start_time == 0)
    {
        adw_preferences_group_set_description (widgets->group,
                                               search == NULL ? no_search_description :
                                                                "Not timed, the inspector was hidden when it started");
        gtk_label_set_label (widgets->total_label, "—");
        for (guint i = 0; i < G_N_ELEMENTS (search_engines); i++)
        {
            gtk_label_set_label (widgets->engine_labels[i], "—");
        }

        return;
    }

    g_autofree char *description = g_strdup_printf ("“%s”%s", search->text,
                                                    search->end_time == 0 ? ", running" : "");
    adw_preferences_group_set_description (widgets->group, description);
    set_label_duration (widgets->total_label, search->start_time, search->end_time, TRUE);

    for (guint i = 0; i < G_N_ELEMENTS (search_engines); i++)
    {
        const NautilusSearchProviderStats *provider = NULL;

        for (guint j = 0; j < search->n_providers; j++)
        {
            if (g_str_has_suffix (search->providers[j].name, search_engines[i].type_suffix))
            {
                provider = &search->providers[j];
            }
        }

        if (provider == NULL)
        {
            gtk_label_set_label (widgets->engine_labels[i], "not used");
            continue;
        }

        g_autofree char *first_hit = provider->first_hit_time != 0 ?
                                     g_strdup_printf ("%.1f ms", (provider->first_hit_time - search->start_time) / 1000.0) :
                                     g_strdup ("—");
        g_autofree char *done = provider->end_time != 0 ?
                                g_strdup_printf ("%.1f ms", (provider->end_time - search->start_time) / 1000.0) :
                                g_strdup ("running");
        g_autofree char *text = g_strdup_printf ("%u hits · first %s · done %s",
                                                 provider->n_hits, first_hit, done);

        gtk_label_set_label (widgets->engine_labels[i], text);
    }
}

static void
update_view_search_stats (NautilusInspectorPage *self,
                          NautilusFilesView     *view)
{
    g_autoptr (NautilusDirectory) directory = NULL;
    NautilusSearchEngine *engine = NULL;

    if (view != NULL && nautilus_files_view_is_searching (view))
    {
        directory = nautilus_directory_get (nautilus_files_view_get_location (view));
        if (NAUTILUS_IS_SEARCH_DIRECTORY (directory))
        {
            engine = nautilus_search_directory_get_engine (NAUTILUS_SEARCH_DIRECTORY (directory));
        }
    }

    update_search_stats (&self->view_search,
                         engine != NULL ? nautilus_search_engine_get_stats (engine) : NULL,
                         "This view is not searching");
}

static void
update_shell_search_stats (NautilusInspectorPage *self)
{
    update_search_stats (&self->shell_search,
                         nautilus_inspector_stats_get_shell_search (),
                         "No search from GNOME Shell since startup");
}

static SearchWidgets
add_search_group (AdwPreferencesPage *page,
                  const char         *title)
{
    SearchWidgets widgets = { .group = add_group (page, title, NULL) };

    widgets.total_label = add_row (widgets.group, "Total");
    for (guint i = 0; i < G_N_ELEMENTS (search_engines); i++)
    {
        widgets.engine_labels[i] = add_row (widgets.group, search_engines[i].title);
    }

    return widgets;
}

static void
update_rates (NautilusInspectorPage *self)
{
    const NautilusInspectorStats *stats = &nautilus_inspector_stats;
    guint64 counts[N_RATES] =
    {
        [RATE_FILES_ADDED] = stats->files_added,
        [RATE_FILES_CHANGED] = stats->files_changed,
        [RATE_FILES_REMOVED] = stats->files_removed,
        [RATE_FILES_MOVED] = stats->files_moved,
        [RATE_DEEP_COUNT_FILES] = stats->deep_count_files_scanned,
    };
    gint64 now = g_get_monotonic_time ();

    if (self->rate_time == 0)
    {
        memcpy (self->rate_previous_counts, counts, sizeof (counts));
        self->rate_time = now;
    }
    else if (now - self->rate_time >= RATE_INTERVAL_US)
    {
        for (guint i = 0; i < N_RATES; i++)
        {
            self->rates[i] = (counts[i] - self->rate_previous_counts[i]) * (double) G_USEC_PER_SEC /
                             (now - self->rate_time);
        }
        memcpy (self->rate_previous_counts, counts, sizeof (counts));
        self->rate_time = now;
    }

    for (guint i = 0; i < N_RATES; i++)
    {
        g_autofree char *text = g_strdup_printf ("%.0f/s · %" G_GUINT64_FORMAT " total",
                                                 self->rates[i], counts[i]);

        gtk_label_set_label (self->rate_labels[i], text);
    }
}

static void
update_directory_list (NautilusInspectorPage *self)
{
    g_autoptr (GArray) infos = nautilus_directory_get_debug_infos ();

    for (guint i = 0; i < self->directory_rows->len; i++)
    {
        adw_expander_row_remove (self->directory_list_row, self->directory_rows->pdata[i]);
    }
    g_ptr_array_set_size (self->directory_rows, 0);

    for (guint i = 0; i < infos->len; i++)
    {
        NautilusDirectoryDebugInfo *info = &g_array_index (infos, NautilusDirectoryDebugInfo, i);
        GtkWidget *row = adw_action_row_new ();
        g_autofree char *subtitle = g_strdup_printf ("%u files · %u monitor requests · %u refs%s%s%s",
                                                     info->n_files, info->n_monitors, info->ref_count,
                                                     info->fully_loaded ? " · listed" : "",
                                                     info->watched ? " · watched" : "",
                                                     info->deep_counting ? " · deep counting" : "");

        adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), info->uri);
        adw_action_row_set_title_lines (ADW_ACTION_ROW (row), 1);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
        adw_expander_row_add_row (self->directory_list_row, row);
        g_ptr_array_add (self->directory_rows, row);
    }
}

static void
update_app_stats (NautilusInspectorPage *self)
{
    const NautilusInspectorStats *stats = &nautilus_inspector_stats;
    NautilusDirectoryCacheStats cache;
    g_autofree char *async_jobs = NULL;
    g_autofree char *directory_count = NULL;
    g_autofree char *cache_size = NULL;
    guint n_queued, n_running, n_cached, max_cached;
    gint64 now = g_get_monotonic_time ();

    update_shell_search_stats (self);
    update_rates (self);

    nautilus_directory_get_cache_stats (&cache);
    set_label_uint (self->directories_label, cache.n_directories);
    set_label_uint (self->fully_loaded_label, cache.n_fully_loaded);
    set_label_uint (self->files_label, cache.n_files);
    set_label_uint (self->watched_label, cache.n_watched);
    set_label_uint (self->monitors_label, cache.n_monitors);
    set_label_uint (self->files_queued_label, cache.n_files_queued);
    async_jobs = g_strdup_printf ("%u / %u", cache.n_async_jobs, cache.n_async_jobs_max);
    gtk_label_set_label (self->async_jobs_label, async_jobs);
    set_label_uint (self->waiting_for_job_label, cache.n_waiting_for_job);
    set_label_uint (self->deep_counting_label, cache.n_deep_counting);
    set_label_timing (self->deep_counts_label, &stats->deep_counts);

    directory_count = g_strdup_printf ("%u directories", cache.n_directories);
    adw_expander_row_set_subtitle (self->directory_list_row, directory_count);
    if (adw_expander_row_get_expanded (self->directory_list_row) &&
        now - self->directory_list_time >= DIRECTORY_LIST_INTERVAL_US)
    {
        update_directory_list (self);
        self->directory_list_time = now;
    }

    nautilus_thumbnail_get_queue_state (&n_queued, &n_running);
    set_label_uint (self->thumbnails_queued_label, n_queued);
    set_label_uint (self->thumbnails_running_label, n_running);
    set_label_timing (self->thumbnail_wait_label, &stats->thumbnail_wait);
    set_label_timing (self->thumbnail_generation_label, &stats->thumbnail_generation);
    set_label_uint (self->thumbnails_generated_label, stats->thumbnails_generated);
    set_label_uint (self->thumbnails_failed_label, stats->thumbnails_failed);

    nautilus_image_get_thumbnail_cache_size (&n_cached, &max_cached);
    cache_size = g_strdup_printf ("%u / %u", n_cached, max_cached);
    gtk_label_set_label (self->thumbnail_cache_label, cache_size);
}

static gboolean
refresh (gpointer user_data)
{
    NautilusInspectorPage *self = user_data;
    NautilusFilesView *picked_view = get_picked_view (self);
    NautilusFilesView *view = picked_view != NULL ? picked_view : get_active_tab_view ();

    if (view != NULL)
    {
        update_view_stats (self, view, picked_view != NULL);
    }
    else
    {
        clear_view_stats (self);
    }
    update_view_search_stats (self, view);

    update_app_stats (self);

    return G_SOURCE_CONTINUE;
}

static void
build_view_page (NautilusInspectorPage *self,
                 AdwPreferencesPage    *page)
{
    AdwPreferencesGroup *group;

    self->view_group = add_group (page, "View", NULL);
    self->location_label = add_row (self->view_group, "Location");
    self->state_label = add_row (self->view_group, "State");
    self->items_label = add_row (self->view_group, "Items");
    self->loads_label = add_row (self->view_group, "Loads in This View");

    self->load_group = group = add_group (page, "Last Load", NULL);
    self->attributes_label = add_row (group, "Files Listed With Attributes");
    self->first_items_label = add_row (group, "First Items Shown");
    self->total_time_label = add_row (group, "Done");

    group = add_group (page, "Adding Items", "Time spent in the view model during the last load");
    self->batches_label = add_row (group, "Batches");
    self->sort_label = add_row (group, "Sorting Batches");
    self->splice_label = add_row (group, "Splicing Into Model");
    set_row_subtitle (self->splice_label, "Includes re-sorting the view and updating the widgets");

    self->view_search = add_search_group (page, "Search");

    group = add_group (page, "Icons on Screen", "Visible icons in this view, by state");
    self->status_labels[NAUTILUS_IMAGE_STATUS_THUMBNAIL] = add_row (group, "Thumbnail Shown");
    self->status_labels[NAUTILUS_IMAGE_STATUS_LOADING_ATTRIBUTES] = add_row (group, "Querying File Info");
    self->status_labels[NAUTILUS_IMAGE_STATUS_LOADING_THUMBNAIL] = add_row (group, "Loading Thumbnail");
    self->status_labels[NAUTILUS_IMAGE_STATUS_FALLBACK] = add_row (group, "Icon (No Thumbnail)");

    group = add_group (page, "Thumbnails on Screen", "Where the thumbnails of visible icons came from");
    self->source_labels[NAUTILUS_IMAGE_SOURCE_MEMORY_CACHE] = add_row (group, "Memory Cache");
    self->source_labels[NAUTILUS_IMAGE_SOURCE_DISK_CACHE] = add_row (group, "Disk Cache");
    self->source_labels[NAUTILUS_IMAGE_SOURCE_THUMBNAILER] = add_row (group, "Thumbnailer");
    set_row_subtitle (self->source_labels[NAUTILUS_IMAGE_SOURCE_THUMBNAILER], "Generated now, or failed");
    self->source_labels[NAUTILUS_IMAGE_SOURCE_TOO_LARGE] = add_row (group, "Skipped, Too Large");
    self->source_labels[NAUTILUS_IMAGE_SOURCE_UNSUPPORTED] = add_row (group, "Skipped, Unsupported Type");
    self->source_labels[NAUTILUS_IMAGE_SOURCE_NONE] = add_row (group, "Not Decided Yet");
    self->time_to_thumbnail_label = add_row (group, "Time to Thumbnail");
    set_row_subtitle (self->time_to_thumbnail_label, "Until shown, timed while the inspector is shown");
}

static void
build_app_page (NautilusInspectorPage *self,
                AdwPreferencesPage    *page)
{
    AdwPreferencesGroup *group;

    self->shell_search = add_search_group (page, "Last GNOME Shell Search");

    group = add_group (page, "File Changes",
                       "Files reported as changed on disk or by Files itself, while the inspector is shown");
    self->rate_labels[RATE_FILES_ADDED] = add_row (group, "Added");
    self->rate_labels[RATE_FILES_CHANGED] = add_row (group, "Changed");
    self->rate_labels[RATE_FILES_REMOVED] = add_row (group, "Removed");
    self->rate_labels[RATE_FILES_MOVED] = add_row (group, "Moved");

    group = add_group (page, "Directories", "All directories in memory, shared by all windows");
    self->directories_label = add_row (group, "Directories");
    self->fully_loaded_label = add_row (group, "Fully Listed");
    self->files_label = add_row (group, "Files");
    self->watched_label = add_row (group, "Watched for Changes");
    set_row_subtitle (self->watched_label, "Directories with a file system monitor");
    self->monitors_label = add_row (group, "Monitor Requests");
    set_row_subtitle (self->monitors_label, "Views and other parts asking for live updates");
    self->files_queued_label = add_row (group, "Files Queued for I/O");
    self->async_jobs_label = add_row (group, "I/O Jobs Running");
    self->waiting_for_job_label = add_row (group, "Directories Waiting for a Job");
    self->directory_list_row = ADW_EXPANDER_ROW (adw_expander_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->directory_list_row), "All Directories");
    adw_preferences_group_add (group, GTK_WIDGET (self->directory_list_row));

    group = add_group (page, "Deep Counts",
                       "Recursive item counts and sizes of folders, as in the Properties dialog. "
                       "A long running count scanning many files keeps the disk busy.");
    self->deep_counting_label = add_row (group, "Running");
    self->rate_labels[RATE_DEEP_COUNT_FILES] = add_row (group, "Files Scanned");
    self->deep_counts_label = add_row (group, "Completed");

    group = add_group (page, "Thumbnailer",
                       "Shared by all views, which share its results. Totals are while the inspector is shown.");
    self->thumbnails_queued_label = add_row (group, "Queued");
    self->thumbnails_running_label = add_row (group, "Generating Now");
    self->thumbnail_wait_label = add_row (group, "Wait in Queue");
    self->thumbnail_generation_label = add_row (group, "Generation");
    self->thumbnails_generated_label = add_row (group, "Generated");
    self->thumbnails_failed_label = add_row (group, "Failed");
    self->thumbnail_cache_label = add_row (group, "Memory Cache");
}

static void
on_inspector_visible_changed (NautilusInspectorPage *self)
{
    nautilus_inspector_stats_set_shown (gtk_widget_get_visible (self->inspector_window));
}

static void
nautilus_inspector_page_root (GtkWidget *widget)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (widget);

    GTK_WIDGET_CLASS (nautilus_inspector_page_parent_class)->root (widget);

    /* Closing the inspector only hides its window. */
    self->inspector_window = GTK_WIDGET (gtk_widget_get_root (widget));
    g_signal_connect_object (self->inspector_window, "notify::visible",
                             G_CALLBACK (on_inspector_visible_changed), self,
                             G_CONNECT_SWAPPED);
    on_inspector_visible_changed (self);
}

static void
nautilus_inspector_page_unroot (GtkWidget *widget)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (widget);

    g_signal_handlers_disconnect_by_data (self->inspector_window, self);
    self->inspector_window = NULL;
    nautilus_inspector_stats_set_shown (FALSE);

    GTK_WIDGET_CLASS (nautilus_inspector_page_parent_class)->unroot (widget);
}

static void
nautilus_inspector_page_map (GtkWidget *widget)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (widget);

    GTK_WIDGET_CLASS (nautilus_inspector_page_parent_class)->map (widget);

    /* Only poll while the page is shown. */
    refresh (self);
    self->refresh_id = g_timeout_add (REFRESH_INTERVAL_MS, refresh, self);
}

static void
nautilus_inspector_page_unmap (GtkWidget *widget)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (widget);

    g_clear_handle_id (&self->refresh_id, g_source_remove);

    GTK_WIDGET_CLASS (nautilus_inspector_page_parent_class)->unmap (widget);
}

static void
nautilus_inspector_page_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (object);

    switch (property_id)
    {
        case PROP_TITLE:
        {
            g_value_set_string (value, "Files");
        }
        break;

        case PROP_OBJECT:
        {
            g_value_set_object (value, self->object);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
    }
}

static void
nautilus_inspector_page_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (object);

    switch (property_id)
    {
        case PROP_OBJECT:
        {
            g_set_object (&self->object, g_value_get_object (value));
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
    }
}

static void
nautilus_inspector_page_dispose (GObject *object)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (object);

    g_clear_handle_id (&self->refresh_id, g_source_remove);
    g_clear_object (&self->object);

    G_OBJECT_CLASS (nautilus_inspector_page_parent_class)->dispose (object);
}

static void
nautilus_inspector_page_finalize (GObject *object)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (object);

    g_clear_pointer (&self->directory_rows, g_ptr_array_unref);

    G_OBJECT_CLASS (nautilus_inspector_page_parent_class)->finalize (object);
}

static void
nautilus_inspector_page_init (NautilusInspectorPage *self)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    AdwViewStack *stack = ADW_VIEW_STACK (adw_view_stack_new ());
    GtkWidget *switcher = adw_view_switcher_new ();

    self->directory_rows = g_ptr_array_new ();

    build_view_page (self, add_page (stack, "view", "View", "folder-symbolic"));
    build_app_page (self, add_page (stack, "app", "App-wide", "view-grid-symbolic"));

    adw_view_switcher_set_stack (ADW_VIEW_SWITCHER (switcher), stack);
    adw_view_switcher_set_policy (ADW_VIEW_SWITCHER (switcher), ADW_VIEW_SWITCHER_POLICY_WIDE);
    gtk_widget_set_halign (switcher, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top (switcher, 6);
    gtk_widget_set_margin_bottom (switcher, 6);
    gtk_widget_set_vexpand (GTK_WIDGET (stack), TRUE);

    gtk_box_append (GTK_BOX (box), switcher);
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (stack));
    adw_bin_set_child (ADW_BIN (self), box);
}

static void
nautilus_inspector_page_class_init (NautilusInspectorPageClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->get_property = nautilus_inspector_page_get_property;
    object_class->set_property = nautilus_inspector_page_set_property;
    object_class->dispose = nautilus_inspector_page_dispose;
    object_class->finalize = nautilus_inspector_page_finalize;

    widget_class->map = nautilus_inspector_page_map;
    widget_class->unmap = nautilus_inspector_page_unmap;
    widget_class->root = nautilus_inspector_page_root;
    widget_class->unroot = nautilus_inspector_page_unroot;

    /* These are the properties GtkInspector looks for on extension pages. */
    g_object_class_install_property (object_class,
                                     PROP_TITLE,
                                     g_param_spec_string ("title", NULL, NULL,
                                                          NULL,
                                                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (object_class,
                                     PROP_OBJECT,
                                     g_param_spec_object ("object", NULL, NULL,
                                                          G_TYPE_OBJECT,
                                                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

void
nautilus_inspector_page_register (void)
{
    /* The extension point is registered by gtk_init(). */
    if (g_io_extension_point_lookup ("gtk-inspector-page") != NULL)
    {
        g_io_extension_point_implement ("gtk-inspector-page",
                                        NAUTILUS_TYPE_INSPECTOR_PAGE,
                                        "nautilus",
                                        10);
    }
}
