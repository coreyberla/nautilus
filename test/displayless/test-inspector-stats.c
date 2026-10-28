/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib-object.h>

#include <nautilus-inspector-stats.h>

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

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_set_nonfatal_assertions ();

    g_test_add_func ("/inspector-stats/timing",
                     test_timing);

    return g_test_run ();
}
