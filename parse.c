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
#include <limits.h>

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
		return ERROR;

	arr = realloc(tbl->array, n * sizeof(void *));
	if (!arr)
		return ERROR;

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
		return ERROR;

	tbl->array[i] = data;

	return 0;
}

static void
lookup_table_for_each(struct lookup_table *tbl,
		      void (*func)(void **, void*), void *data)
{
	unsigned i;

	for (i = 0; i < tbl->alloc; i++)
		if (tbl->array[i])
			(*func)(&tbl->array[i], data);
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

	return 0;
}

static void
object_info_destroy(struct object_info *oi)
{
	struct surface_graph_list *sgl, *tmp;

	if (oi->jobj)
		json_object_put(oi->jobj);

	switch (oi->type) {
	default:
	case TYPE_WESTON_OUTPUT:
		break;
	case TYPE_WESTON_SURFACE:
		free(oi->info.ws.description);
		for (sgl = oi->info.ws.glist; sgl; sgl = tmp) {
			tmp = sgl->next;
			free(sgl);
		}
		break;
	}

	free(oi);
}

static void
free_item(void **ptr, void *data)
{
	object_info_destroy(*ptr);
	*ptr = NULL;
}

void
parse_context_release(struct parse_context *ctx)
{
	lookup_table_for_each(&ctx->idmap, free_item, NULL);
	lookup_table_release(&ctx->idmap);
}

static struct object_info *
object_info_create(unsigned id, enum object_type type)
{
	struct object_info *oi;

	oi = calloc(1, sizeof *oi);
	if (!oi)
		return ERROR_NULL;

	oi->id = id;
	oi->type = type;

	return oi;
}

static int
object_info_update(struct object_info *oi, enum object_type type,
		   struct json_object *jobj)
{
	if (oi->type != type)
		return ERROR;

	if (oi->jobj)
		json_object_put(oi->jobj);
	oi->jobj = json_object_get(jobj);

	return 0;
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

	return ERROR;
}

static int
parse_id(unsigned *id, struct json_object *jobj)
{
	int64_t val;

	errno = 0;
	val = json_object_get_int64(jobj);
	if (errno)
		return ERROR;

	if (val < 0 || val > UINT_MAX)
		return ERROR;

	*id = val;

	return 0;
}

static int
parse_weston_output(struct parse_context *ctx, struct object_info *oi)
{
	struct json_object *name_jobj;

	if (!json_object_object_get_ex(oi->jobj, "name", &name_jobj))
		return ERROR;

	oi->info.wo.name = json_object_get_string(name_jobj);

	return 0;
}

static int
parse_weston_surface(struct parse_context *ctx, struct object_info *oi)
{
	struct json_object *desc_jobj;
	struct json_object *parent;
	const char *desc;
	char str[64];
	int ret;

	if (!json_object_object_get_ex(oi->jobj, "desc", &desc_jobj))
		return ERROR;

	desc = json_object_get_string(desc_jobj);
	if (!desc) {
		snprintf(str, sizeof(str), "[id:%u]", oi->id);
		desc = str;
	}

	free(oi->info.ws.description);
	oi->info.ws.description = NULL;

	if (json_object_object_get_ex(oi->jobj, "main_surface", &parent)) {
		unsigned id;
		struct object_info *poi;

		if (parse_id(&id, parent) < 0)
			return ERROR;

		poi = lookup_table_get(&ctx->idmap, id);
		if (!poi)
			return ERROR;

		ret = asprintf(&oi->info.ws.description, "%s of %s", desc,
			       poi->info.ws.description);
		if (ret < 0)
			oi->info.ws.description = NULL;
	} else {
		oi->info.ws.description = strdup(desc);
	}

	if (!oi->info.ws.description)
		return ERROR;

	return 0;
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

	if (parse_id(&id, id_jobj) < 0)
		return ERROR;

	if (!json_object_object_get_ex(jobj, "type", &type_jobj))
		return ERROR;

	if (!json_object_is_type(type_jobj, json_type_string))
		return ERROR;

	if (get_object_type(&type, json_object_get_string(type_jobj)) < 0)
		return ERROR;

	oi = lookup_table_get(&ctx->idmap, id);
	if (!oi) {
		oi = object_info_create(id, type);
		if (lookup_table_set(&ctx->idmap, id, oi) < 0)
			return ERROR;
	}

	if (object_info_update(oi, type, jobj) < 0)
		return ERROR;

	switch (oi->type) {
	case TYPE_WESTON_OUTPUT:
		return parse_weston_output(ctx, oi);
	case TYPE_WESTON_SURFACE:
		return parse_weston_surface(ctx, oi);
	}

	return 0;
}

static int
parse_int(int64_t *r, struct json_object *jobj)
{
	int64_t value;

	if (!jobj || !json_object_is_type(jobj, json_type_int))
		return ERROR;

	errno = 0;
	value = json_object_get_int64(jobj);
	if (errno)
		return ERROR;

	*r = value;
	return 0;
}

static int
parse_timespec(struct timespec *ts_out, struct json_object *jobj)
{
	int64_t v;
	struct timespec ts;

	if (!json_object_is_type(jobj, json_type_array))
		return ERROR;

	if (json_object_array_length(jobj) != 2)
		return ERROR;

	if (parse_int(&v, json_object_array_get_idx(jobj, 0)) < 0)
		return ERROR;
	ts.tv_sec = v;

	if (parse_int(&v, json_object_array_get_idx(jobj, 1)) < 0)
		return ERROR;
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
		return ERROR;

	if (!json_object_object_get_ex(jobj, "N", &name_jobj))
		return ERROR;

	if (!json_object_is_type(name_jobj, json_type_string))
		return ERROR;

	graph_data_time(ctx->gdata, &ts);
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
		return ERROR;

	if (json_object_object_get_ex(jobj, "id", &key_obj))
		return parse_context_process_info(ctx, jobj, key_obj);

	if (json_object_object_get_ex(jobj, "T", &key_obj))
		return parse_context_process_timepoint(ctx, jobj, key_obj);

	return ERROR;
}

struct object_info *
get_object_info_from_timepoint(struct parse_context *ctx,
			       struct json_object *jobj, const char *member)
{
	struct json_object *mem_jobj;
	int64_t value;
	unsigned id;

	if (!json_object_object_get_ex(jobj, member, &mem_jobj))
		return ERROR_NULL;

	if (parse_int(&value, mem_jobj) < 0)
		return ERROR_NULL;
	id = value;

	return lookup_table_get(&ctx->idmap, id);
}

struct timespec
get_timespec_from_timepoint(struct parse_context *ctx,
			    struct json_object *jobj, const char *member)
{
	struct json_object *mem_jobj;
	struct timespec ts;

	timespec_invalidate(&ts);

	if (!json_object_object_get_ex(jobj, member, &mem_jobj))
		return ts;

	parse_timespec(&ts, mem_jobj);

	return ts;
}
