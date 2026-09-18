/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#ifndef FORGE_STRV
#define FORGE_STRV

typedef struct {
  char **strs;    /* array of elements, NULL-terminated */
  int count;      /* number of stored elements */
  int capacity;   /* container capacity (allocated length of strs) */
} strv_t;

strv_t *strv_new();
void strv_free(strv_t *strv);
int strv_init(strv_t *strv);
void strv_destroy(strv_t *strv);

int strv_append(strv_t* strv, const char* str);

/**
 * @brief Set the string at the given idx; the current entry at idx must
 *        not be NULL (no gaps allowed)
 *
 * @param strv the target object
 * @param idx the target index
 * @param str the target string
 * @return int 0 on success, -1 on failure
 */
int strv_set(strv_t* strv, int idx, const char* str);
/**
 * @brief Get the string at the given idx
 * **Important** returns the raw pointer; strdup it yourself
 *
 * @param strv the target object
 * @param idx the target index
 * @return char* pointer into strs
 */
char* strv_get(strv_t* strv, int idx);

/**
 * @brief Search for a target string in the strv
 *
 * @param strv the target object
 * @param target the target string
 * @return int index of the first occurrence, or -1 if not found
 */
int strv_find(strv_t* strv, const char* target);
/**
 * @brief Search for a target string in the strv and replace the first
 *        occurrence
 *
 * @param strv the target object
 * @param target the target string
 * @param replacement the string that replaces the target
 * @return int index of the target on success, -1 on failure
 */
int strv_replace_first(strv_t* strv, const char* target, const char* replacement);
/**
 * @brief Replace all occurrences of target in the strv
 *
 * @param strv the target object
 * @param target the target string
 * @param replacement the string that replaces the target
 * @return int number of replacements actually made, -1 on failure
 */
int strv_replace_all(strv_t* strv, const char* target, const char* replacement);

strv_t* strv_dup(const strv_t *strv);
int strv_copy(strv_t *dest, const strv_t *src);
int strv_append_non_copy(strv_t* strv, char* str);

#endif