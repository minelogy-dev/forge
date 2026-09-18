/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#ifndef FORGE_ARCHIVER
#define FORGE_ARCHIVER

#include "forge_type.h"

struct archiver_args_t {
    toolchain_t *toolchain;      /**< pointer to the parent toolchain */

    int deterministic;           /**< deterministic timestamps (-D, /BREPRO) */
    int verbose;                 /**< verbose output (-v or /VERBOSE) */

    strv_t options;              /**< extra user-defined archiver options */
};

/* Archiver command generation (dispatch and backends; the implementation
   lives in lib/src/archiver.c and lib/src/archiver/{ar,llvm-ar}.c). */
strv_t *archiver(const forge_context_t context);
strv_t *ar_archiver(const forge_context_t context);
strv_t *llvm_ar_archiver(const forge_context_t context);

#endif