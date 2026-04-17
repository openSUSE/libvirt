/*
 * vsh-table.c: table printing helper
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

#include <config.h>
#include "vsh-table.h"

#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>

#include "viralloc.h"
#include "virbuffer.h"

#define HEX_ENCODE_LENGTH 4 /* represents length of '\xNN' */

typedef struct _vshTableRow vshTableRow;
struct _vshTableRow {
    char **cells;
    unsigned int *flags;
    size_t ncells;
};


struct _vshTable {
    vshTableRow **rows;
    size_t nrows;
};


static void
vshTableRowFree(vshTableRow *row)
{
    size_t i;

    if (!row)
        return;

    for (i = 0; i < row->ncells; i++)
        g_free(row->cells[i]);

    g_free(row->cells);
    g_free(row->flags);
    g_free(row);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(vshTableRow, vshTableRowFree);


void
vshTableFree(vshTable *table)
{
    size_t i;

    if (!table)
        return;

    for (i = 0; i < table->nrows; i++)
        vshTableRowFree(table->rows[i]);
    g_free(table->rows);
    g_free(table);
}


/**
 * vshTableRowNew:
 * @arg: the first argument.
 * @ap: list of variadic arguments
 *
 * Create a new row in the table. Each argument passed
 * represents a cell in the row.
 *
 * Return: pointer to vshTableRow *row or NULL.
 */
static vshTableRow *
vshTableRowNew(const char *arg, va_list ap)
{
    vshTableRow *row = NULL;
    size_t nflags = 0;

    if (!arg) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Table row cannot be empty"));
        return NULL;
    }

    row = g_new0(vshTableRow, 1);

    while (arg) {
        g_autofree char *tmp = NULL;
        unsigned int fn = VSH_TABLE_CELL_DEFAULT;

        tmp = g_strdup(arg);

        VIR_APPEND_ELEMENT(row->cells, row->ncells, tmp);
        VIR_APPEND_ELEMENT(row->flags, nflags, fn);

        arg = va_arg(ap, const char *);
    }

    return row;
}


/**
 * vshTableNew:
 * @arg: List of column names (NULL terminated)
 *
 * Create a new table.
 *
 * Returns: pointer to table or NULL.
 */
vshTable *
vshTableNew(const char *arg, ...)
{
    g_autoptr(vshTable) table = NULL;
    g_autoptr(vshTableRow) header = NULL;
    va_list ap;

    table = g_new0(vshTable, 1);

    va_start(ap, arg);
    header = vshTableRowNew(arg, ap);
    va_end(ap);

    if (!header)
        return NULL;

    VIR_APPEND_ELEMENT(table->rows, table->nrows, header);

    return g_steal_pointer(&table);
}


/**
 * vshTableRowAppend:
 * @table: table to append to
 * @arg: cells of the row (NULL terminated)
 *
 * Append new row into the @table. The number of cells in the row has
 * to be equal to the number of cells in the table header.
 *
 * Returns: 0 if succeeded, -1 if failed.
 */
int
vshTableRowAppend(vshTable *table, const char *arg, ...)
{
    g_autoptr(vshTableRow) row = NULL;
    size_t ncolumns = table->rows[0]->ncells;
    va_list ap;

    va_start(ap, arg);
    row = vshTableRowNew(arg, ap);
    va_end(ap);

    if (!row)
        return -1;

    if (ncolumns != row->ncells) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Incorrect number of cells in a table row"));
        return -1;
    }

    VIR_APPEND_ELEMENT(table->rows, table->nrows, row);

    return 0;
}


/**
 * vshTableRowAppendFlags:
 * @table: table to append to
 * @flags: start of the cell tuples of the row row (NULL terminated)
 *
 * Append new row into the @table. The number of cells in the row has
 * to be equal to the number of cells in the table header. Each column consists
 * of a tuple of an 'unsigned int' corresponding to bitwise-or of
 * vshTableCellFlags and an const char * representing the value to add.
 *
 * Returns: 0 if succeeded, -1 if failed.
 */
