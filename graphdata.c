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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <inttypes.h>
#include <assert.h>

#include "wesgr.h"

struct svg_context {
	FILE *fp;
	struct timespec begin;
	double width;
	double height;
	double nsec_to_x;
	double offset_x;

	struct {
		uint64_t a, b;
	} time_range;
};

int
graph_data_init(struct graph_data *gdata)
{
	memset(gdata, 0, sizeof *gdata);
	timespec_invalidate(&gdata->begin);

	return 0;
}

static void
vblank_destroy(struct vblank *vbl)
{
	free(vbl);
}

static void
vblank_set_release(struct vblank_set *vblanks)
{
	struct vblank *vbl, *tmp;

	for (vbl = vblanks->vbl; vbl; vbl = tmp) {
		tmp = vbl->next;
		vblank_destroy(vbl);
	}
}

static void
transition_destroy(struct transition *tr)
{
	free(tr);
}

static void
transition_set_release(struct transition_set *tset)
{
	struct transition *tr, *tmp;

	for (tr = tset->trans; tr; tr = tmp) {
		tmp = tr->next;
		transition_destroy(tr);
	}
}

static void
line_block_destroy(struct line_block *bl)
{
	free(bl->desc);
	free(bl);
}

static void
line_graph_release(struct line_graph *linegr)
{
	struct line_block *lb, *tmp;

	for (lb = linegr->block; lb; lb = tmp) {
		tmp = lb->next;
		line_block_destroy(lb);
	}
}

static void
output_graph_destroy(struct output_graph *og)
{
	line_graph_release(&og->delay_line);
	line_graph_release(&og->submit_line);
	line_graph_release(&og->gpu_line);
	transition_set_release(&og->begins);
	transition_set_release(&og->posts);
	vblank_set_release(&og->vblanks);
	free(og);
}

void
graph_data_release(struct graph_data *gdata)
{
	struct output_graph *og, *tmp;

	for (og = gdata->output; og; og = tmp) {
		tmp = og->next;
		output_graph_destroy(og);
	}
}

void
graph_data_time(struct graph_data *gdata, const struct timespec *ts)
{
	if (!timespec_is_valid(&gdata->begin))
		gdata->begin = *ts;
	gdata->end = *ts;
}

#define NSEC_PER_SEC 1000000000

static int
timespec_cmp(const struct timespec *a, const struct timespec *b)
{
	assert(a->tv_nsec >= 0 && a->tv_nsec < NSEC_PER_SEC);
	assert(b->tv_nsec >= 0 && b->tv_nsec < NSEC_PER_SEC);

	if (a->tv_sec < b->tv_sec)
		return -1;

	if (a->tv_sec > b->tv_sec)
		return 1;

	if (a->tv_nsec < b->tv_nsec)
		return -1;

	if (a->tv_nsec > b->tv_nsec)
		return 1;

	return 0;
}

static void
timespec_sub(struct timespec *r,
	     const struct timespec *a, const struct timespec *b)
{
	r->tv_sec = a->tv_sec - b->tv_sec;
	r->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += NSEC_PER_SEC;
	}
}

static uint64_t
timespec_sub_to_nsec(const struct timespec *a, const struct timespec *b)
{
	struct timespec d;
	uint64_t nsec;

	timespec_sub(&d, a, b);
	nsec = d.tv_sec * NSEC_PER_SEC + d.tv_nsec;

	return nsec;
}

static double
svg_get_x_from_nsec(struct svg_context *ctx, uint64_t nsec)
{
	if (nsec < ctx->time_range.a)
		return ctx->offset_x;

	if (nsec > ctx->time_range.b)
		nsec = ctx->time_range.b;

	return ctx->offset_x + ctx->nsec_to_x * (nsec - ctx->time_range.a);
}

static double
svg_get_x(struct svg_context *ctx, const struct timespec *ts)
{
	return svg_get_x_from_nsec(ctx, timespec_sub_to_nsec(ts, &ctx->begin));
}

static int
is_in_range(struct svg_context *ctx, const struct timespec *a,
	    const struct timespec *b)
{
	uint64_t begin, end;

	assert(timespec_cmp(a, b) <= 0);

	if (timespec_cmp(b, &ctx->begin) < 0)
		return 0;

	begin = timespec_sub_to_nsec(a, &ctx->begin);
	end = timespec_sub_to_nsec(b, &ctx->begin);

	return !(end < ctx->time_range.a || begin > ctx->time_range.b);
}

static int
line_block_to_svg(struct line_block *lb, struct svg_context *ctx, double y)
{
	double a, b;

	if (!is_in_range(ctx, &lb->begin, &lb->end))
		return 0;

	a = svg_get_x(ctx, &lb->begin);
	b = svg_get_x(ctx, &lb->end);
	fprintf(ctx->fp, "<path d=\"M %.2f %.2f H %.2f\" />\n", a, y, b);

	return 0;
}

