/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "nautilus-inspector-stats"

#include "nautilus-inspector-stats.h"

gboolean nautilus_inspector_stats_enabled = FALSE;
NautilusInspectorStats nautilus_inspector_stats = { 0 };

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
