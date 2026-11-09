/*
 * SPDX-FileCopyrightText: 2022 António Fernandes <antoniof@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

/* Needed for NautilusColumn (typedef only). */
#include "nautilus-types.h"

#include "nautilus-view-cell.h"

G_BEGIN_DECLS

#define NAUTILUS_TYPE_LABEL_CELL (nautilus_label_cell_get_type())

G_DECLARE_FINAL_TYPE (NautilusLabelCell, nautilus_label_cell, NAUTILUS, LABEL_CELL, NautilusViewCell)

typedef struct _NautilusColumnWidth NautilusColumnWidth;

NautilusColumnWidth * nautilus_column_width_new   (void);
void                  nautilus_column_width_free  (NautilusColumnWidth *self);
void                  nautilus_column_width_reset (NautilusColumnWidth *self);
void                  nautilus_column_width_set_visible (NautilusColumnWidth *self,
                                                         gboolean             visible);

NautilusViewCell * nautilus_label_cell_new (NautilusListBase    *view,
                                            NautilusColumn      *column,
                                            NautilusColumnWidth *column_width);

G_END_DECLS
