/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib-object.h>
#include <string.h>

#include <nautilus-inspector-stats.h>

static void
reset_stats (void)
{
    nautilus_inspector_stats_set_shown (FALSE);
    memset (&nautilus_inspector_stats, 0, sizeof (nautilus_inspector_stats));
}

static void
test_timing (void)
{
    NautilusTimingStats timing = { 0 };

    nautilus_inspector_stats_add_timing (&timing, 300);
    nautilus_inspector_stats_add_timing (&timing, 1000);
    nautilus_inspector_stats_add_timing (&timing, 200);

    g_assert_cmpuint (timing.n_calls, ==, 3);
    g_assert_cmpint (timing.total_time, ==, 1500);
    g_assert_cmpint (timing.max_time, ==, 1000);
}

static void
test_counters_only_while_shown (void)
{
    reset_stats ();

    NAUTILUS_INSPECTOR_STATS_ADD (files_added, 5);
    g_assert_cmpuint (nautilus_inspector_stats.files_added, ==, 0);

    nautilus_inspector_stats_set_shown (TRUE);
    NAUTILUS_INSPECTOR_STATS_ADD (files_added, 5);
    NAUTILUS_INSPECTOR_STATS_ADD (files_added, 2);
    g_assert_cmpuint (nautilus_inspector_stats.files_added, ==, 7);

    reset_stats ();
}

static void
test_extension_calls (void)
{
    g_autoptr (GObject) provider = g_object_new (G_TYPE_OBJECT, NULL);
    g_autoptr (GObject) other_provider = g_object_new (G_TYPE_OBJECT, NULL);
    const NautilusExtensionStats *stats;

    g_assert_null (nautilus_inspector_stats_get_extension (provider));

    nautilus_inspector_stats_record_extension_call (provider, NAUTILUS_EXTENSION_CALL_INFO, 100);
    nautilus_inspector_stats_record_extension_call (provider, NAUTILUS_EXTENSION_CALL_INFO, 300);
    nautilus_inspector_stats_record_extension_call (provider, NAUTILUS_EXTENSION_CALL_FILE_MENU, 50);
    nautilus_inspector_stats_record_extension_info_outcome (provider, NAUTILUS_EXTENSION_INFO_ASYNC);
    nautilus_inspector_stats_record_extension_info_outcome (provider, NAUTILUS_EXTENSION_INFO_CANCELLED);

    stats = nautilus_inspector_stats_get_extension (provider);
    g_assert_nonnull (stats);
    g_assert_cmpuint (stats->calls[NAUTILUS_EXTENSION_CALL_INFO].n_calls, ==, 2);
    g_assert_cmpint (stats->calls[NAUTILUS_EXTENSION_CALL_INFO].total_time, ==, 400);
    g_assert_cmpint (stats->calls[NAUTILUS_EXTENSION_CALL_INFO].max_time, ==, 300);
    g_assert_cmpuint (stats->calls[NAUTILUS_EXTENSION_CALL_FILE_MENU].n_calls, ==, 1);
    g_assert_cmpuint (stats->calls[NAUTILUS_EXTENSION_CALL_COLUMNS].n_calls, ==, 0);
    g_assert_cmpuint (stats->info_outcomes[NAUTILUS_EXTENSION_INFO_ASYNC], ==, 1);
    g_assert_cmpuint (stats->info_outcomes[NAUTILUS_EXTENSION_INFO_FAILED], ==, 0);
    g_assert_cmpuint (stats->info_outcomes[NAUTILUS_EXTENSION_INFO_CANCELLED], ==, 1);

    /* Providers are tracked separately. */
    g_assert_null (nautilus_inspector_stats_get_extension (other_provider));
}

static void
test_search_not_timed_while_hidden (void)
{
    g_autoptr (GObject) provider = g_object_new (G_TYPE_OBJECT, NULL);
    NautilusSearchStats *search = nautilus_inspector_stats_search_new ();

    reset_stats ();

    nautilus_inspector_stats_record_search_start (search, "hidden");
    nautilus_inspector_stats_record_search_provider_start (search, provider);
    nautilus_inspector_stats_record_search_hits (search, provider, 3);
    nautilus_inspector_stats_record_search_provider_end (search, provider);
    nautilus_inspector_stats_record_search_end (search);

    g_assert_cmpint (search->start_time, ==, 0);
    g_assert_cmpint (search->end_time, ==, 0);
    g_assert_null (search->text);
    g_assert_cmpuint (search->n_providers, ==, 0);

    nautilus_inspector_stats_search_unref (search);
}

