/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _NautilusTextCache NautilusTextCache;

NautilusTextCache *nautilus_text_cache_new        (guint              max_entries);
void               nautilus_text_cache_free       (NautilusTextCache *self);
void               nautilus_text_cache_clear      (NautilusTextCache *self);

PangoLayout       *nautilus_text_cache_get        (NautilusTextCache *self,
                                                   GtkWidget         *widget,
                                                   const char        *text,
                                                   PangoAttrList     *attrs,
                                                   int               *width,
                                                   int               *height);
PangoLayout       *nautilus_text_cache_get_ellipsized (NautilusTextCache  *self,
                                                       GtkWidget          *widget,
                                                       const char         *text,
                                                       PangoAttrList      *attrs,
                                                       PangoEllipsizeMode  ellipsize,
                                                       int                 width);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NautilusTextCache, nautilus_text_cache_free)

G_END_DECLS
