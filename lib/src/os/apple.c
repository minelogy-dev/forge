/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file os/apple.c
 * @brief Apple (macOS) platform implementation of forge_os.h -- a
 *        stub (disabled until launch).
 *
 * 2026-09-13: the old implementation (the Apple variants of
 * os_exe_dir / os_now_ms / os_mtime_ms / os_export_flag) had not
 * been tested on real macOS and was deleted. Reason: to avoid an
 * implementation that "looks cross-platform but does not actually
 * run". To restore it: reimplement (the git history will be squashed
 * before launch, so the old implementation will no longer be
 * retrievable -- export a backup before the squash if it must be
 * kept) and complete the testing on real macOS.
 *
 * When compiled with __APPLE__, this file provides no symbols: a
 * link-time fail-fast.
 */
#if defined(__APPLE__) || defined(__MACH__)
/* stub: empty implementation */
#endif