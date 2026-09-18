/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#include "cli.h"

/* The version number is passed in with -D by the build script build.c
   (FORGE_VERSION); when it is undefined (e.g. a hand-compiled host),
   fall back to the default string. */
#ifndef FORGE_VERSION
#define FORGE_VERSION "v0.0.1"
#endif

void show_version(void) {
    printf("%s version %s\n", PROGRAM_NAME, FORGE_VERSION);
    printf("Copyright (C) %s %s\n", COPYRIGHT_YEAR, AUTHOR);
    printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n"
           "This is free software: you are free to change and redistribute it.\n"
           "There is NO WARRANTY, to the extent permitted by law.\n");
    printf("Written by %s.\n", AUTHOR);
}