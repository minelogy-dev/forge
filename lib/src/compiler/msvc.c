/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file compiler/msvc.c
 * @brief msvc_compiler() stub: the MSVC backend has not been tested
 *        on Windows, so it stays disabled until launch.
 *
 * 2026-09-13: the old implementation (ported from the legacy
 * semantics, self-annotated "to be reviewed after Windows testing")
 * was deleted -- to avoid an implementation that "looks runnable but
 * is unverified". To restore it: reimplement (the git history will
 * be squashed before launch, so the old implementation will no
 * longer be retrievable -- export a backup before the squash if it
 * must be kept) and complete the testing on real Windows.
 *
 * Current behavior: msvc_compiler() always returns NULL. When the
 * compiler() dispatcher routes to MSVC, the command-generation
 * failure path produces an explicit error (a failed compile, not
 * silence), so build.conf must not set compiler=MSVC before launch
 * (README: "not supported yet"). The link branch is likewise no
 * longer provided.
 */
#include "forge_compiler.h"

strv_t *msvc_compiler(const forge_context_t context) {
  (void)context;
  return NULL; /* stub: untested, must not be used */
}