static void
test_search_providers (void)
{
    g_autoptr (GObject) provider = g_object_new (G_TYPE_OBJECT, NULL);
    g_autoptr (GObject) other_provider = g_object_new (G_TYPE_OBJECT, NULL);
    NautilusSearchStats *search = nautilus_inspector_stats_search_new ();

    reset_stats ();
    nautilus_inspector_stats_set_shown (TRUE);

    nautilus_inspector_stats_record_search_start (search, "query");
    nautilus_inspector_stats_record_search_provider_start (search, provider);
    nautilus_inspector_stats_record_search_provider_start (search, other_provider);
    nautilus_inspector_stats_record_search_hits (search, provider, 3);
    nautilus_inspector_stats_record_search_hits (search, provider, 2);
    nautilus_inspector_stats_record_search_provider_end (search, provider);

    g_assert_cmpstr (search->text, ==, "query");
    g_assert_cmpint (search->start_time, !=, 0);
    g_assert_cmpuint (search->n_providers, ==, 2);
    g_assert_cmpstr (search->providers[0].name, ==, "GObject");
    g_assert_cmpuint (search->providers[0].n_hits, ==, 5);
    g_assert_cmpint (search->providers[0].first_hit_time, >=, search->start_time);
    g_assert_cmpint (search->providers[0].end_time, >=, search->providers[0].first_hit_time);
    g_assert_cmpuint (search->providers[1].n_hits, ==, 0);
    g_assert_cmpint (search->providers[1].first_hit_time, ==, 0);
    g_assert_cmpint (search->providers[1].end_time, ==, 0);
    g_assert_cmpint (search->end_time, ==, 0);

    nautilus_inspector_stats_record_search_end (search);
    g_assert_cmpint (search->end_time, >=, search->start_time);

    /* Restarting a search starts from scratch. */
    nautilus_inspector_stats_record_search_start (search, "new query");
    g_assert_cmpstr (search->text, ==, "new query");
    g_assert_cmpuint (search->n_providers, ==, 0);
    g_assert_cmpint (search->end_time, ==, 0);

    nautilus_inspector_stats_search_unref (search);
    reset_stats ();
}

static void
test_concurrent_searches (void)
{
    g_autoptr (GObject) provider = g_object_new (G_TYPE_OBJECT, NULL);
    NautilusSearchStats *first = nautilus_inspector_stats_search_new ();
    NautilusSearchStats *second = nautilus_inspector_stats_search_new ();

    reset_stats ();
    nautilus_inspector_stats_set_shown (TRUE);

    nautilus_inspector_stats_record_search_start (first, "first");
    nautilus_inspector_stats_record_search_provider_start (first, provider);
    nautilus_inspector_stats_record_search_start (second, "second");
    nautilus_inspector_stats_record_search_provider_start (second, provider);

    /* The first search finishing late must not affect the second. */
    nautilus_inspector_stats_record_search_hits (first, provider, 10);
    nautilus_inspector_stats_record_search_end (first);

    g_assert_cmpstr (first->text, ==, "first");
    g_assert_cmpuint (first->providers[0].n_hits, ==, 10);
    g_assert_cmpint (first->end_time, !=, 0);
    g_assert_cmpstr (second->text, ==, "second");
    g_assert_cmpuint (second->providers[0].n_hits, ==, 0);
    g_assert_cmpint (second->end_time, ==, 0);

    nautilus_inspector_stats_search_unref (first);
    nautilus_inspector_stats_search_unref (second);
    reset_stats ();
}

static void
test_shell_search_outlives_engine (void)
{
    NautilusSearchStats *first = nautilus_inspector_stats_search_new ();
    NautilusSearchStats *second = nautilus_inspector_stats_search_new ();

    reset_stats ();
    nautilus_inspector_stats_set_shown (TRUE);

    nautilus_inspector_stats_record_search_start (first, "first");
    nautilus_inspector_stats_set_shell_search (first);

    /* The engine owning the search goes away when the search ends. */
    nautilus_inspector_stats_search_unref (first);
    g_assert_cmpstr (nautilus_inspector_stats_get_shell_search ()->text, ==, "first");

    nautilus_inspector_stats_record_search_start (second, "second");
    nautilus_inspector_stats_set_shell_search (second);
    g_assert_true (nautilus_inspector_stats_get_shell_search () == second);

    nautilus_inspector_stats_search_unref (second);
    g_assert_cmpstr (nautilus_inspector_stats_get_shell_search ()->text, ==, "second");

    reset_stats ();
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_set_nonfatal_assertions ();

    g_test_add_func ("/inspector-stats/timing",
                     test_timing);
    g_test_add_func ("/inspector-stats/counters-only-while-shown",
                     test_counters_only_while_shown);
    g_test_add_func ("/inspector-stats/extension-calls",
                     test_extension_calls);
    g_test_add_func ("/inspector-stats/search-not-timed-while-hidden",
                     test_search_not_timed_while_hidden);
    g_test_add_func ("/inspector-stats/search-providers",
                     test_search_providers);
    g_test_add_func ("/inspector-stats/concurrent-searches",
                     test_concurrent_searches);
    g_test_add_func ("/inspector-stats/shell-search-outlives-engine",
                     test_shell_search_outlives_engine);

    return g_test_run ();
}
