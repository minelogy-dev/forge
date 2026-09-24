/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#include <build.h>

/* The version has a single source of truth: the version key in
   build.conf (which plays a role similar to .env). The host forge
   injects it into this file's compilation via the
   -DCONF_version=<value> macro (the value is not wrapped in quotes;
   execv goes straight to the compiler, no shell layer). We recover
   the quoted literal here via STR stringification, falling back to
   "dev" when unconfigured; the host-tool target then forwards
   FORGE_VERSION via add_define for --version output and gen_deb
   packaging. */
#define STR_IMPL(tok) #tok
#define STR(tok) STR_IMPL(tok)
#ifndef CONF_version
#define FORGE_VERSION "dev"
#else
#define FORGE_VERSION STR(CONF_version)
#endif

function(build) {
  parse_args(builtin_args);
  target("forge") {
    set_toolchain(GCC);
    add_sources("src/forge.c");
    add_sources("src/cli.c");
    add_sources("src/parser.c");
    add_sources("src/generate.c");
    add_sources("lib/src/os");
    add_include_path("include");
    add_include_path("lib/include");
    /* add_define does not add quotes automatically (values are passed
   to the compiler as literal tokens, so numeric macros like
   add_define("PI", "3.14") work); here we wrap in quotes explicitly */
add_define("FORGE_VERSION", "\"" FORGE_VERSION "\"");
    set_visibility(HIDDEN);
    set_optimization(OPT_SPEED);
    set_standard("c23");
    set_output_path("build");
    set_output_intermediate_path("build/exe-obj");
    if (compile() != 0)
      return -1;
  }
  target("main") {
    set_toolchain(GCC);
    set_type(FORGE_STATIC_LIB);
    /* A static library must NOT carry -flto: its objects are linked by
       whichever compiler builds the consumer (e.g. clang, whose driver
       cannot process GCC-LTO IR without extra plugin plumbing). LTO
       benefits only the final executable, not archived objects. */
    set(TARGET_LTO, 1, DISABLE);
    add_sources("src/build/main.c");
    add_sources_r("lib/src");
    add_include_path("lib/include");
    add_include_path("lib/src");
    set_optimization(OPT_AGGRESSIVE);
    set_standard("c23");
    set_output_path("build");
    if (compile() != 0)
      return -1;
  }
  target("forge") {
    set_toolchain(GCC);
    set_type(FORGE_SHARED_LIB);
    add_sources_r("lib/src");
    add_include_path("lib/include");
    add_include_path("lib/src");
    set_optimization(OPT_SPEED);
    set_standard("c23");
    set_visibility(HIDDEN);
    set_output_path("build/shared");
    if (compile() != 0)
      return -1;
  }

  /* Header sync: keep build/output/include in step with lib/include so
     the host forge (<exe_dir>/include resolves first) and every build
     in this checkout are self-contained. The public headers live in a
     target: language = ALL collects every file under lib/include
     (adding a header there is all it takes — no list elsewhere to
     update), and the loop below copies them preserving the directory
     structure (headers are not assumed to be flat). No compile() here:
     the copy IS the export. */
  target("include") {
    set_type(FORGE_SOURCE);
    set_language(ALL);
    add_sources_r("lib/include");
    for (source_t *s = _target->sources; s; s = s->next) {
      /* Collected paths are absolute project-root relative; strip the
         lib/include/ prefix to reproduce the layout under the sync dir */
      char *rel = os_path_file_rel(s->file, NULL);
      const char *leaf = rel;
      if (rel && strncmp(rel, "lib/include/", 12) == 0)
        leaf = rel + 12;
      char *dd = leaf ? os_path_join("build/output/include", leaf) : NULL;
      char *parent = dd ? os_path_dirname(dd) : NULL;
      int rc = (!dd || !parent || os_mkdir_r(parent) != 0)
                   ? -1
                   : os_copy_file(s->file, dd);
      if (rc != 0) {
        free(rel);
        free(dd);
        free(parent);
        return -1;
      }
      free(rel);
      free(dd);
      free(parent);
    }
  }
  return 0;
}
set_default(build);

default_test();

