/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "parser.h"
#include "generate.h"
#include "forge_os.h"

char *program_name;

/* The host tool links only the platform implementations under
   lib/src/os/, yet unix.o references the runtime globals
   os_verbose / os_jobs, so they are defined here to satisfy the link. */
FORGE_TLS int os_verbose = 0;
int os_jobs = 1;

typedef struct {
  const char *long_name;
  int takes_arg;
  int flag_store;
  char *argv;
} opt_t;

constexpr int opts_count = 256;
opt_t opts[opts_count] = {
    ['V'] = {"version", 0, 0, NULL},
    ['h'] = {"help", 0, 0, NULL},
};

void usage(char *program) {
  fprintf(stderr, "Usage: %s <path>\n", program);
  exit(0);
}

int forge_parse_args(int argc, char *argv[], opt_t *opts, int opts_count) {
  int i = 1;
  for (; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == '-') { // long
        if (argv[i][2] != '\0') {
          char *eq = strchr(argv[i], '=');
          size_t name_len = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);
          char *value = eq ? eq + 1 : NULL;
          if (eq)
            *eq = '\0';
          int match_idx = -1;
          int match_count = 0;
          char *name = argv[i] + 2;

          for (int j = 0; j < opts_count; j++) {
            if (opts[j].long_name == NULL)
              continue;
            if (strcmp(opts[j].long_name, name) == 0) {
              match_idx = j;
              match_count = 1;
              break;
            }
            if (strncmp(opts[j].long_name, name, strlen(name)) == 0) {
              match_count++;
              match_idx = j;
            }
          }
          if (match_count == 0) {
            fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], argv[i]);
            exit(EXIT_FAILURE);
          }
          if (match_count > 1) {
            fprintf(stderr, "%s: option '%s' is ambiguous\n", argv[0], argv[i]);
            exit(EXIT_FAILURE);
          }

          opt_t *target = &opts[match_idx];
          if (target->takes_arg) {
            if (value) {
              target->argv = value; // --output=file
            } else if (i + 1 < argc && argv[i + 1][0] != '-') {
              target->argv = argv[++i]; // --output file
            } else {
              fprintf(stderr, "%s: option '--%s' requires an argument\n",
                      argv[0], target->long_name);
              exit(EXIT_FAILURE);
            }
          } else {
            if (value) {
              fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n",
                      argv[0], target->long_name);
              exit(EXIT_FAILURE);
            }
            target->flag_store++;
          }
          continue;
        } else {
          i++;
          break;
        }
      } else if (argv[i][1] != '\0') { // short
        if (opts[(unsigned char)argv[i][1]].takes_arg) {
          if (argv[i][2] != '\0')
            opts[(unsigned char)argv[i][1]].argv = argv[i] + 2;
          else if (i + 1 < argc) {
            i++;
            opts[(unsigned char)argv[i - 1][1]].argv = argv[i];
          } else {
            fprintf(stderr, "%s: option requires an argument -- '%c'\n",
                    argv[0], argv[i][1]);
            exit(EXIT_FAILURE);
          }
          continue;
        }
        for (int j = 1; argv[i][j] != '\0'; j++) {
          opt_t *o = &opts[(unsigned char)argv[i][j]];
          /* Unregistered option: the options table is zero-initialized,
             so a NULL long_name must be checked explicitly. */
          if (o->long_name == NULL) {
            fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0],
                    argv[i][j]);
            exit(EXIT_FAILURE);
          }
          if (o->takes_arg) {
            fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0],
                    argv[i][j]);
            exit(EXIT_FAILURE);
          }
          o->flag_store++;
        }
      }
    } else {
      break;
    }
  }
  return i;
}

int forge_main(char *target);

int main(int argc, char *argv[]) {
  program_name = argv[0];
  if (argc == 1)
    usage(argv[0]);
  int idx = forge_parse_args(argc, argv, opts, opts_count);

  if (opts['h'].flag_store > 0)
    usage(argv[0]);

  if (opts['V'].flag_store > 0) {
    show_version();
    return 0;
  }
  int ret;
  for (; idx < argc; idx++) {
    ret = forge_main(argv[idx]);
    if (ret != 0)
      return ret;
  }
  return 0;
}

static char *path_join(const char *dir, const char *name) {
  size_t d = strlen(dir);
  int need_sep = d > 0 && dir[d - 1] != '/' && dir[d - 1] != '\\';
  size_t len = d + (size_t)need_sep + strlen(name) + 1;
  char *out = malloc(len);
  if (!out)
    return NULL;
  memcpy(out, dir, d);
  if (need_sep)
    out[d++] = '/';
  strcpy(out + d, name);
  return out;
}