static int
line_graph_to_svg(struct line_graph *linegr, struct svg_context *ctx)
{
	struct line_block *lb;

	fprintf(ctx->fp, "<g class=\"%s\">\n", linegr->style);
	fprintf(ctx->fp,
		"<text x=\"10\" y=\"0.5em\" "
		"transform=\"translate(0,%.2f)\" "
		"class=\"line_label\">%s</text>\n",
		linegr->y, linegr->label);

	for (lb = linegr->block; lb; lb = lb->next)
		if (line_block_to_svg(lb, ctx, linegr->y) < 0)
			return ERROR;

	fprintf(ctx->fp, "</g>\n");

	return 0;
}

static int
transition_to_svg(struct transition *tr, struct svg_context *ctx,
		  double y1, double y2)
{
	double t;

	if (!is_in_range(ctx, &tr->ts, &tr->ts))
		return 0;

	t = svg_get_x(ctx, &tr->ts);
	fprintf(ctx->fp, "<path d=\"M %.2f %.2f V %.2f\" />"
		"<circle cx=\"%.2f\" cy=\"%.2f\" r=\"3\" />\n",
		t, y1, y2,
		t, (y1 + y2) * 0.5);

	return 0;
}

static int
transition_set_to_svg(struct transition_set *tset, struct svg_context *ctx,
		      double y1, double y2)
{
	struct transition *tr;

	fprintf(ctx->fp, "<g class=\"%s\">\n", tset->style);

	for (tr = tset->trans; tr; tr = tr->next)
		if (transition_to_svg(tr, ctx, y1, y2) < 0)
			return ERROR;

	fprintf(ctx->fp, "</g>\n");

	return 0;
}

static int
vblank_to_svg(struct vblank *vbl, struct svg_context *ctx,
	      double y1, double y2)
{
	double t;

	if (!is_in_range(ctx, &vbl->ts, &vbl->ts))
		return 0;

	t = svg_get_x(ctx, &vbl->ts);
	fprintf(ctx->fp, "<path d=\"M %.2f %.2f V %.2f\" />"
		"<circle cx=\"%.2f\" cy=\"%.2f\" r=\"3\" />\n",
		t, y1, y2, t, y1);

	return 0;
}

static int
vblank_set_to_svg(struct vblank_set *vblanks, struct svg_context *ctx,
		      double y1, double y2)
{
	struct vblank *vbl;

	fprintf(ctx->fp, "<g class=\"vblank\">\n");

	for (vbl = vblanks->vbl; vbl; vbl = vbl->next)
		if (vblank_to_svg(vbl, ctx, y1, y2) < 0)
			return ERROR;

	fprintf(ctx->fp, "</g>\n");

	return 0;
}

static int
output_graph_to_svg(struct output_graph *og, struct svg_context *ctx)
{
	if (vblank_set_to_svg(&og->vblanks, ctx, og->y1, og->y2) < 0)
		return ERROR;

	if (line_graph_to_svg(&og->delay_line, ctx) < 0)
		return ERROR;

	if (line_graph_to_svg(&og->submit_line, ctx) < 0)
		return ERROR;

	if (line_graph_to_svg(&og->gpu_line, ctx) < 0)
		return ERROR;

	if (transition_set_to_svg(&og->begins, ctx,
				  og->delay_line.y, og->submit_line.y) < 0)
		return ERROR;

	if (transition_set_to_svg(&og->posts, ctx,
				  og->submit_line.y, og->gpu_line.y) < 0)
		return ERROR;

	return 0;
}

static uint64_t
round_up(uint64_t nsec, uint64_t f)
{
	return (nsec + f - 1) / f * f;
}

