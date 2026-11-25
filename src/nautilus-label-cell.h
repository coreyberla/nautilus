/*
 * SPDX-FileCopyrightText: 2022 António Fernandes <antoniof@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

/* Needed for NautilusColumn (typedef only). */
#include "nautilus-types.h"

#include "nautilus-view-cell.h"
#include "nautilus-text-cache.h"

G_BEGIN_DECLS

#define NAUTILUS_TYPE_LABEL_CELL (nautilus_label_cell_get_type())

G_DECLARE_FINAL_TYPE (NautilusLabelCell, nautilus_label_cell, NAUTILUS, LABEL_CELL, NautilusViewCell)

NautilusViewCell * nautilus_label_cell_new (NautilusListBase *view,
                                            NautilusColumn   *column);

void nautilus_label_cell_set_text_cache (NautilusLabelCell *self,
                                         NautilusTextCache *cache);

G_END_DECLS
