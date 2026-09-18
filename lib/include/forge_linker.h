/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#ifndef FORGE_LINKER
#define FORGE_LINKER

#include "forge_type.h"

/* Linker type values uniformly come from forge_def.h (LD / LLD etc.; the
   clang chain uses CLANG). No separate enum is defined here (the dead
   LD_LD/LD_CLANG value ranges were removed). */
/* Link command generation (dispatch and backends; the implementation lives
   in lib/src/linker.c and lib/src/linker/{gcc,clang}.c). When the backend's
   source_type is FORGE_OBJ, a link command is generated (currently reusing
   the compiler's link branch). */
strv_t *linker(const forge_context_t context);
strv_t *gcc_linker(const forge_context_t context);
strv_t *clang_linker(const forge_context_t context);

struct linker_args_t {
    toolchain_t *toolchain;      /**< pointer to the parent toolchain; used to
                                      fetch the target's -L/-l */

    strv_t lib_search_paths;      /**< extra library search paths (appended to
                                       target->lib_dirs) */
    strv_t libraries;            /**< names of libraries to link (appended to
                                      target->libs) */

    int lto_enabled;
    int shared_enabled;          /**< build a shared library (.so/.dll) */
    int static_crt;              /**< static-link C runtime (/MT or -static) */
    int strip_symbols;           /**< strip symbols after linking */
                               /**< (-s or /PDBSTRIPPED) */
    int gc_sections;             /**< remove unused sections */
                               /**< (-Wl,--gc-sections or /OPT:REF) */
    char *map_file;              /**< generate a map file */
                               /**< (-Wl,-Map=xxx.map or /MAP) */

    strv_t export_symbol;

    strv_t options;              /**< extra user-defined link options */
};

#endif