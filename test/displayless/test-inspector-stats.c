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

    return g_test_run ();
}
