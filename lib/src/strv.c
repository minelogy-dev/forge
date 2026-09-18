/* SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Forge-BuildScript-Exception */
/* Copyright (C) 2026 The Forge development team */
/* Additional permission under GPLv3+ §7 applies; see LICENSE. */

#include "forge_strv.h"
#include "forge_os.h"
#include <stdlib.h>
#include <string.h>


strv_t *strv_new() {
  strv_t *v = malloc(sizeof(strv_t));
  if(v == NULL) return NULL;
  if(strv_init(v)) {
    free(v);
    return NULL;
  }
  return v;
}
void strv_free(strv_t *strv) {
  strv_destroy(strv);
  free(strv);
}

int strv_init(strv_t *strv) {
  if (strv == NULL) return -1;
  strv->count = 0;
  strv->capacity = 4;
  strv->strs = calloc(4, sizeof(char*));
  if(strv->strs == NULL) return -1;
  return 0;
}

void strv_destroy(strv_t *strv) {
  if (strv == NULL || strv->strs == NULL) return;
  for(int i = 0; i < strv->capacity; i++) {
    if(strv->strs[i] != NULL) {
      free(strv->strs[i]);
      strv->strs[i] = NULL;
    }
  }
  free(strv->strs);
}

int strv_append(strv_t* strv, const char* str) {
  if(str == NULL || strv == NULL) return 1;
  return strv_append_non_copy(strv, forge_strdup(str));
}

int strv_append_non_copy(strv_t* strv, char* str) {
  if(str == NULL || strv == NULL) return 1;
  if(strv->count >= strv->capacity - 1) {
    char **ptr = realloc(strv->strs, strv->capacity * 2 * sizeof(char*));
    if(ptr == NULL) return -1;
    strv->strs = ptr;
    strv->capacity *=2;
    memset(strv->strs + strv->count, 0, (strv->capacity - strv->count) * sizeof(char*));
  }
  strv->strs[strv->count++] = str;
  return strv->strs[strv->count-1] == NULL;
}

int strv_set(strv_t* strv, int idx, const char* str) {
  if(strv == NULL || str == NULL) return -1;
  if(idx < 0 || idx > strv->count) return -1;
  char* ptr = forge_strdup(str);
  if(ptr == NULL) return -1;
  free(strv->strs[idx]);
  strv->strs[idx] = ptr;
  return 0;
}
char* strv_get(strv_t* strv, int idx) {
  if(strv == NULL || idx < 0) return NULL;
  if(idx >= strv->capacity) return NULL;
  return strv->strs[idx];
}
int strv_find(strv_t* strv, const char* target) {
  if(strv == NULL || target == NULL) return -1;
  for(size_t i = 0; i < strv->count; i++) {
    if(strcmp(strv->strs[i], target) == 0)
      return (int)i;
  }
  return -1;
}
int strv_replace_first(strv_t* strv, const char* target, const char* replacement) {
  if(strv == NULL || replacement == NULL) return -1;
  int idx = strv_find(strv, target);
  if(idx >= 0) {
    char* ptr = forge_strdup(replacement);
    if(ptr == NULL) return -1;
    free(strv->strs[idx]); 
    strv->strs[idx] = ptr;
  }
  return idx;
}
int strv_replace_all(strv_t* strv, const char* target, const char* replacement) {
  if(strv == NULL || target == NULL || replacement == NULL) return -1;
  int num = 0;
  for(size_t i = 0; i < strv->count; i++) {
    if(strcmp(strv->strs[i], target) == 0) {
      char* ptr = forge_strdup(replacement);
      if(ptr == NULL) return -1;
      free(strv->strs[i]); 
      strv->strs[i] = ptr;
      num++;
    }
  }
  return num;
}
strv_t* strv_dup(const strv_t *strv) {
  strv_t *s = strv_new();
  if(s == NULL) return NULL;
  if(strv_copy(s, strv)) {
    strv_free(s);
    return NULL;
  }
  return s;
}
int strv_copy(strv_t *dest, const strv_t *src) {
  if (dest == NULL || src == NULL) return -1;
  if (src->capacity == 0) {
    strv_destroy(dest);
    return 0;
  }
  strv_t *v = malloc(sizeof(strv_t));
  if(v == NULL) return -1;
  v->count = src->count;
  v->capacity = src->capacity;
  v->strs = calloc(src->capacity, sizeof(char*));
  if(v->strs == NULL){
    free(v);
    return -1;
  }
  for(int i = 0; i < src->count; i++) {
    char* ptr = forge_strdup(src->strs[i]);
    if(ptr == NULL) {
      strv_free(v);
      return -1;
    }
    v->strs[i] = ptr;
  }
  strv_destroy(dest);
  *dest = *v;
  free(v);
  return 0;
}