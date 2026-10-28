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

/* Durations are in µs. */
typedef struct
{
    guint n_calls;
    gint64 total_time;
    gint64 max_time;
} NautilusTimingStats;

typedef struct
{
    /* nautilus_directory_notify_files_*() */
    guint64 files_added;
    guint64 files_changed;
    guint64 files_removed;
    guint64 files_moved;

    /* The thumbnailer is shared by all views, which share its results. */
    guint64 thumbnails_generated;
    NautilusTimingStats thumbnail_generation;
    NautilusTimingStats thumbnail_wait;
    guint64 thumbnails_failed;

    guint64 deep_count_files_scanned;
    NautilusTimingStats deep_counts;
} NautilusInspectorStats;

#ifdef ENABLE_INSPECTOR_PAGE

extern gboolean nautilus_inspector_stats_enabled;
extern NautilusInspectorStats nautilus_inspector_stats;

#define NAUTILUS_INSPECTOR_STATS_ENABLED() G_UNLIKELY (nautilus_inspector_stats_enabled)

#define NAUTILUS_INSPECTOR_STATS_ADD(field, n) \
        G_STMT_START { \
            if (NAUTILUS_INSPECTOR_STATS_ENABLED ()) \
            { \
                nautilus_inspector_stats.field += (n); \
            } \
        } G_STMT_END

/* Only call with a duration measured while the inspector was shown. */
#define NAUTILUS_INSPECTOR_STATS_ADD_TIMING(field, duration) \
        nautilus_inspector_stats_add_timing (&nautilus_inspector_stats.field, (duration))

void nautilus_inspector_stats_set_shown (gboolean shown);

void nautilus_inspector_stats_add_timing (NautilusTimingStats *timing,
                                          gint64               duration);

#else

#define NAUTILUS_INSPECTOR_STATS_ENABLED() FALSE
/* The arguments are still type-checked, but never evaluated. */
#define NAUTILUS_INSPECTOR_STATS_ADD(field, n) G_STMT_START { if (0) { (void) (n); } } G_STMT_END
#define NAUTILUS_INSPECTOR_STATS_ADD_TIMING(field, duration) G_STMT_START { if (0) { (void) (duration); } } G_STMT_END

#endif

G_END_DECLS