int
vshTableRowAppendFlags(vshTable *table,
                       unsigned int f1,
                       const char *c1,
                       ...)
{
    g_autoptr(vshTableRow) row = g_new0(vshTableRow, 1);
    size_t ncolumns = table->rows[0]->ncells;
    va_list ap;
    unsigned int fn = f1;
    g_autofree char *cn = g_strdup(c1);
    size_t nflags = 0;

    va_start(ap, c1);
    while (cn) {
        VIR_APPEND_ELEMENT(row->flags, nflags, fn);
        VIR_APPEND_ELEMENT(row->cells, row->ncells, cn);

        fn = va_arg(ap, unsigned int);
        cn = g_strdup(va_arg(ap, const char *));
    }
    va_end(ap);

    if (ncolumns != row->ncells) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Incorrect number of cells in a table row"));
        return -1;
    }

    VIR_APPEND_ELEMENT(table->rows, table->nrows, row);

    return 0;
}

/**
 * Function pulled from util-linux
 *
 * Function's name in util-linux: mbs_safe_encode_to_buffer
 *
 * Returns allocated string where all control and non-printable chars are
 * replaced with \x?? hex sequence, or NULL.
 */
static char *
vshTableSafeEncode(const char *s, size_t *width)
{
    const char *p = s;
    size_t sz = s ? strlen(s) : 0;
    char *buf;
    char *ret;
    mbstate_t st = { 0 };

    buf = g_new0(char, (sz * HEX_ENCODE_LENGTH) + 1);

    ret = buf;
    *width = 0;

    while (p && *p) {
        if ((*p == '\\' && *(p + 1) == 'x') ||
            g_ascii_iscntrl(*p)) {
            g_snprintf(buf, HEX_ENCODE_LENGTH + 1, "\\x%02x", *p);
            buf += HEX_ENCODE_LENGTH;
            *width += HEX_ENCODE_LENGTH;
            p++;
        } else {
            wchar_t wc;
            size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &st);

            if (len == 0)
                break;		/* end of string */

            if (len == (size_t) -1 || len == (size_t) -2) {
                len = 1;
                /*
                 * Not valid multibyte sequence -- maybe it's
                 * printable char according to the current locales.
                 */
                if (!g_ascii_isprint(*p)) {
                    g_snprintf(buf, HEX_ENCODE_LENGTH + 1, "\\x%02x", *p);
                    buf += HEX_ENCODE_LENGTH;
                    *width += HEX_ENCODE_LENGTH;
                } else {
                    *buf++ = *p;
                    (*width)++;
                }
            } else if (!iswprint(wc)) {
                size_t i;
                for (i = 0; i < len; i++) {
                    g_snprintf(buf, HEX_ENCODE_LENGTH + 1, "\\x%02x", p[i]);
                    buf += HEX_ENCODE_LENGTH;
                    *width += HEX_ENCODE_LENGTH;
                }
            } else {
                memcpy(buf, p, len);
                buf += len;
                *width += g_unichar_iszerowidth(wc) ? 0 : (g_unichar_iswide(wc) ? 2 : 1);
            }
            p += len;
        }
    }

    *buf = '\0';
    return ret;
}


/**
 * vshTableGetColumnsWidths:
 * @table: table
 * @maxwidths: maximum count of characters for each columns
 * @widths: count of characters for each cell in the table
 *
 * Fill passed @maxwidths and @widths arrays with maximum number
 * of characters for columns and number of character per each
 * table cell, respectively. Both lengths include the extra whitespace for
 * separation of columns.
 * Handle unicode strings (user must have multibyte locale)
 *
 * Return 0 in case of success, -1 otherwise.
 */
static int
vshTableGetColumnsWidths(vshTable *table,
                         size_t *maxwidths,
                         size_t **widths,
                         bool header)
{
    size_t i;

    i = header? 0 : 1;
    for (; i < table->nrows; i++) {
        vshTableRow *row = table->rows[i];
        size_t j;

        for (j = 0; j < row->ncells; j++) {
            size_t size = 0;
            /* need to replace nonprintable and control characters,
             * because width of some of those characters (e.g. \t, \v, \b ...)
             * cannot be counted properly */
            char *tmp = vshTableSafeEncode(row->cells[j], &size);
            if (!tmp)
                return -1;

            /* include the built-in whitespace in the calculated length */
            if (!(row->flags[j] & VSH_TABLE_CELL_SKIP_LEADING))
                size += 1;

            if (!(row->flags[j] & VSH_TABLE_CELL_SKIP_TRAILING))
                size += 2;

            VIR_FREE(row->cells[j]);
            row->cells[j] = tmp;
            widths[i][j] = size;

            if (widths[i][j] > maxwidths[j])
                maxwidths[j] = widths[i][j];
        }
    }

    return 0;
}


