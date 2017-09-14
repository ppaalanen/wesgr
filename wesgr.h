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
#include <time.h>

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

struct json_object;

struct info_weston_output;
struct info_weston_surface;

struct update {
	struct timespec damage;
	struct timespec flush;
	struct timespec vblank;
	struct update *next;
};

struct update_graph {
	struct update_graph *next;
	struct update *updates;
	const char *style;
	char *label;

	double y;

	struct update *need_vblank;
};

struct activity {
	struct timespec begin;
	struct timespec end;
	struct activity *next;
};

struct activity_set {
	struct activity *act;
};

struct vblank {
	struct timespec ts;
	struct vblank *next;
};

struct vblank_set {
	struct vblank *vbl;
};

struct transition {
	struct timespec ts;
	struct transition *next;
};

struct transition_set {
	struct transition *trans;
	const char *style;
};

struct line_block {
	struct timespec begin;
	struct timespec end;
	const char *style;
	char *desc;
	struct line_block *next;
};

struct line_graph {
	struct line_block *block;
	const char *style;
	const char *label;

	double y;
};

struct output_graph {
	struct info_weston_output *info;
	struct output_graph *next;

	struct line_graph delay_line;
	struct line_graph submit_line;
	struct line_graph gpu_line;
	struct line_graph renderer_gpu_line;
	struct transition_set begins;
	struct transition_set posts;
	struct vblank_set vblanks;
	struct activity_set not_looping;
	struct update_graph *updates;

	double y1, y2;
	double title_y;

	struct timespec last_req;
	struct timespec last_finished;
	struct timespec last_begin;
	struct timespec last_posted;
	struct timespec last_exit_loop;
	struct timespec last_renderer_gpu_begin;
};

struct graph_data {
	struct output_graph *output;

	struct timespec begin;
	struct timespec end;

	double time_axis_y;
	double legend_y;
};

struct surface_graph_list {
	struct surface_graph_list *next;
	struct output_graph *output_gr;
	struct update_graph *update_gr;
};

enum object_type {
	TYPE_WESTON_OUTPUT,
	TYPE_WESTON_SURFACE,
};

struct info_weston_output {
	const char *name;
	struct output_graph *output_gr;
};

struct info_weston_surface {
	char *description;

	struct update *open_update;
	struct surface_graph_list *glist;
	struct surface_graph_list *last;
};

struct object_info {
	unsigned id;
	enum object_type type;
	struct json_object *jobj;
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
graph_data_end(struct graph_data *gdata);

void
graph_data_time(struct graph_data *gdata, const struct timespec *ts);

int
graph_data_to_svg(struct graph_data *gdata, int from_ms, int to_ms,
		  const char *filename);

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

struct timespec
get_timespec_from_timepoint(struct parse_context *ctx,
			       struct json_object *jobj, const char *member);

static inline void
timespec_invalidate(struct timespec *ts)
{
	ts->tv_nsec = -1;
}

static inline int
timespec_is_valid(const struct timespec *ts)
{
	return ts->tv_nsec >= 0;
}

void
generic_error(const char *file, int line, const char *func);

#define ERROR ({ generic_error(__FILE__, __LINE__, __func__); -1; })
#define ERROR_NULL ({ generic_error(__FILE__, __LINE__, __func__); NULL; })

#endif /* WESGR_H */

