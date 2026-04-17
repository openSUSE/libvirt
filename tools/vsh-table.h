/*
 * vsh-table.h: table printing helper
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "vsh.h"

typedef struct _vshTable vshTable;

typedef enum {
    VSH_TABLE_CELL_DEFAULT = 0,
    VSH_TABLE_CELL_SKIP_LEADING = 1 << 0,
    VSH_TABLE_CELL_SKIP_TRAILING = 1 << 1,
    VSH_TABLE_CELL_ALIGN_RIGHT = 1 << 2,
} vshTableCellFlags;

void
vshTableFree(vshTable *table);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(vshTable, vshTableFree);

vshTable *
vshTableNew(const char *format, ...)
    G_GNUC_NULL_TERMINATED;

int
vshTableRowAppend(vshTable *table, const char *arg, ...)
    G_GNUC_NULL_TERMINATED;

int
vshTableRowAppendFlags(vshTable *table,
                       unsigned int f1,
                       const char *c1,
                       ...)
    G_GNUC_NULL_TERMINATED;

void
vshTablePrintToStdout(vshTable *table, vshControl *ctl);

char *
vshTablePrintToString(vshTable *table, bool header);
