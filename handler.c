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
#include <string.h>

#include <json.h>

#include "wesgr.h"

static struct activity *
activity_create(struct activity_set *acts,
		const struct timespec *begin, const struct timespec *end)
{
	struct activity *act;

	act = calloc(1, sizeof *act);
	if (!act)
		return ERROR_NULL;

	act->begin = *begin;
	act->end = *end;
	act->next = acts->act;
	acts->act = act;

	return act;
}

static void
activity_set_init(struct activity_set *acs)
{
	acs->act = NULL;
}

static struct vblank *
vblank_create(struct vblank_set *vblanks, const struct timespec *vbl_time)
{
	struct vblank *vbl;

	vbl = calloc(1, sizeof *vbl);
	if (!vbl)
		return ERROR_NULL;

	vbl->ts = *vbl_time;
	vbl->next = vblanks->vbl;
	vblanks->vbl = vbl;

	return vbl;
}

static void
vblank_set_init(struct vblank_set *vblanks)
{
	vblanks->vbl = NULL;
}

static struct transition *
transition_create(struct transition_set *tset, const struct timespec *ts)
{
	struct transition *trans;

	trans = calloc(1, sizeof *trans);
	if (!trans)
		return ERROR_NULL;

	trans->ts = *ts;
	trans->next = tset->trans;
	tset->trans = trans;

	return trans;
}

static void
transition_set_init(struct transition_set *tset, const char *style)
{
	tset->trans = NULL;
	tset->style = style;
}

static void
line_graph_init(struct line_graph *lg, const char *style, const char *label)
{
	lg->block = NULL;
	lg->style = style;
	lg->label = label;
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
		return ERROR_NULL;

	line_graph_init(&og->delay_line, "delay_line", "delay before repaint");
	line_graph_init(&og->submit_line, "submit_line", "output_repaint()");
	line_graph_init(&og->gpu_line, "gpu_line", "time to hit presentation");
	line_graph_init(&og->renderer_gpu_line, "renderer_gpu_line", "gpu rendering");
	transition_set_init(&og->begins, "trans_begin");
	transition_set_init(&og->posts, "trans_post");
	vblank_set_init(&og->vblanks);
	activity_set_init(&og->not_looping);

	timespec_invalidate(&og->last_req);
	timespec_invalidate(&og->last_finished);
	timespec_invalidate(&og->last_begin);
	timespec_invalidate(&og->last_posted);
	timespec_invalidate(&og->last_exit_loop);
	timespec_invalidate(&og->last_renderer_gpu_begin);
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
		return ERROR_NULL;

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
		return ERROR;

	og->last_begin = *ts;

	if (timespec_is_valid(&og->last_finished)) {
		struct line_block *lb;
		struct transition *trans;

		lb = line_block_create(&og->delay_line, &og->last_finished,
				       ts, "repaint_delay");
		if (!lb)
			return ERROR;

		trans = transition_create(&og->begins, ts);
		if (!trans)
			return ERROR;
	}

	timespec_invalidate(&og->last_finished);

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
		return ERROR;

	og->last_posted = *ts;

	if (timespec_is_valid(&og->last_begin)) {
		struct line_block *lb;
		struct transition *trans;

		lb = line_block_create(&og->submit_line, &og->last_begin,
				       ts, "repaint_submit");
		if (!lb)
			return ERROR;

		trans = transition_create(&og->posts, ts);
		if (!trans)
			return ERROR;
	}

	timespec_invalidate(&og->last_begin);

	return 0;
}

static void
process_need_list(struct update_graph *update_gr,
		  const struct timespec *vblank)
{
	struct update *update;

	update = update_gr->need_vblank;
	if (!update)
		return;

	while (1) {
		update->vblank = *vblank;

		if (!update->next)
			break;

		update = update->next;
	}

	update->next = update_gr->updates;
	update_gr->updates = update;
	update_gr->need_vblank = NULL;
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
		return ERROR;

	og->last_finished = *ts;

	if (timespec_is_valid(&og->last_posted)) {
		struct line_block *lb;
		struct vblank *vbl;
		struct update_graph *ugr;

		lb = line_block_create(&og->gpu_line, &og->last_posted,
				       ts, "repaint_gpu");
		if (!lb)
			return ERROR;

		/* XXX: use the real vblank time, not ts */
		vbl = vblank_create(&og->vblanks, ts);
		if (!vbl)
			return ERROR;

		for (ugr = og->updates; ugr; ugr = ugr->next)
			process_need_list(ugr, &vbl->ts);
	}

	timespec_invalidate(&og->last_posted);

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
		return ERROR;

	og->last_req = *ts;

	return 0;
}

