/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "ds_tree.h"
#include "kv_parser.h"
#include "log.h"
#include "memutil.h"

#define KV_PARSER_MAX_LINE_LENGTH 256

struct kv_parser
{
    char *dir;
    ds_tree_t kv_list;
};

struct kv_parser_str_entry
{
    struct ds_tree_node node;
    char *key;
    char *value;
};

static void kv_parser_parse_dir(kv_parser_t *self, glob_t *gl);
static bool kv_parser_read_filenames(glob_t *gl, const char *dir_name);

kv_parser_t *kv_parser_new(void)
{
    kv_parser_t *self = CALLOC(1, sizeof(*self));
    ds_tree_init(&self->kv_list, ds_str_cmp, kv_parser_str_entry_t, node);

    return self;
}

void kv_parser_del(kv_parser_t *self)
{
    if (self == NULL) return;
    kv_parser_flush(self);
    FREE(self->dir);
    FREE(self);
}

void kv_parser_set_dir(kv_parser_t *self, const char *dir)
{
    FREE(self->dir);
    LOGD("%s: set kv dir to %s", __func__, dir);
    self->dir = STRDUP(dir);
}

bool kv_parser_is_populated(kv_parser_t *self)
{
    return ds_tree_is_empty(&self->kv_list) ? false : true;
}

void kv_parser_populate(kv_parser_t *self)
{
    glob_t gl;
    char *path, *copy, *dir;

    if (self->dir == NULL)
    {
        LOGE("%s: dir is not set", __func__);
        return;
    }

    kv_parser_flush(self);

    copy = STRDUP(self->dir);
    dir = copy;
    while ((path = strsep(&dir, ":")))
    {
        LOGD("%s: parsing dir %s", __func__, path);
        kv_parser_read_filenames(&gl, path);
        kv_parser_parse_dir(self, &gl);
        globfree(&gl);
    }

    FREE(copy);
}

void kv_parser_flush(kv_parser_t *self)
{
    kv_parser_str_entry_t *entry;

    while ((entry = ds_tree_remove_head(&self->kv_list)) != NULL)
    {
        FREE(entry->key);
        FREE(entry->value);
        FREE(entry);
    }
}

/* It returns value of the key. Modification of return string is undefined behavior */
const char *kv_parser_get_value(kv_parser_t *self, const char *key)
{
    kv_parser_str_entry_t *entry;

    if (self == NULL)
    {
        LOGE("%s: self is not initialized when called key=%s (missing call kv_parser_new())", __func__, key);
        return NULL;
    }

    entry = ds_tree_find(&self->kv_list, key);
    if (entry == NULL) return NULL;

    return entry->value;
}

void kv_parser_walk(kv_parser_t *self, kv_parser_walk_fn_t fn, void *priv)
{
    kv_parser_str_entry_t *entry;

    ds_tree_foreach (&self->kv_list, entry)
    {
        fn(priv, entry->key, entry->value);
    }
}

static bool kv_parser_read_filenames(glob_t *gl, const char *dir_name)
{
    int rc;
    char filename_pattern[PATH_MAX];

    snprintf(filename_pattern, sizeof(filename_pattern), "%s/*", dir_name);

    rc = glob(filename_pattern, 0, NULL, gl);
    if (rc != 0 && rc != GLOB_NOMATCH)
    {
        LOGE("%s: Glob error on pattern %s", __func__, filename_pattern);
        return false;
    }

    return true;
}

static void kv_parser_parse_line(kv_parser_t *self, char *line, size_t line_len)
{
    kv_parser_str_entry_t *entry;

    if (line_len == 0) return;
    if (line[0] == '#') return;  // omit comments
    const char *k = strsep(&line, "=");
    const char *v = strsep(&line, "\n");
    if (k == NULL) return;
    if (v == NULL) return;
    if (strlen(k) == 0) return;

    entry = ds_tree_find(&self->kv_list, k);
    if (entry == NULL)
    {
        /* No kv_str_entry object, insert new one */
        struct kv_parser_str_entry *e = MALLOC(sizeof(*e));
        e->key = STRDUP(k);
        e->value = STRDUP(v);

        ds_tree_insert(&self->kv_list, e, e->key);
    }
    else
    {
        /* kv_str_entry object exists, overwrite value */
        FREE(entry->value);

        entry->value = STRDUP(v);
    }
}

static void kv_parser_parse_file(kv_parser_t *self, const char *path)
{
    FILE *file;
    char *line = NULL;
    size_t buffer_size = 0;
    ssize_t line_len;

    file = fopen(path, "rb");
    if (file == NULL)
    {
        LOGW("%s: Unable to fopen file %s", __func__, path);
        return;
    }

    while ((line_len = getline(&line, &buffer_size, file)) != -1)
    {
        kv_parser_parse_line(self, line, line_len);
    }
    fclose(file);
    FREE(line);
}

static void kv_parser_parse_dir(kv_parser_t *self, glob_t *gl)
{
    size_t i;
    for (i = 0; i < gl->gl_pathc; i++)
    {
        kv_parser_parse_file(self, gl->gl_pathv[i]);
    }
}