/**
 * vshTableRowPrint:
 * @row: table to append to
 * @maxwidths: maximum count of characters for each columns
 * @widths: count of character for each cell in this row
 * @buf: buffer to store table (only if @toStdout == true)
 */
static void
vshTableRowPrint(vshTableRow *row,
                 size_t *maxwidths,
                 size_t *widths,
                 virBuffer *buf)
{
    size_t i;
    size_t j;

    for (i = 0; i < row->ncells; i++) {
        if (!(row->flags[i] & VSH_TABLE_CELL_SKIP_LEADING))
            virBufferAddLit(buf, " ");

        if (row->flags[i] & VSH_TABLE_CELL_ALIGN_RIGHT) {
            for (j = 0; j < maxwidths[i] - widths[i]; j++)
                virBufferAddChar(buf, ' ');
            virBufferAsprintf(buf, "%s", row->cells[i]);
        } else {
            virBufferAsprintf(buf, "%s", row->cells[i]);

            if (i < (row->ncells - 1)) {
                for (j = 0; j < maxwidths[i] - widths[i]; j++)
                    virBufferAddChar(buf, ' ');
            }
        }

        if (!(row->flags[i] & VSH_TABLE_CELL_SKIP_TRAILING) &&
            i < (row->ncells - 1))
                virBufferAddLit(buf, "  ");
    }
    virBufferAddChar(buf, '\n');
}


/**
 * vshTablePrint:
 * @table: table to print
 * @header: whetever to print to header (true) or not (false)
 * this argument is relevant only if @ctl == NULL
 *
 * Get table. To get an alignment of columns right, function
 * fills 2d array @widths with count of characters in each cell and
 * array @maxwidths maximum count of character in each column.
 * Function then prints tables header and content.
 *
 * Return string containing table, or NULL
 */
static char *
vshTablePrint(vshTable *table, bool header)
{
    size_t i;
    size_t j;
    g_autofree size_t *maxwidths = NULL;
    size_t **widths;
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;
    char *ret = NULL;

    maxwidths = g_new0(size_t, table->rows[0]->ncells);

    widths = g_new0(size_t *, table->nrows);

    /* retrieve widths of columns */
    for (i = 0; i < table->nrows; i++)
        widths[i] = g_new0(size_t, table->rows[0]->ncells);

    if (vshTableGetColumnsWidths(table, maxwidths, widths, header) < 0)
        goto cleanup;

    if (header) {
        /* print header */
        vshTableRowPrint(table->rows[0], maxwidths, widths[0], &buf);

        /* print dividing line  */
        for (i = 0; i < table->rows[0]->ncells; i++) {
            for (j = 0; j < maxwidths[i]; j++)
                virBufferAddChar(&buf, '-');
        }
        virBufferAddChar(&buf, '\n');
    }
    /* print content */
    for (i = 1; i < table->nrows; i++)
        vshTableRowPrint(table->rows[i], maxwidths, widths[i], &buf);

    ret = virBufferContentAndReset(&buf);

 cleanup:
    for (i = 0; i < table->nrows; i++)
        VIR_FREE(widths[i]);
    VIR_FREE(widths);
    return ret;
}


/**
 * vshTablePrintToStdout:
 * @table: table to print
 * @ctl virtshell control structure
 *
 * Print table returned in string to stdout.
 * If effect on vshControl structure on printing function changes in future
 * (apart from quiet mode) this code may need update
 */
void
vshTablePrintToStdout(vshTable *table, vshControl *ctl)
{
    bool header;
    g_autofree char *out = NULL;

    header = ctl ? !ctl->quiet : true;

    out = vshTablePrintToString(table, header);
    if (out)
        vshPrint(ctl, "%s", out);
}


/**
 * vshTablePrintToString:
 * @table: table to print
 * @header: whetever to print to header (true) or not (false)
 *
 * Return string containing table, or NULL if table was printed to
 * stdout. User will have to free returned string.
 */
char *
vshTablePrintToString(vshTable *table, bool header)
{
    return vshTablePrint(table, header);
}