static int
core_repaint_exit_loop(struct parse_context *ctx, const struct timespec *ts,
		       struct json_object *jobj)
{
	struct object_info *output;
	struct output_graph *og;

	output = get_object_info_from_timepoint(ctx, jobj, "wo");
	og = get_output_graph(ctx, output);
	if (!og)
		return ERROR;

	og->last_exit_loop = *ts;

	return 0;
}

static int
core_repaint_enter_loop(struct parse_context *ctx, const struct timespec *ts,
			struct json_object *jobj)
{
	struct object_info *output;
	struct output_graph *og;
	struct activity *act;

	output = get_object_info_from_timepoint(ctx, jobj, "wo");
	og = get_output_graph(ctx, output);
	if (!og)
		return ERROR;

	if (!timespec_is_valid(&og->last_exit_loop)) {
		og->last_exit_loop.tv_sec = 0;
		og->last_exit_loop.tv_nsec = 0;
	}

	act = activity_create(&og->not_looping, &og->last_exit_loop, ts);
	if (!act)
		return ERROR;

	timespec_invalidate(&og->last_exit_loop);

	return 0;
}

static struct update *
create_update(const struct timespec *damaged)
{
	struct update *update;

	update = calloc(1, sizeof *update);
	if (!update)
		return ERROR_NULL;

	update->damage = *damaged;
	timespec_invalidate(&update->flush);
	timespec_invalidate(&update->vblank);

	return update;
}

static struct update_graph *
create_update_graph(struct output_graph *output_gr,
		    struct info_weston_surface *iws)
{
	struct update_graph *update_gr;

	update_gr = calloc(1, sizeof *update_gr);
	if (!update_gr)
		return ERROR_NULL;

	update_gr->label = strdup(iws->description);
	update_gr->style = "damage";
	update_gr->next = output_gr->updates;
	output_gr->updates = update_gr;

	return update_gr;
}

static struct surface_graph_list *
create_surface_graph_list(struct info_weston_surface *iws,
			  struct output_graph *output_gr)
{
	struct surface_graph_list *sgl;

	assert(output_gr);

	sgl = calloc(1, sizeof *sgl);
	if (!sgl)
		return ERROR_NULL;

	sgl->update_gr = create_update_graph(output_gr, iws);
	sgl->output_gr = output_gr;
	sgl->next = iws->glist;
	iws->glist = sgl;

	if (!sgl->update_gr)
		return ERROR_NULL;

	return sgl;
}

static struct surface_graph_list *
get_surface_graph_list_default(struct parse_context *ctx,
			       struct info_weston_surface *iws)
{
	struct output_graph *output_gr;
	struct surface_graph_list *sgl;

	if (iws->last)
		return iws->last;

	assert(iws->glist == NULL);

	/* By default, pick whatever the first output is. */
	output_gr = ctx->gdata->output;
	if (!output_gr)
		return NULL;

	sgl = create_surface_graph_list(iws, output_gr);
	if (!sgl)
		return ERROR_NULL;

	iws->last = sgl;

	return sgl;
}

static struct surface_graph_list *
get_surface_graph_list(struct parse_context *ctx,
		       struct info_weston_surface *iws,
		       struct output_graph *output_gr)
{
	struct surface_graph_list *sgl;

	assert(output_gr);

	if (iws->last && iws->last->output_gr == output_gr)
		return iws->last;

	for (sgl = iws->glist; sgl; sgl = sgl->next) {
		if (sgl->output_gr == output_gr)
			return sgl;
	}

	sgl = create_surface_graph_list(iws, output_gr);
	if (!sgl)
		return ERROR_NULL;

	iws->last = sgl;

	return sgl;
}

static int
put_update_to_graph_list(struct surface_graph_list *sgl, struct update *update)
{
	if (!update)
		return 0;

	assert(update->next == NULL);
	update->next = sgl->update_gr->updates;
	sgl->update_gr->updates = update;

	return 0;
}

static int
put_update_to_need_list(struct surface_graph_list *sgl, struct update *update)
{
	if (!update)
		return 0;

	assert(update->next == NULL);
	update->next = sgl->update_gr->need_vblank;
	sgl->update_gr->need_vblank = update;

	return 0;
}

static int
core_commit_damage(struct parse_context *ctx, const struct timespec *ts,
		   struct json_object *jobj)
{
	struct object_info *surface;
	struct surface_graph_list *sgl;

