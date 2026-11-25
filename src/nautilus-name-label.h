/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "nautilus-text-cache.h"

G_BEGIN_DECLS

#define NAUTILUS_TYPE_NAME_LABEL (nautilus_name_label_get_type ())

G_DECLARE_FINAL_TYPE (NautilusNameLabel, nautilus_name_label, NAUTILUS, NAME_LABEL, GtkWidget)

GtkWidget  *nautilus_name_label_new                 (void);
const char *nautilus_name_label_get_text            (NautilusNameLabel  *self);
void        nautilus_name_label_set_text            (NautilusNameLabel  *self,
                                                     const char         *text);
void        nautilus_name_label_set_ellipsize       (NautilusNameLabel  *self,
                                                     PangoEllipsizeMode  ellipsize);
void        nautilus_name_label_set_xalign          (NautilusNameLabel  *self,
                                                     float               xalign);
void        nautilus_name_label_set_tabular_numbers (NautilusNameLabel  *self,
                                                     gboolean            tabular_numbers);
void        nautilus_name_label_set_cache           (NautilusNameLabel  *self,
                                                     NautilusTextCache  *cache);
gboolean    nautilus_name_label_is_ellipsized       (NautilusNameLabel  *self);

G_END_DECLS
