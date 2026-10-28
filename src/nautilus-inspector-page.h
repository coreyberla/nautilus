/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_INSPECTOR_PAGE (nautilus_inspector_page_get_type ())

G_DECLARE_FINAL_TYPE (NautilusInspectorPage, nautilus_inspector_page, NAUTILUS, INSPECTOR_PAGE, AdwBin)

void
nautilus_inspector_page_register (void);

G_END_DECLS
