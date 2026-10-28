/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <config.h>
#include <glib.h>

G_BEGIN_DECLS

/* Statistics for the GTK Inspector page are only collected while the
 * inspector is shown, and never when built without -Dinspector-page=true.
 *
 * Timers should only be started when NAUTILUS_INSPECTOR_STATS_ENABLED() is
 * true. When built without the page, it is a constant FALSE and the recording
 * functions below are empty, so the compiler drops all of it. */

#ifdef ENABLE_INSPECTOR_PAGE

extern gboolean nautilus_inspector_stats_enabled;

#define NAUTILUS_INSPECTOR_STATS_ENABLED() G_UNLIKELY (nautilus_inspector_stats_enabled)

void nautilus_inspector_stats_set_shown (gboolean shown);

#else

#define NAUTILUS_INSPECTOR_STATS_ENABLED() FALSE

#endif

G_END_DECLS