static int is_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return 0;
  fclose(f);
  return 1;
}

static int has_build_h(const char *dir) {
  if (!dir || !*dir)
    return 0;
  char *p = path_join(dir, "build.h");
  if (!p)
    return 0;
  int ok = is_file(p);
  free(p);
  return ok;
}

static char *resolve_include_dir(void) {
  const char *prefix = getenv("FORGE_PREFIX");
  char *exe_dir = os_exe_dir();
  /* 私有 include 位(<prefix>/lib/forge/include;无 prefix 时从可执行
     文件的上级 lib 推断):build.h 太通用,不放进全局 /usr/include,
     避免与其他包撞名,也避免误用他人同名头 */
  char *priv = NULL;
  char *exe_priv = NULL;
  char *exe_inc = exe_dir ? path_join(exe_dir, "include") : NULL;
  if (exe_dir) {
    char *base = os_path_dirname(exe_dir);
    if (base) {
      exe_priv = path_join(base, "lib/forge/include");
      free(base);
    }
  }
  if (prefix && *prefix)
    priv = path_join(prefix, "lib/forge/include");

  char *cands[7];
  int n = 0;
  if (priv)
    cands[n++] = priv;
  if (prefix && *prefix)
    cands[n++] = path_join(prefix, "include"); /* legacy bootstrap layout */
  if (exe_priv)
    cands[n++] = exe_priv;
  if (exe_inc)
    cands[n++] = exe_inc;
  cands[n++] = forge_strdup("/usr/lib/forge/include");
  cands[n++] = forge_strdup("/usr/local/lib/forge/include");
  free(exe_dir);

  for (int i = 0; i < n; i++) {
    if (cands[i] && has_build_h(cands[i])) {
      for (int j = 0; j < n; j++)
        if (j != i)
          free(cands[j]);
      return cands[i];
    }
  }
  fprintf(stderr, "%s: build.h not found in", program_name);
  for (int i = 0; i < n; i++)
    fprintf(stderr, " %s", cands[i] ? cands[i] : "(null)");
  fprintf(stderr, " (set FORGE_PREFIX or install build.h)\n");
  for (int i = 0; i < n; i++)
    free(cands[i]);
  return NULL;
}

static char *resolve_lib(int compiler) {
  const char *names[3] = {compiler == MSVC ? "main.lib" : "libmain.a",
                          compiler == MSVC ? NULL : "main.a", NULL};
  const char *prefix = getenv("FORGE_PREFIX");
  char *exe_dir = os_exe_dir();
  char *exe_priv = NULL;
  if (exe_dir) {
    char *base = os_path_dirname(exe_dir);
    if (base) {
      exe_priv = path_join(base, "lib/forge");
      free(base);
    }
  }

  char *dirs[7];
  int n = 0;
  if (prefix && *prefix) {
    if (compiler == MSVC)
      dirs[n++] = forge_strdup(prefix);
    else {
      dirs[n++] = path_join(prefix, "lib/forge"); /* private lib area */
      dirs[n++] = path_join(prefix, "lib");       /* legacy */
    }
  }
  if (exe_priv)
    dirs[n++] = exe_priv;
  if (exe_dir)
    dirs[n++] = forge_strdup(exe_dir);
  dirs[n++] = forge_strdup("/usr/lib/forge");
  dirs[n++] = forge_strdup("/usr/local/lib/forge");
  dirs[n++] = forge_strdup("/usr/local/lib");
  dirs[n++] = forge_strdup("/usr/lib");
  free(exe_dir);

  for (int i = 0; i < n; i++) {
    for (int k = 0; names[k]; k++) {
      char *p = path_join(dirs[i], names[k]);
      if (!p)
        continue;
      if (is_file(p)) {
        for (int j = 0; j < n; j++)
          free(dirs[j]);
        free(exe_dir);
        return p;
      }
      free(p);
    }
  }
  fprintf(stderr, "%s: library", program_name);
  for (int k = 0; names[k]; k++)
    fprintf(stderr, " %s", names[k]);
  fprintf(stderr, " not found in");
  for (int i = 0; i < n; i++)
    fprintf(stderr, " %s", dirs[i]);
  fprintf(stderr, " (set FORGE_PREFIX or install to /usr/local/lib)\n");
  for (int i = 0; i < n; i++)
    free(dirs[i]);
  free(exe_dir);
  return NULL;
}

