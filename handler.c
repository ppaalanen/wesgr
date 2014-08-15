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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <assert.h>

#include <json.h>

#include "wesgr.h"

static void
timespec_invalidate(struct timespec *ts)
{
	ts->tv_nsec = -1;
}

static int
timespec_is_valid(struct timespec *ts)
{
	return ts->tv_nsec >= 0;
}

static struct output_graph *
get_output_graph(struct parse_context *ctx, struct object_info *output)
{
	struct output_graph *og;
	struct info_weston_output *wo;

	if (!output)
		return NULL;

	assert(output->type == TYPE_WESTON_OUTPUT);
	wo = &output->info.wo;

	if (wo->output_gr)
		return wo->output_gr;

	og = calloc(1, sizeof *og);
	if (!og)
		return NULL;

	timespec_invalidate(&og->last_req);
	timespec_invalidate(&og->last_finished);
	timespec_invalidate(&og->last_begin);
	timespec_invalidate(&og->last_posted);
	og->info = wo;
	og->next = ctx->gdata->output;
	ctx->gdata->output = og;

	wo->output_gr = og;

	return og;
}

static struct line_block *
line_block_create(struct line_graph *linegr, const struct timespec *begin,
		  const struct timespec *end, const char *style)
{
	struct line_block *lb;

	lb = calloc(1, sizeof *lb);
	if (!lb)
		return NULL;

	lb->begin = *begin;
	lb->end = *end;
	lb->style = style;
	lb->next = linegr->block;
	linegr->block = lb;

	return lb;
}

static int
core_repaint_begin(struct parse_context *ctx, const struct timespec *ts,
		   struct json_object *jobj)
{
	struct object_info *output;
	struct output_graph *og;

	output = get_object_info_from_timepoint(ctx, jobj, "wo");
	og = get_output_graph(ctx, output);
	if (!og)
		return -1;

	og->last_begin = *ts;

	if (timespec_is_valid(&og->last_finished)) {
		struct line_block *lb;

		lb = line_block_create(&og->repaint_line, &og->last_finished,
				       ts, "repaint_delay");
		if (!lb)
			return -1;
	}

	return 0;
}

static int
core_repaint_posted(struct parse_context *ctx, const struct timespec *ts,
		    struct json_object *jobj)
{
	struct object_info *output;
	struct output_graph *og;

	output = get_object_info_from_timepoint(ctx, jobj, "wo");
	og = get_output_graph(ctx, output);
	if (!og)
		return -1;

	og->last_posted = *ts;

	if (timespec_is_valid(&og->last_begin)) {
		struct line_block *lb;

		lb = line_block_create(&og->repaint_line, &og->last_begin,
				       ts, "repaint_submit");
		if (!lb)
			return -1;
	}

	return 0;
}

static int
core_repaint_finished(struct parse_context *ctx, const struct timespec *ts,
		      struct json_object *jobj)
{
	struct object_info *output;
	struct output_graph *og;

	output = get_object_info_from_timepoint(ctx, jobj, "wo");
	og = get_output_graph(ctx, output);
	if (!og)
		return -1;

	og->last_finished = *ts;

	if (timespec_is_valid(&og->last_posted)) {
		struct line_block *lb;

		lb = line_block_create(&og->repaint_line, &og->last_posted,
				       ts, "repaint_gpu");
		if (!lb)
			return -1;
	}

	return 0;
}

static int
core_repaint_req(struct parse_context *ctx, const struct timespec *ts,
		 struct json_object *jobj)
{
	struct object_info *output;
	struct output_graph *og;

	output = get_object_info_from_timepoint(ctx, jobj, "wo");
	og = get_output_graph(ctx, output);
	if (!og)
		return -1;

	og->last_req = *ts;

	return 0;
}

static int
debug_handler(struct parse_context *ctx,
	      const struct timespec *ts, struct json_object *jobj)
{
	printf("%" PRId64 ".%09ld %s\n", (int64_t)ts->tv_sec, ts->tv_nsec,
	       json_object_get_string(jobj));

	return 0;
}

const struct tp_handler_item tp_handler_list[] = {
	{ "core_repaint_enter_loop", debug_handler },
	{ "core_repaint_exit_loop", debug_handler },
	{ "core_repaint_finished", core_repaint_finished },
	{ "core_repaint_begin", core_repaint_begin },
	{ "core_repaint_posted", core_repaint_posted },
	{ "core_repaint_req", core_repaint_req },
	{ NULL, NULL }
};

