/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "nautilus-inspector-stats"

#include "nautilus-inspector-stats.h"

#include <glib-object.h>
#include <string.h>

gboolean nautilus_inspector_stats_enabled = FALSE;
NautilusInspectorStats nautilus_inspector_stats = { 0 };

static NautilusSearchStats *shell_search = NULL;

void
nautilus_inspector_stats_set_shown (gboolean shown)
{
    nautilus_inspector_stats_enabled = shown;
}

void
nautilus_inspector_stats_add_timing (NautilusTimingStats *timing,
                                     gint64               duration)
{
    timing->n_calls += 1;
    timing->total_time += duration;
    timing->max_time = MAX (timing->max_time, duration);
}

static NautilusSearchProviderStats *
get_search_provider_stats (NautilusSearchStats *search,
                           gconstpointer        provider)
{
    for (guint i = 0; i < search->n_providers; i++)
    {
        if (search->providers[i].provider == provider)
        {
            return &search->providers[i];
        }
    }

    return NULL;
}

static void
clear_search (NautilusSearchStats *search)
{
    g_free (search->text);
    memset (search, 0, sizeof (*search));
}

void
nautilus_inspector_stats_record_search_start (NautilusSearchStats *search,
                                              const char          *text)
{
    clear_search (search);

    /* Searches are only timed if the inspector is shown when they start. */
    if (NAUTILUS_INSPECTOR_STATS_ENABLED ())
    {
        search->text = g_strdup (text);
        search->start_time = g_get_monotonic_time ();
    }
}

void
nautilus_inspector_stats_record_search_provider_start (NautilusSearchStats *search,
                                                       gconstpointer        provider)
{
    if (search->start_time == 0 || search->n_providers == NAUTILUS_SEARCH_MAX_PROVIDERS)
    {
        return;
    }

    search->providers[search->n_providers] = (NautilusSearchProviderStats)
    {
        .provider = provider,
        .name = G_OBJECT_TYPE_NAME (provider),
    };
    search->n_providers += 1;
}

void
nautilus_inspector_stats_record_search_hits (NautilusSearchStats *search,
                                             gconstpointer        provider,
                                             guint                n_hits)
{
    NautilusSearchProviderStats *stats = get_search_provider_stats (search, provider);

    if (stats == NULL)
    {
        return;
    }

    if (stats->first_hit_time == 0)
    {
        stats->first_hit_time = g_get_monotonic_time ();
    }
    stats->n_hits += n_hits;
}

void
nautilus_inspector_stats_record_search_provider_end (NautilusSearchStats *search,
                                                     gconstpointer        provider)
{
    NautilusSearchProviderStats *stats = get_search_provider_stats (search, provider);

    if (stats != NULL)
    {
        stats->end_time = g_get_monotonic_time ();
    }
}

void
nautilus_inspector_stats_record_search_end (NautilusSearchStats *search)
{
    if (search->start_time != 0)
    {
        search->end_time = g_get_monotonic_time ();
    }
}

NautilusSearchStats *
nautilus_inspector_stats_search_new (void)
{
    return g_rc_box_new0 (NautilusSearchStats);
}

void
nautilus_inspector_stats_search_unref (NautilusSearchStats *search)
{
    g_rc_box_release_full (search, (GDestroyNotify) clear_search);
}

void
nautilus_inspector_stats_set_shell_search (NautilusSearchStats *search)
{
    NautilusSearchStats *old_search = g_steal_pointer (&shell_search);

    shell_search = g_rc_box_acquire (search);
    g_clear_pointer (&old_search, nautilus_inspector_stats_search_unref);
}

const NautilusSearchStats *
nautilus_inspector_stats_get_shell_search (void)
{
    return shell_search;
}
