/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/* Example project: identical, verbatim, to the "Build script
   example" in the README */

#include <build.h>
function(build) {
  parse_args(builtin_args);
  target("app") {
    set_toolchain(GCC);
    add_sources_r("src");
    add_include_path("include");
    set_optimization(OPT_SPEED);
    if (compile() != 0)
      return -1;
  }
  return 0;
}
set_default(build);
default_test();
