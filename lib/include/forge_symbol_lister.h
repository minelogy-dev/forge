/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#ifndef FORGE_SYMBOL_LISTER
#define FORGE_SYMBOL_LISTER

#include "forge_type.h"

struct symbol_lister_args_t {
    toolchain_t *toolchain;      /**< pointer to the parent toolchain */

    int demangle;                /**< demangle C++ names (-C; no direct */
                                 /**< dumpbin equivalent) */
    int numeric_sort;            /**< sort by address (-n; default dumpbin */
                                 /**< sorting by symbol name) */
    int undefined_only;          /**< show only undefined symbols (-u; not */
                                 /**< supported by dumpbin) */
    int dynamic;                 /**< show the dynamic symbol table (-D; */
                                 /**< uses /EXPORTS) */
    char *format;                /**< output format (e.g. "sysv"/"posix"; */
                                 /**< not controllable with dumpbin) */

    strv_t options;              /**< extra user-defined options */
};

#endif