/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file os/windows.c
 * @brief Windows platform implementation of forge_os.h -- a stub
 *        (disabled until launch).
 *
 * 2026-09-13: the old implementation (~1200 lines, ported from the
 * legacy semantics, not tested on real Windows) was deleted.
 * Reason: to avoid an implementation that "looks cross-platform but
 * does not actually run". To restore it: reimplement (the git
 * history will be squashed before launch, so the old implementation
 * will no longer be retrievable -- export a backup before the
 * squash if it must be kept) and complete the testing on real
 * Windows.
 *
 * When compiled with FORGE_OS_WINDOWS, this file provides no os_*
 * symbols: if the platform side needs them, the link fails outright
 * (an easily locatable error) -- a stronger fail-fast than an empty
 * function, with no "links but crashes at runtime" middle state.
 */
#if defined(FORGE_OS_WINDOWS)
/* stub: empty implementation (no symbols until it has been tested
   on real Windows) */
#endif