/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "nautilus-inspector-stats"

#include "nautilus-inspector-stats.h"

gboolean nautilus_inspector_stats_enabled = FALSE;

void
nautilus_inspector_stats_set_shown (gboolean shown)
{
    nautilus_inspector_stats_enabled = shown;
}
