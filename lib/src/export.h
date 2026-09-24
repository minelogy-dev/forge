/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#if defined _MSC_VER
#define SYMBOL_PUBLIC _declspec(dllexport)
#else
#define SYMBOL_PUBLIC __attribute__((visibility("default")))
#endif