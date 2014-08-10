/*
 * Copyright © 2014 Pekka Paalanen <pq@iki.fi>
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <json.h>

#include "wesgr.h"

static void
lookup_table_init(struct lookup_table *tbl)
{
	tbl->id_base = 0;
	tbl->alloc = 0;
	tbl->array = NULL;
}

static int
lookup_table_ensure(struct lookup_table *tbl, unsigned n)
{
	void **arr;
	unsigned i;

	if (n <= tbl->alloc)
		return 0;

	n = (n / 512 + 1) * 512;
	if (n <= tbl->alloc)
		return -1;

	arr = realloc(tbl->array, n * sizeof(void *));
	if (!arr)
		return -1;

	for (i = tbl->alloc; i < n; i++)
		arr[i] = NULL;

	tbl->array = arr;
	tbl->alloc = n;

	return 0;
}

static void *
lookup_table_get(struct lookup_table *tbl, unsigned id)
{
	unsigned i = id - tbl->id_base;

	if (i >= tbl->alloc)
		return NULL;

	return tbl->array[i];
}

static int
lookup_table_set(struct lookup_table *tbl, unsigned id, void *data)
{
	unsigned i;

	if (id == 0)
		return -1;

	if (tbl->id_base == 0)
		tbl->id_base = id;

	i = id - tbl->id_base;
	if (lookup_table_ensure(tbl, i + 1) < 0)
		return -1;

	tbl->array[i] = data;

	return 0;
}

static void
lookup_table_release(struct lookup_table *tbl)
{
	free(tbl->array);
}

int
parse_context_init(struct parse_context *ctx, struct graph_data *gdata)
{
	lookup_table_init(&ctx->idmap);
	ctx->gdata = gdata;
	ctx->obj_list = NULL;

	return 0;
}

static void
object_info_destroy(struct object_info *oi)
{
	if (oi->jobj)
		json_object_put(oi->jobj);

	free(oi);
}

void
parse_context_release(struct parse_context *ctx)
{
	struct object_info *oi, *tmp;

	lookup_table_release(&ctx->idmap);

	for (oi = ctx->obj_list; oi; oi = tmp) {
		tmp = oi->next;
		object_info_destroy(oi);
	}
}

static struct object_info *
parse_context_create_object_info(struct parse_context *ctx, unsigned id,
				 enum object_type type,
				 struct json_object *jobj)
{
	struct object_info *oi;

	oi = malloc(sizeof *oi);
	if (!oi)
		return NULL;

	oi->id = id;
	oi->type = type;
	oi->jobj = json_object_get(jobj);
	oi->next = ctx->obj_list;
	ctx->obj_list = oi;

	return oi;
}

static int
get_object_type(enum object_type *type, const char *type_name)
{
	static const struct {
		const char *name;
		enum object_type type;
	} map[] = {
		{ "weston_output", TYPE_WESTON_OUTPUT },
		{ "weston_surface", TYPE_WESTON_SURFACE },
	};
	unsigned i;

	for (i = 0; i < ARRAY_LENGTH(map); i++) {
		if (strcmp(map[i].name, type_name) == 0) {
			*type = map[i].type;
			return 0;
		}
	}

	return -1;
}

static int
parse_context_process_info(struct parse_context *ctx,
			   struct json_object *jobj,
			   struct json_object *id_jobj)
{
	unsigned id;
	struct object_info *oi;
	struct json_object *type_jobj;
	enum object_type type;

	errno = 0;
	id = json_object_get_int64(id_jobj);
	if (errno)
		return -1;

	if (!json_object_object_get_ex(jobj, "type", &type_jobj))
		return -1;

	if (!json_object_is_type(type_jobj, json_type_string))
		return -1;

	if (get_object_type(&type, json_object_get_string(type_jobj)) < 0)
		return -1;

	oi = parse_context_create_object_info(ctx, id, type, jobj);
	if (lookup_table_set(&ctx->idmap, id, oi) < 0)
		return -1;

	return 0;
}

static int
parse_int(int64_t *r, struct json_object *jobj)
{
	int64_t value;

	if (!jobj || !json_object_is_type(jobj, json_type_int))
		return -1;

	errno = 0;
	value = json_object_get_int64(jobj);
	if (errno)
		return -1;

	*r = value;
	return 0;
}

static int
parse_timespec(struct timespec *ts_out, struct json_object *jobj)
{
	int64_t v;
	struct timespec ts;

	if (!json_object_is_type(jobj, json_type_array))
		return -1;

	if (json_object_array_length(jobj) != 2)
		return -1;

	if (parse_int(&v, json_object_array_get_idx(jobj, 0)) < 0)
		return -1;
	ts.tv_sec = v;

	if (parse_int(&v, json_object_array_get_idx(jobj, 1)) < 0)
		return -1;
	ts.tv_nsec = v;

	*ts_out = ts;

	return 0;
}

static int
parse_context_process_timepoint(struct parse_context *ctx,
				struct json_object *jobj,
				struct json_object *T_jobj)
{
	struct timespec ts;
	struct json_object *name_jobj;
	const char *name;
	unsigned i;

	if (parse_timespec(&ts, T_jobj) < 0)
		return -1;

	if (!json_object_object_get_ex(jobj, "N", &name_jobj))
		return -1;

	if (!json_object_is_type(name_jobj, json_type_string))
		return -1;

	name = json_object_get_string(name_jobj);
	for (i = 0; tp_handler_list[i].tp_name; i++)
		if (strcmp(tp_handler_list[i].tp_name, name) == 0)
			return tp_handler_list[i].func(ctx, &ts, jobj);

	fprintf(stderr, "unhandled timepoint '%s'\n", name);

	return 0;
}

int
parse_context_process_object(struct parse_context *ctx,
			     struct json_object *jobj)
{
	struct json_object *key_obj;

	if (!json_object_is_type(jobj, json_type_object))
		return -1;

	if (json_object_object_get_ex(jobj, "id", &key_obj))
		return parse_context_process_info(ctx, jobj, key_obj);

	if (json_object_object_get_ex(jobj, "T", &key_obj))
		return parse_context_process_timepoint(ctx, jobj, key_obj);

	return -1;
}

