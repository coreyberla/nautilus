/*
 * SPDX-FileCopyrightText: 2026 The Nautilus Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_MOUNT_OPERATION (nautilus_mount_operation_get_type ())

G_DECLARE_FINAL_TYPE (NautilusMountOperation, nautilus_mount_operation, NAUTILUS, MOUNT_OPERATION, GtkMountOperation)

GMountOperation * nautilus_mount_operation_new (GtkWindow *parent);

G_END_DECLS