//TODO: later, scan symbols with symbol_lister once compiled to obj
//(current implementation: textual scan for identifiers after function(;
//an export list is enough, though not exact)
static char **collect_export_symbols(const char *build_c) {
  size_t cap = 4, n = 0;
  char **flags = calloc(cap, sizeof(char *));
  if (!flags)
    return NULL;

  FILE *f = fopen(build_c, "r");
  if (f) {
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
      char *p = line;
      while ((p = strstr(p, "function(")) != NULL) {
        char *name = p + strlen("function(");
        while (*name == ' ' || *name == '\t')
          name++;
        size_t len =
            strspn(name,
                   "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
        if (len > 0 && name[len] == ')') {
          char flag[128];
          snprintf(flag, sizeof(flag), "/EXPORT:function_%.*s", (int)len, name);
          if (n + 2 > cap) {
            size_t ncap = cap * 2;
            char **nf = realloc(flags, ncap * sizeof(char *));
            if (!nf)
              goto done;
            flags = nf;
            cap = ncap;
          }
          flags[n++] = forge_strdup(flag);
          p = name + len;
        } else {
          p = name;
        }
      }
    }
  done:
    fclose(f);
  }

  if (n + 2 > cap) {
    size_t ncap = cap * 2;
    char **nf = realloc(flags, ncap * sizeof(char *));
    if (!nf) {
      for (size_t i = 0; i < n; i++)
        free(flags[i]);
      free(flags);
      return NULL;
    }
    flags = nf;
  }
  flags[n++] = forge_strdup("/EXPORT:function_default");
  flags[n] = NULL;
  return flags;
}

/* Convert the whole build.conf into -DCONF_* compile flags: each
   `KEY=VALUE` line yields `-DCONF_<KEY>=<VALUE>` (values are not
   quoted, since compilation goes straight through execv, not a shell;
   key names are kept as-is; leading/trailing whitespace and a
   trailing \r are stripped; `#` starts a comment cut off at the end
   of the line; lines without '=', empty keys or pure comments are
   skipped). Returns a NULL-terminated array, or NULL on failure (the
   caller frees each element, then the array). The DSL side
   stringifies values itself, e.g. `#define STR(x) #x` then
   `STR(CONF_version)`. */
static char **conf_defines(const char *content) {
  size_t cap = 8, n = 0;
  char **defs = calloc(cap, sizeof(char *));
  if (!defs)
    return NULL;
  const char *p = content;
  while (p && *p) {
    const char *nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    const char *hash = memchr(p, '#', len);
    if (hash)
      len = (size_t)(hash - p);
    size_t klen = 0;
    while (klen < len && p[klen] != '=' && p[klen] != '\r')
      klen++;
    if (klen > 0 && p[klen] == '=') {
      const char *v = p + klen + 1;
      const char *vend = p + len;
      while (v < vend && (*v == ' ' || *v == '\t'))
        v++;
      while (vend > v &&
             (vend[-1] == ' ' || vend[-1] == '\t' || vend[-1] == '\r'))
        vend--;
      size_t vlen = (size_t)(vend - v);
      char *flag = malloc(7 /* -DCONF_ */ + klen + 1 /* = */ + vlen + 1);
      if (!flag)
        goto fail;
      memcpy(flag, "-DCONF_", 7);
      memcpy(flag + 7, p, klen);
      memcpy(flag + 7 + klen, "=", 1);
      memcpy(flag + 7 + klen + 1, v, vlen);
      flag[7 + klen + 1 + vlen] = '\0';
      if (n + 2 > cap) {
        size_t ncap = cap * 2;
        char **nd = realloc(defs, ncap * sizeof(char *));
        if (!nd) {
          free(flag);
          goto fail;
        }
        defs = nd;
        cap = ncap;
      }
      defs[n++] = flag;
    }
    p = nl ? nl + 1 : NULL;
  }
  defs[n] = NULL;
  return defs;
fail:
  for (size_t i = 0; i < n; i++)
    free(defs[i]);
  free(defs);
  return NULL;
}

/* Fetch the output_name value from build.conf (it decides the name of
   the generated script executable). If the key is absent, return NULL
   (the caller falls back to "make"); if the value is invalid - an
   empty string or one containing a path separator ('/' or '\\', the
   artifact must be a bare file name without a path) - set *bad and
   return NULL. Values follow the same rules as conf_defines: '#'-led
   comments and surrounding whitespace are stripped. */