static void
time_scale_to_svg(struct svg_context *ctx, double y)
{
	uint64_t nsec;
	uint64_t skip_ms;
	static const uint64_t mtick_levels[] = { 1, 5, 10, 20, 50, 100, 500 };
	uint64_t big_skip = NSEC_PER_SEC;
	uint64_t lil_skip;
	unsigned i;
	double left, right;
	const double big_tick_size = 15.0;
	const double lil_tick_size = 10.0;
	const double tick_label_up = 5.0;

	skip_ms = round(50.0 / ctx->nsec_to_x * 1e-6);
	for (i = 0; i < ARRAY_LENGTH(mtick_levels); i++) {
		if (skip_ms < mtick_levels[i]) {
			big_skip = mtick_levels[i] * 1000000;
			break;
		}
	}
	lil_skip = big_skip / 5;

	fprintf(ctx->fp, "<path d=\"");
	for (nsec = round_up(ctx->time_range.a, big_skip);
	     nsec <= ctx->time_range.b; nsec += big_skip) {
		fprintf(ctx->fp, "M %.2f %.2f V %.2f ",
			svg_get_x_from_nsec(ctx, nsec), y, y + big_tick_size);
	}
	fprintf(ctx->fp, "\" class=\"major_tick\" />\n");

	for (nsec = round_up(ctx->time_range.a, big_skip);
	     nsec <= ctx->time_range.b; nsec += big_skip) {
		fprintf(ctx->fp, "<text x=\"%.2f\" y=\"%.2f\""
			" text-anchor=\"middle\""
			" class=\"tick_label\">%" PRIu64 "</text>\n",
			svg_get_x_from_nsec(ctx, nsec),
			y - tick_label_up, nsec / 1000000);
	}

	fprintf(ctx->fp, "<path d=\"");
	for (nsec = round_up(ctx->time_range.a, lil_skip);
	     nsec <= ctx->time_range.b; nsec += lil_skip) {
		if (nsec % big_skip == 0)
			continue;

		fprintf(ctx->fp, "M %.2f %.2f V %.2f ",
			svg_get_x_from_nsec(ctx, nsec), y, y + lil_tick_size);
	}
	fprintf(ctx->fp, "\" class=\"minor_tick\" />\n");

	left = svg_get_x_from_nsec(ctx, ctx->time_range.a);
	right = svg_get_x_from_nsec(ctx, ctx->time_range.b);
	fprintf(ctx->fp, "<path d=\"M %.2f %.2f H %.2f\" class=\"axis\" />\n",
		left, y, right);

	fprintf(ctx->fp, "<text x=\"%.2f\" y=\"-1.5em\" text-anchor=\"middle\""
		" transform=\"translate(0,%.2f)\""
		" class=\"axis_label\">time (ms)</text>\n",
		(left + right) / 2.0, y - tick_label_up);
}

static int
headers_to_svg(struct svg_context *ctx)
{
	FILE *in;
	char buf[2048];
	size_t len;

	fprintf(ctx->fp,
		"<svg xmlns=\"http://www.w3.org/2000/svg\""
		" width=\"%d\" height=\"%d\""
		" version=\"1.1\" baseProfile=\"full\">\n"
		"<defs>\n"
		"<style type=\"text/css\"><![CDATA[\n",
		(int)ctx->width, (int)ctx->height);

	in = fopen("style.css", "r");
	if (!in)
		return ERROR;

	while ((len = fread(buf, 1, sizeof(buf), in))) {
		if (fwrite(buf, 1, len, ctx->fp) != len) {
			fclose(in);
			return ERROR;
		}
	}
	fclose(in);

	fprintf(ctx->fp,
		"]]></style>\n"
		"</defs>\n"
		"<rect width=\"100%%\" height=\"100%%\" fill=\"white\" />\n"
		"<g id=\"layer1\">\n");

	return 0;
}

static void
footers_to_svg(struct svg_context *ctx)
{
	fprintf(ctx->fp,
	"</g>\n"
	"</svg>\n");
}

static void
svg_context_init(struct svg_context *ctx, struct graph_data *gdata,
		 int from_ms, int to_ms, double width, double height)
{
	const double margin = 5.0;
	const double left_pad = 250.0;
	const double right_pad = 20.0;

	if (from_ms < 0)
		ctx->time_range.a = 0;
	else
		ctx->time_range.a = (uint64_t)from_ms * 1000000;

	if (to_ms < 0)
		ctx->time_range.b = timespec_sub_to_nsec(&gdata->end,
							 &gdata->begin);
	else
		ctx->time_range.b = (uint64_t)to_ms * 1000000;

	ctx->width = width;
	ctx->height = height;
	ctx->begin = gdata->begin;
	ctx->offset_x = margin + left_pad;

	ctx->nsec_to_x = (ctx->width - 2 * margin - left_pad - right_pad) /
			 (ctx->time_range.b - ctx->time_range.a);
}

static void
graph_data_init_draw(struct graph_data *gdata, double *width, double *height)
{
	struct output_graph *og;
	const double line_step = 20.0;
	const double output_margin = 30.0;
	double y = 50.5;

	gdata->time_axis_y = y;
	y += 30.0;

	for (og = gdata->output; og; og = og->next) {
		og->y1 = y - 10.0;

		og->delay_line.y = y;
		y += line_step;

		og->submit_line.y = y;
		y += line_step;

		og->gpu_line.y = y;
		y += line_step;

		og->y2 = y + 10.0;

		y += output_margin;
	}

	*width = 1300;
	*height = y + line_step;
}

int
graph_data_to_svg(struct graph_data *gdata, int from_ms, int to_ms,
		  const char *filename)
{
	struct output_graph *og;
	struct svg_context ctx;
	double w, h;

	graph_data_init_draw(gdata, &w, &h);
	svg_context_init(&ctx, gdata, from_ms, to_ms, w, h);

	ctx.fp = fopen(filename, "w");
	if (!ctx.fp)
		return ERROR;

	if (headers_to_svg(&ctx) < 0)
		return ERROR;

	time_scale_to_svg(&ctx, gdata->time_axis_y);

	for (og = gdata->output; og; og = og->next)
		if (output_graph_to_svg(og, &ctx) < 0)
			return ERROR;

	footers_to_svg(&ctx);

	if (fclose(ctx.fp) != 0)
		return ERROR;

	return 0;
}