	surface = get_object_info_from_timepoint(ctx, jobj, "ws");
	if (surface->type != TYPE_WESTON_SURFACE)
		return ERROR;

	sgl = get_surface_graph_list_default(ctx, &surface->info.ws);
	if (!sgl) {
		fprintf(stderr, "info: ignoring core_commit_damage event at"
			" %" PRId64 ".%09ld\n",
			(int64_t)ts->tv_sec, ts->tv_nsec);
		return 0;
	}

	if (put_update_to_graph_list(sgl, surface->info.ws.open_update) < 0)
		return ERROR;

	surface->info.ws.open_update = create_update(ts);
	if (!surface->info.ws.open_update)
		return ERROR;

	return 0;
}

static int
core_flush_damage(struct parse_context *ctx, const struct timespec *ts,
		  struct json_object *jobj)
{
	struct object_info *surface;
	struct object_info *output;
	struct output_graph *og;
	struct update *update;
	struct surface_graph_list *sgl;

	surface = get_object_info_from_timepoint(ctx, jobj, "ws");
	if (surface->type != TYPE_WESTON_SURFACE)
		return ERROR;

	update = surface->info.ws.open_update;
	if (!update) {
		update = create_update(ts);
		if (!update)
			return ERROR;

		timespec_invalidate(&update->damage);
	}
	surface->info.ws.open_update = NULL;

	output = get_object_info_from_timepoint(ctx, jobj, "wo");
	og = get_output_graph(ctx, output);
	if (!og)
		return ERROR;

	update->flush = *ts;

	sgl = get_surface_graph_list(ctx, &surface->info.ws, og);
	if (!sgl)
		return ERROR;

	if (put_update_to_need_list(sgl, update) < 0)
		return ERROR;

	return 0;
}

static int
renderer_gpu_begin(struct parse_context *ctx, const struct timespec *ts,
		   struct json_object *jobj)
{
	struct object_info *output;
	struct output_graph *og;

	output = get_object_info_from_timepoint(ctx, jobj, "wo");
	og = get_output_graph(ctx, output);
	if (!og)
		return ERROR;

	og->last_renderer_gpu_begin =
		get_timespec_from_timepoint(ctx, jobj, "gpu");

	return 0;
}

static int
renderer_gpu_end(struct parse_context *ctx, const struct timespec *ts,
		 struct json_object *jobj)
{
	struct object_info *output;
	struct output_graph *og;

	output = get_object_info_from_timepoint(ctx, jobj, "wo");
	og = get_output_graph(ctx, output);
	if (!og)
		return ERROR;

	if (timespec_is_valid(&og->last_renderer_gpu_begin)) {
		struct timespec gpu_ts;
		struct line_block *lb;

		gpu_ts = get_timespec_from_timepoint(ctx, jobj, "gpu");

		lb = line_block_create(&og->renderer_gpu_line,
				       &og->last_renderer_gpu_begin,
				       &gpu_ts, "renderer_gpu");
		if (!lb)
			return ERROR;
	}

	timespec_invalidate(&og->last_renderer_gpu_begin);

	return 0;
}

int
graph_data_end(struct graph_data *gdata)
{
	struct timespec invalid = { 0, 0 };
	struct output_graph *og;
	struct update_graph *upg;

	timespec_invalidate(&invalid);

	for (og = gdata->output; og; og = og->next) {
		if (timespec_is_valid(&og->last_exit_loop)) {
			struct activity *act;

			act = activity_create(&og->not_looping,
					      &og->last_exit_loop, &invalid);
			if (!act)
				return ERROR;
		}

		for (upg = og->updates; upg; upg = upg->next)
			process_need_list(upg, &invalid);
	}

	return 0;
}

const struct tp_handler_item tp_handler_list[] = {
	{ "core_repaint_enter_loop", core_repaint_enter_loop },
	{ "core_repaint_exit_loop", core_repaint_exit_loop },
	{ "core_repaint_finished", core_repaint_finished },
	{ "core_repaint_begin", core_repaint_begin },
	{ "core_repaint_posted", core_repaint_posted },
	{ "core_repaint_req", core_repaint_req },
	{ "core_commit_damage", core_commit_damage },
	{ "core_flush_damage", core_flush_damage },
	{ "renderer_gpu_begin", renderer_gpu_begin},
	{ "renderer_gpu_end", renderer_gpu_end},
	{ NULL, NULL }
};