/* ---------------- install / gen_deb (Linux only) ----------------
 * Effective only on Linux (guarded by the LINUX compile-time macro
 * from build.h); on Windows the function bodies are a runtime error +
 * return -1 (deliberately not #error, so that build.c still compiles
 * on Windows). */

#if LINUX

/* Recursive copy of every entry under src_dir into dst_dir (os_copy_file
   handles files only). Used for the synced build/output/include, whose
   layout is not assumed flat. */
static int copy_dir_r(const char *src_dir, const char *dst_dir) {
  if (os_mkdir_r(dst_dir) != 0)
    return -1;
  int n = 0;
  char **names = os_listdir(src_dir, &n);
  if (!names)
    return -1;
  int ret = 0;
  for (int i = 0; i < n && ret == 0; i++) {
    char *s = os_path_join(src_dir, names[i]);
    char *d = os_path_join(dst_dir, names[i]);
    if (!s || !d) {
      ret = -1;
    } else if (os_path_is_dir(s)) {
      ret = copy_dir_r(s, d);
    } else if (os_copy_file(s, d) != 0) {
      ret = -1;
    }
    free(s);
    free(d);
  }
  for (int i = 0; i < n; i++)
    free(names[i]);
  free(names);
  return ret;
}

/* Copy the artifacts to the system paths under prefix (shared by
   install and gen_deb's data root); returns 0 on success. Executables
   get +x (the copy produces 0644). */
static int install_to(const char *prefix) {
  int ret = 0;
  char *bin = os_path_join(prefix, "bin");
  /* Private lib area <prefix>/lib/forge: libmain.a / libforge.so are
     generic names — the global /usr/lib would clash with system
     packages (same reasoning as the headers below). */
  char *lib = os_path_join(prefix, "lib/forge");
  /* Headers go to <prefix>/lib/forge/include — the same private area
     (build.h is too generic to risk clashing with system packages). */
  char *inc = os_path_join(lib, "include");
  if (!bin || !lib || !inc || os_mkdir_r(bin) != 0 || os_mkdir_r(lib) != 0 ||
      os_mkdir_r(inc) != 0) {
    free(bin);
    free(lib);
    free(inc);
    return -1;
  }

  /* Host tool (with the executable bit) */
  char *dst = os_path_join(bin, "forge");
  if (os_copy_file("build/output/forge", dst) != 0)
    ret = -1;
  else {
    char *chmod_argv[] = {"chmod", "+x", dst, NULL};
    os_shell(chmod_argv); /* not fatal: ignored on failure */
  }
  free(dst);

  /* Runtime library and static library */
  dst = os_path_join(lib, "libmain.a");
  if (os_copy_file("build/output/libmain.a", dst) != 0)
    ret = -1;
  free(dst);
  dst = os_path_join(lib, "libforge.so");
  if (os_copy_file("build/shared/output/libforge.so", dst) != 0)
    ret = -1;
  free(dst);

  /* Headers: the synced build product build/output/include (install and
     gen_deb run function_build first, so the sync has happened).
     Recursive — the header layout is not assumed flat. */
  if (copy_dir_r("build/output/include", inc) != 0)
    ret = -1;

  free(bin);
  free(lib);
  free(inc);
  return ret;
}

/* uname -m -> Debian architecture name: x86_64->amd64,
   aarch64->arm64; anything else is kept as-is */
static const char *deb_arch(void) {
  static char buf[64];
  char *out = os_shell((char *[]){"uname", "-m", NULL});
  if (!out)
    return "amd64";
  size_t len = strlen(out);
  while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
    out[--len] = '\0';
  const char *m = out;
  if (strcmp(m, "x86_64") == 0)
    m = "amd64";
  else if (strcmp(m, "aarch64") == 0)
    m = "arm64";
  snprintf(buf, sizeof(buf), "%s", m);
  free(out);
  return buf;
}

/* Packaging helper: tar/ar run directly via argv (status captured);
   returns -1 on failure */
static int run_quiet(char **argv) {
  int st = -1;
  char *out =
      os_execute_capture_all_status(argv[0], argv, "", NULL, &st);
  free(out);
  return st == 0 ? 0 : -1;
}
#endif /* LINUX */

