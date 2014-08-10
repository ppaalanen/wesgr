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

#ifndef WESGR_H
#define WESGR_H

#include <stdint.h>

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

struct json_object;
struct timespec;

struct graph_data {
};

enum object_type {
	TYPE_WESTON_OUTPUT,
	TYPE_WESTON_SURFACE,
};

struct info_weston_output {
	const char *name;
};

struct info_weston_surface {
	const char *description;
};

struct object_info {
	unsigned id;
	enum object_type type;
	struct json_object *jobj;
	struct object_info *next;
	union {
		struct info_weston_output wo;
		struct info_weston_surface ws;
	} info;
};

struct lookup_table {
	unsigned id_base;
	void **array;
	unsigned alloc;
};

struct parse_context {
	struct lookup_table idmap;
	struct graph_data *gdata;
	struct object_info *obj_list;
};

typedef int (*tp_handler_t)(struct parse_context *ctx,
			    const struct timespec *ts,
			    struct json_object *jobj);

struct tp_handler_item {
	const char *tp_name;
	tp_handler_t func;
};

extern const struct tp_handler_item tp_handler_list[];

int
graph_data_init(struct graph_data *gdata);

void
graph_data_release(struct graph_data *gdata);

int
parse_context_init(struct parse_context *ctx, struct graph_data *gdata);

void
parse_context_release(struct parse_context *ctx);

int
parse_context_process_object(struct parse_context *ctx,
			     struct json_object *jobj);

struct object_info *
get_object_info_from_timepoint(struct parse_context *ctx,
			       struct json_object *jobj, const char *member);

#endif /* WESGR_H */

