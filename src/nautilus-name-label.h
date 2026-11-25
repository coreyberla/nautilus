/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_NAME_LABEL (nautilus_name_label_get_type ())

G_DECLARE_FINAL_TYPE (NautilusNameLabel, nautilus_name_label, NAUTILUS, NAME_LABEL, GtkWidget)

const char *nautilus_name_label_get_text      (NautilusNameLabel *self);
void        nautilus_name_label_set_text      (NautilusNameLabel *self,
                                               const char        *text);
gboolean    nautilus_name_label_is_ellipsized (NautilusNameLabel *self);

G_END_DECLS