static char *conf_output_name(const char *content, int *bad) {
  *bad = 0;
  const char *p = content;
  while (p && *p) {
    const char *nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    const char *hash = memchr(p, '#', len);
    if (hash)
      len = (size_t)(hash - p);
    size_t skip = 0;
    while (skip < len && (p[skip] == ' ' || p[skip] == '\t'))
      skip++;
    const char *k = p + skip;
    size_t kend = 0;
    while (kend < len - skip && k[kend] != '=' && k[kend] != '\r' &&
           k[kend] != ' ' && k[kend] != '\t')
      kend++;
    if (kend == 11 && strncmp(k, "output_name", 11) == 0 && k[kend] == '=') {
      const char *v = k + kend + 1;
      const char *vend = p + len;
      while (v < vend && (*v == ' ' || *v == '\t'))
        v++;
      while (vend > v &&
             (vend[-1] == ' ' || vend[-1] == '\t' || vend[-1] == '\r'))
        vend--;
      size_t vlen = (size_t)(vend - v);
      if (vlen == 0 || memchr(v, '/', vlen) || memchr(v, '\\', vlen)) {
        *bad = 1;
        return NULL;
      }
      char *n = malloc(vlen + 1);
      if (n) {
        memcpy(n, v, vlen);
        n[vlen] = '\0';
      }
      return n;
    }
    p = nl ? nl + 1 : NULL;
  }
  return NULL;
}

int generate_build(char *target, int compiler, char *compiler_path,
                   char *conf_content) {
  if (!target || !*target || !compiler_path || !*compiler_path) {
    fprintf(stderr, "%s: invalid arguments (target or compiler path empty)\n", program_name);
    return -1;
  }
  if (compiler != GCC && compiler != CLANG && compiler != MSVC) {
    fprintf(stderr, "%s: unknown compiler (%d), expected GCC, CLANG or MSVC\n", program_name, 
            compiler);
    return -1;
  }

  char *build_c = path_join(target, "build.c");
  if (!build_c)
    return -1;
  if (!is_file(build_c)) {
    fprintf(stderr, "%s: cannot open %s\n", program_name, build_c);
    free(build_c);
    return -1;
  }

  char *inc = resolve_include_dir();
  if (!inc) {
    free(build_c);
    return -1;
  }
  size_t incflag_len = strlen(inc) + 3;
  char *incflag = malloc(incflag_len);
  if (!incflag) {
    free(inc);
    free(build_c);
    return -1;
  }
  snprintf(incflag, incflag_len, "-I%s", inc);

  char *forge_dir = path_join(target, ".forge");
  if (!forge_dir) {
    free(incflag);
    free(inc);
    free(build_c);
    return -1;
  }
  if (os_mkdir_r(forge_dir) != 0) {
    fprintf(stderr, "%s: cannot create directory %s\n", program_name, forge_dir);
    free(forge_dir);
    free(incflag);
    free(inc);
    free(build_c);
    return -1;
  }

  /* Flags to compile build.c = -I <include dir> + -DCONF_* definitions
     from build.conf */
  char **conf_defs = conf_defines(conf_content);
  if (!conf_defs) {
    fprintf(stderr, "%s: out of memory\n", program_name);
    free(forge_dir);
    free(incflag);
    free(inc);
    free(build_c);
    return -1;
  }
  size_t extra_n = 0;
  while (conf_defs[extra_n])
    extra_n++;
  char **compile_extra = calloc(extra_n + 2, sizeof(char *));
  if (!compile_extra) {
    for (size_t i = 0; conf_defs[i]; i++)
      free(conf_defs[i]);
    free(conf_defs);
    fprintf(stderr, "%s: out of memory\n", program_name);
    free(forge_dir);
    free(incflag);
    free(inc);
    free(build_c);
    return -1;
  }
  compile_extra[0] = incflag;
  for (size_t i = 0; i < extra_n; i++)
    compile_extra[i + 1] = conf_defs[i];

  char *srcs[] = {build_c, NULL};
  int ret = gen_compile(compiler_path, compiler, srcs, forge_dir, compile_extra);
  for (size_t i = 0; conf_defs[i]; i++)
    free(conf_defs[i]);
  free(conf_defs);
  free(compile_extra);
  if (ret != 0) {
    fprintf(stderr, "%s: compile %s failed (%d)\n", program_name, build_c, ret);
    free(forge_dir);
    free(incflag);
    free(inc);
    free(build_c);
    return ret;
  }
  free(incflag);
  free(inc);

  const char *obj_name = compiler == MSVC ? "build.obj" : "build.o";
  char *obj = path_join(forge_dir, obj_name);
  if (!obj) {
    free(forge_dir);
    free(build_c);
    return -1;
  }

  const char *exe_ext = os_exe_ext();
  int exe_bad = 0;
  char *conf_exe = conf_output_name(conf_content, &exe_bad);
  if (exe_bad) {
    fprintf(stderr,
            "%s: invalid output_name in build.conf (must be a bare file "
            "name, no path separators)\n",
            program_name);
    free(obj);
    free(forge_dir);
    free(build_c);
    return -1;
  }
  const char *exe_base = conf_exe ? conf_exe : "make"; /* default output_name */
  size_t exe_name_len = strlen(exe_base) + strlen(exe_ext) + 1;
  char *exe_name = malloc(exe_name_len);
  if (!exe_name) {
    free(obj);
    free(forge_dir);
    free(build_c);
    free(conf_exe);
    return -1;
  }
  snprintf(exe_name, exe_name_len, "%s%s", exe_base, exe_ext);
  char *exe = path_join(target, exe_name);
  free(exe_name);
  free(conf_exe);
  if (!exe) {
    free(obj);
    free(forge_dir);
    free(build_c);
    return -1;
  }

  char *lib = resolve_lib(compiler);
  if (!lib) {
    free(exe);
    free(obj);
    free(forge_dir);
    free(build_c);
    return -1;
  }

  char **link_extra = NULL;
  if (compiler == MSVC) {
    link_extra = collect_export_symbols(build_c);
  } else {
    link_extra = calloc(2, sizeof(char *));
    if (link_extra)
      link_extra[0] = (char *)os_export_flag();
  }
  if (!link_extra) {
    fprintf(stderr, "%s: out of memory\n", program_name);
    free(lib);
    free(exe);
    free(obj);
    free(forge_dir);
    free(build_c);
    return -1;
  }

  char *objs[] = {obj, lib, NULL};
  ret = gen_link(compiler_path, compiler, objs, exe, link_extra);
  if (ret != 0)
    fprintf(stderr, "%s: link %s failed (%d)\n", program_name, exe, ret);

  if (compiler == MSVC) {
    for (char **p = link_extra; *p; p++)
      free(*p);
  }
  free(link_extra);
  free(lib);
  free(exe);
  free(obj);
  free(forge_dir);
  free(build_c);
  return ret;
}

