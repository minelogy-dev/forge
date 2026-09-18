/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

/**
 * @file args.c
 * @brief Command-line argument parsing (forge_parse_args) and runtime
 *        global state.
 *
 * Two parts:
 * - forge_parse_args(): mirrors the opt / forge_parse_args semantics
 *   of the forge tool (src/forge.c) -- short options indexed by
 *   character code, attached `-j8` / split `-j 8`, `--jobs=8` /
 *   `--jobs 8`, prefix matching of long options with ambiguity
 *   detection, and exit(EXIT_FAILURE) on unknown / ambiguous /
 *   missing-argument options. The first positional argument ends
 *   option parsing and its index is returned. This duplicates the
 *   parsing logic of the forge tool (src/forge.c) (the binaries are
 *   independent); keep the semantics in sync when modifying.
 * - Runtime global state: os_verbose (FORGE_TLS thread-local, the
 *   three-state command printing, consumed by the build.c execution
 *   rounds) and os_jobs (a plain global, the number of parallel
 *   compile tasks), plus os_set_verbose() / os_set_jobs(). This file
 *   is platform-independent and compiled once for all platforms.
 *
 * The opt struct and forge_parse_args are declared in build.h (with
 * the OPT / builtin_args / forge_parse_args macros, used inside
 * build.c's functions to get -j/-v in one line).
 */
#include "build.h"
#include "forge_os.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(FORGE_OS_POSIX)
#include <signal.h>

/* Ignore SIGPIPE: when capture_impl writes data to a child's stdin
   and the child closes its read end early, write() would trigger the
   default SIGPIPE action (terminating the process). A build tool can
   simply ignore it globally; write() then returns EPIPE instead,
   which the caller treats as a failure. */
__attribute__((constructor)) static void ignore_sigpipe(void) {
  signal(SIGPIPE, SIG_IGN);
}
#endif

FORGE_TLS int os_verbose = 0;
int os_jobs = 1;

/* Three-state pass-through: -1 = -q silent (no output while commands
   succeed), 0 = default (executed commands always print, captured
   output only on failure), 1 = -v everything. The build.c execution
   rounds use this three-state to decide command printing and silent
   echo */
void os_set_verbose(int verbose) { os_verbose = verbose; }

void os_set_jobs(int jobs) { os_jobs = jobs < 1 ? 1 : jobs; }

int forge_parse_args(int argc, char **argv, opt_t *opts, int opts_count) {
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
          if (target->argv == NULL) {
            target->argv = malloc(8 * sizeof(size_t));
            memset(target->argv, -1, sizeof(size_t) * 8);
            target->argv[7] = NULL;
          }
          if (target->argv[target->argc] == NULL) {
            char **ptr =
                realloc(target->argv, (target->argc * 2 + 2) * sizeof(size_t));
            if (ptr == NULL) {
              fprintf(stderr, "%s: realloc failed\n", argv[0]);
              exit(EXIT_FAILURE);
            }
            target->argv = ptr;
            memset(target->argv + target->argc + 1, -1,
                   sizeof(char *) * (target->argc + 1));
            target->argv[target->argc * 2 + 1] = NULL;
          }
          if (target->takes_arg) {
            if (value) {
              target->argv[target->argc++] = value; // --output=file
            } else if (i + 1 < argc && argv[i + 1][0] != '-') {
              target->argv[target->argc++] = argv[++i]; // --output file
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
        opt_t *target = &opts[(unsigned char)argv[i][1]];
        if (target->takes_arg) {
                        if(target->argv == NULL) {
                target->argv = malloc(8 * sizeof(size_t));
                memset(target->argv, -1, sizeof(size_t) * 8);
                target->argv[7] = NULL;
              }
              if(target->argv[target->argc] == NULL) {
                char **ptr = realloc(target->argv, (target->argc * 2 + 2) * sizeof(size_t));
                if(ptr == NULL) {
                  fprintf(stderr, "%s: realloc failed\n", argv[0]);
                  exit(EXIT_FAILURE);
                }
                target->argv = ptr;
                memset(target->argv + target->argc + 1, -1, sizeof(char*) * (target->argc + 1));
                target->argv[target->argc * 2 + 1] = NULL;
              }
          if (argv[i][2] != '\0')
            target->argv[target->argc++] = argv[i] + 2;
          else if (i + 1 < argc) {
            i++;
            target->argv[target->argc++] = argv[i];
          } else {
            fprintf(stderr, "%s: option requires an argument -- '%c'\n",
                    argv[0], argv[i][1]);
            exit(EXIT_FAILURE);
          }
          continue;
        }
        for (int j = 1; argv[i][j] != '\0'; j++) {
          if (opts[(unsigned char)argv[i][j]].takes_arg) {
            fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0],
                    argv[i][j]);
            exit(EXIT_FAILURE);
          }
          opts[(unsigned char)argv[i][j]].flag_store++;
        }
      }
    } else {
      break;
    }
  }
  return i;
}