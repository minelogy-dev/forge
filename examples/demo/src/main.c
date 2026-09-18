/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#include <hello.h>
#include <stdio.h>

const char *greeting(void) { return "hello from forge demo"; }

int main(void) {
  puts(greeting());
  return 0;
}