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

/* Extension provider → NautilusExtensionStats. Providers live as long as the
 * application, so they are not referenced. */
static GHashTable *extension_stats = NULL;

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

static NautilusExtensionStats *
ensure_extension_stats (gpointer provider)
{
    NautilusExtensionStats *stats;

    if (extension_stats == NULL)
    {
        extension_stats = g_hash_table_new_full (NULL, NULL, NULL, g_free);
    }

    stats = g_hash_table_lookup (extension_stats, provider);
    if (stats == NULL)
    {
        stats = g_new0 (NautilusExtensionStats, 1);
        g_hash_table_insert (extension_stats, provider, stats);
    }

    return stats;
}

void
nautilus_inspector_stats_record_extension_call (gpointer              provider,
                                                NautilusExtensionCall call,
                                                gint64                duration)
{
    nautilus_inspector_stats_add_timing (&ensure_extension_stats (provider)->calls[call], duration);
}

void
nautilus_inspector_stats_record_extension_info_outcome (gpointer                     provider,
                                                        NautilusExtensionInfoOutcome outcome)
{
    ensure_extension_stats (provider)->info_outcomes[outcome] += 1;
}

const NautilusExtensionStats *
nautilus_inspector_stats_get_extension (gpointer provider)
{
    return extension_stats != NULL ? g_hash_table_lookup (extension_stats, provider) : NULL;
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
