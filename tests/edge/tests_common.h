#ifndef TESTS_COMMON_H
#define TESTS_COMMON_H

#include <build.h>
#include <forge_os.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* S2a test common infrastructure: isolated temporary project
   directories + forge/make subprocess wrappers. All artifacts land
   under the gitignored test/edge_* trees, per the isolation rule. */

/* Create (or clear and re-create) the test/edge_<name>/ project
   directory; returns a malloc'd absolute path, or NULL on failure */
static char *edge_dir(const char *name) {
  char rel[256];
  snprintf(rel, sizeof(rel), "test/edge_%s", name);
  os_remove_r(rel); /* clear leftovers */
  if (os_mkdir_r(rel) != 0)
    return NULL;
  return os_path_abs(rel);
}

/* Write file under dir (creating parent directories as needed);
   0 on success */
static int edge_write(const char *dir, const char *file, const char *content) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s", dir, file);
  char *parent = os_path_dirname(path);
  int ok = 0;
  if (parent) {
    if (os_mkdir_r(parent) == 0) {
      FILE *f = fopen(path, "w");
      if (f) {
        fputs(content, f);
        fclose(f);
        ok = 1;
      }
    }
    free(parent);
  }
  return ok ? 0 : -1;
}

/* Default build.c template: a single function(build) + set_default.
   An optional version print is available (see
   CONFIG_BUILD_C_WITH_VERSION). */
static const char *edge_default_build_c(void) {
  return "/* cfg */\n"
         "#include <build.h>\n"
         "function(build) { return 0; }\n"
         "set_default(build);\n";
}

/* Default build.conf (pass NULL to substitute your own content) */
static const char *edge_default_conf = "compiler=GCC\n";

/* Initialize a template project; a NULL conf means the default */
static int edge_init(const char *name, const char *conf) {
  char *dir = edge_dir(name);
  if (!dir)
    return -1;
  int r = edge_write(dir, "build.c", edge_default_build_c());
  if (r == 0 && conf)
    r = edge_write(dir, "build.conf", conf);
  free(dir);
  return r;
}

/* Run argv (when workdir is non-NULL, chdir there first and restore
   afterwards - ./make artifacts are relative to cwd, so it must run
   from the project root); returns the exit code, merged output into
   out. FORGE_PREFIX points at bootstrap-prefix. */
static int edge_run(char **argv, char *out, size_t outsz, const char *workdir) {
  static char prefix[512];
  static int prefix_once = 0;
  if (!prefix_once) {
    char *abs = os_path_abs(NULL);
    if (!abs)
      return -1;
    snprintf(prefix, sizeof(prefix), "FORGE_PREFIX=%s/build/output",
             abs);
    free(abs);
    prefix_once = 1;
  }
  putenv(prefix);
  /* Environment self-check: forge's resolve candidates depend on cwd
     (exe-dir derivation) and FORGE_PREFIX. Print both once per binary
     so a missing/mis-targeted prefix is visible instead of a 3x FAIL. */
  static int env_checked = 0;
  if (!env_checked) {
    char *cwd = os_path_abs(NULL);
    fprintf(stderr, "[edge] cwd=%s FORGE_PREFIX=%s\n", cwd ? cwd : "?",
            getenv("FORGE_PREFIX") ? getenv("FORGE_PREFIX") : "(unset)");
    free(cwd);
    env_checked = 1;
  }
  int st;
  /* workdir goes through os_execute_*'s native spawn file action
     (chdir happens at child spawn, never touching our cwd), so no
     chdir/restore hack is needed */
  char *cap = os_execute_capture_all_status(argv[0], argv, "", workdir, &st);
  if (cap) {
    if (out && outsz) {
      strncpy(out, cap, outsz - 1);
      out[outsz - 1] = '\0';
    }
    free(cap);
  } else if (out && outsz) {
    out[0] = '\0';
  }
  return st;
}

/* Run the host forge on dir; buffer output in outsz; return the exit
   code */
static int edge_forge(const char *dir, char *out, size_t outsz) {
  char *args[] = {"build/output/forge", (char *)dir, NULL};
  return edge_run(args, out, outsz, NULL);
}

/* Run ./make [arg] under dir (cwd switches to dir so artifact paths
   stay relative) */
static int edge_make(const char *dir, const char *arg, char *out, size_t outsz) {
  int st;
  /* execvp("./make") after chdir(dir): must be a relative path -
     "make" is not on PATH */
  if (arg && *arg && strcmp(arg, "make") != 0) {
    char *args2[3] = {(char *)"./make", (char *)arg, NULL};
    st = edge_run(args2, out, outsz, dir);
  } else {
    char *args3[2] = {(char *)"./make", NULL};
    st = edge_run(args3, out, outsz, dir);
  }
  if (st != 0) {
    fprintf(stderr, "[edge_make %s] exit=%d:\n%s\n", dir, st,
            out && *out ? out : "(no output captured)");
  }
  return st;
}

/* Whether the host forge is available (SKIP when bootstrap-prefix is
   missing) */
static int edge_available(void) {
  char *abs = os_path_abs(NULL);
  char path[512];
  snprintf(path, sizeof(path), "%s/build/output/forge", abs ? abs : ".");
  free(abs);
  if (!os_path_is_file(path))
    return 0;
  return 1;
}
#endif /* TESTS_COMMON_H */