function(install) {
  parse_args();
  const char *prefix =
      (argc > 1 && argv[1] && *argv[1]) ? argv[1] : "/usr/local";
#if LINUX
  /* Ensure the artifacts (including the header sync) are fresh first */
  int r = function_build(argc, argv);
  if (r != 0)
    return r;
  r = install_to(prefix);
  if (r != 0)
    fprintf(stderr,
            "forge: install to %s failed\n"
            "  (check write permission, or pass a prefix you own, e.g. "
            "make install ~/.local)\n",
            prefix);
  return r;
#else
  (void)prefix;
  fprintf(stderr, "forge: install is only supported on Linux\n");
  return -1;
#endif
}

function(gen_deb) {
  parse_args();
#if LINUX
  /* Version: FORGE_VERSION from build.c (single source of truth),
     leading v stripped */
  const char *v = FORGE_VERSION;
  if (*v == 'v')
    v++;
  char ver[64];
  snprintf(ver, sizeof(ver), "%s", v);
  const char *arch = deb_arch();

  int ret = function_build(argc, argv); /* ensure artifacts are fresh first */
  if (ret != 0)
    return ret;

  /* Layout:
       build/deb/
         debian-binary  data/  control-dir/control
         control.tar.gz data.tar.gz  forge_<ver>_<arch>.deb */
  char *work = os_path_join("build", "deb");
  char *data = os_path_join(work, "data");
  char *cdir = os_path_join(work, "control-dir");
  if (!work || !data || !cdir) {
    free(work);
    free(data);
    free(cdir);
    return -1;
  }
  os_remove_r(work);
  if (os_mkdir_r(data) != 0 || os_mkdir_r(cdir) != 0) {
    free(work);
    free(data);
    free(cdir);
    return -1;
  }

  /* Data root = reuse install's copy logic. The deb data tree mirrors
     the filesystem root: packages install under /usr (FHS), so the
     copy root is <data>/usr — bin/forge, include/*.h, lib/*. */
  char *data_usr = os_path_join(data, "usr");
  if (!data_usr) {
    ret = -1;
    goto cleanup;
  }
  if (install_to(data_usr) != 0) {
    free(data_usr);
    free(work);
    free(data);
    free(cdir);
    return -1;
  }
  free(data_usr);

  char *deb_bin = os_path_join(work, "debian-binary");
  char *ctl = os_path_join(cdir, "control");
  char *cgz = os_path_join(work, "control.tar.gz");
  char *dgz = os_path_join(work, "data.tar.gz");
  size_t deblen = strlen(work) + strlen(ver) + strlen(arch) + 28;
  char *deb = malloc(deblen);
  if (!deb_bin || !ctl || !cgz || !dgz || !deb) {
    ret = -1;
    goto cleanup;
  }
  snprintf(deb, deblen, "%s/forge_%s_%s.deb", work, ver, arch);

  FILE *f = fopen(deb_bin, "w");
  if (f) {
    fputs("2.0\n", f);
    fclose(f);
  } else {
    ret = -1;
  }
  f = fopen(ctl, "w");
  if (f) {
    fprintf(f,
            "Package: forge\n"
            "Version: %s\n"
            "Section: devel\n"
            "Priority: optional\n"
            "Architecture: %s\n"
            "Maintainer: Forge Developers\n"
            "Description: Forge - a build system for C projects; build "
            "system\n",
            ver, arch);
    fclose(f);
  } else {
    ret = -1;
  }

  if (ret == 0) {
    char *t1[] = {"tar", "-czf", cgz, "-C", cdir, "control", NULL};
    char *t2[] = {"tar", "-czf", dgz, "-C", data, ".", NULL};
    char *ar[] = {"ar", "rcs", deb, deb_bin, cgz, dgz, NULL};
    if (run_quiet(t1) != 0 || run_quiet(t2) != 0 || run_quiet(ar) != 0)
      ret = -1;
    else
      printf("forge: %s\n", deb);
  }

cleanup:
  free(deb_bin);
  free(ctl);
  free(cgz);
  free(dgz);
  free(deb);
  free(work);
  free(data);
  free(cdir);
  return ret;
#else
  fprintf(stderr, "forge: gen_deb is only supported on Linux\n");
  return -1;
#endif
}