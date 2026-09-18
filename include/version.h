/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#ifndef FORGE_VERSION_H
#define FORGE_VERSION_H

/* The single source of the version number is FORGE_VERSION from the
   build script build.c, passed via the host target's add_define(-D)
   into the translation unit that includes this header (forge.c and
   cli.c show it via show_version). This header no longer defines the
   version string. */

#define PROGRAM_NAME "forge"
#define PACKAGE_NAME "Forge"
#define COPYRIGHT_YEAR "2026"
#define AUTHOR "The Forge development team"

#endif /* FORGE_VERSION_H */