int forge_main(char *target) {
  int ret = 0;
  int compiler = UNKNOWN;
  char *compiler_path = nullptr;
  char *buf = path_join(target, "build.conf");
  FILE *config = fopen(buf, "r");
  if (config) {
    fseek(config, 0, SEEK_END);
    long size = ftell(config);
    if (size == -1) {
      fprintf(stderr, "Can't read %s\n", buf);
      ret = -1;
      goto end;
    }
    rewind(config);

    char *content = (char *)malloc(size + 1);
    if (!content) {
      fprintf(stderr, "Can't read %s\n", buf);
      ret = -1;
      goto end;
    }

    size_t bytes_read = fread(content, 1, size, config);
    if (bytes_read != (size_t)size) {
      fprintf(stderr, "Can't read %s\n", buf);
      free(content);
      ret = -1;
      goto end;
    }
    content[bytes_read] = '\0';

    fclose(config);
    config = NULL;
    free(buf);
    buf = content;

    compiler_path = get_compiler_path(buf);
    char *comp = get_compiler(buf);
    if (comp) {
      if (strcmp(comp, "GCC") == 0)
        compiler = GCC;
      else if (strcmp(comp, "CLANG") == 0)
        compiler = CLANG;
      else if (strcmp(comp, "MSVC") == 0)
        compiler = MSVC;
      else {
        /* An unknown compiler must be reported explicitly: previously
           it fell into UNKNOWN, compiler_path stayed NULL, and
           generate_build misreported "invalid arguments" - a silently
           misleading error. */
        fprintf(stderr, "%s: unknown compiler '%s' in build.conf\n",
                program_name, comp);
        free(comp);
        ret = -1;
        goto end;
      }
      free(comp);
    } else compiler = GCC;

    if(!compiler_path) {
      switch(compiler) {
        case GCC:
          compiler_path = forge_strdup("gcc");
          break;
        case CLANG:
          compiler_path = forge_strdup("clang");
          break;
        case MSVC:
          compiler_path = forge_strdup("cl");
          break;
      }
    }

    /* buf = build.conf contents */
    ret = generate_build(target, compiler, compiler_path, buf);

  } else {
    fprintf(stderr, "Can't open %s\n", buf);
    ret = -1;
  }
end:
  if (config)
    fclose(config);
  free(compiler_path);
  free(buf);
  return ret;
}