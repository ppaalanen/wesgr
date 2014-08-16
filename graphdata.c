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

#include "wesgr.h"

struct svg_context {
	FILE *fp;
	struct timespec begin;
	double width;
	double height;
	double nsec_to_x;
	double offset_x;
	double line_step_y;

	struct {
		uint64_t a, b;
	} time_range;

	double cur_y;
};

int
graph_data_init(struct graph_data *gdata)
{
	memset(gdata, 0, sizeof *gdata);
	timespec_invalidate(&gdata->begin);

	return 0;
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
	return ctx->offset_x + ctx->nsec_to_x * (nsec - ctx->time_range.a);
}

static double
svg_get_x(struct svg_context *ctx, const struct timespec *ts)
{
	return svg_get_x_from_nsec(ctx, timespec_sub_to_nsec(ts, &ctx->begin));
}

static int
line_block_to_svg(struct line_block *lb, struct svg_context *ctx)
{
	double a, b;

	a = svg_get_x(ctx, &lb->begin);
	b = svg_get_x(ctx, &lb->end);

	fprintf(ctx->fp, "<path d=\"M %.2f %.2f H %.2f\" />\n",
		a, ctx->cur_y, b);

	return 0;
}

static int
line_graph_to_svg(struct line_graph *linegr, struct svg_context *ctx,
		  const char *style)
{
	struct line_block *lb;

	fprintf(ctx->fp, "<g class=\"%s\">\n", style);

	for (lb = linegr->block; lb; lb = lb->next)
		if (line_block_to_svg(lb, ctx) < 0)
			return ERROR;

	fprintf(ctx->fp, "</g>\n");

	return 0;
}

static int
output_graph_to_svg(struct output_graph *og, struct svg_context *ctx)
{
	if (line_graph_to_svg(&og->delay_line, ctx, "delay_line") < 0)
		return ERROR;
	ctx->cur_y += ctx->line_step_y;

	if (line_graph_to_svg(&og->submit_line, ctx, "submit_line") < 0)
		return ERROR;
	ctx->cur_y += ctx->line_step_y;

	if (line_graph_to_svg(&og->gpu_line, ctx, "gpu_line") < 0)
		return ERROR;
	ctx->cur_y += ctx->line_step_y;

	return 0;
}

static uint64_t
round_up_nsec(uint64_t nsec, uint64_t ms)
{
	uint64_t f = ms * 1000000;

	return (nsec + f - 1) / f * f;
}

static void
time_scale_to_svg(struct svg_context *ctx)
{
	uint64_t nsec;

	fprintf(ctx->fp, "<path d=\"M %.2f %.2f H %.2f\" class=\"axis\" />\n",
		svg_get_x_from_nsec(ctx, ctx->time_range.a), ctx->cur_y,
		svg_get_x_from_nsec(ctx, ctx->time_range.b));

	fprintf(ctx->fp, "<path d=\"");
	for (nsec = round_up_nsec(ctx->time_range.a, 10);
	     nsec < ctx->time_range.b; nsec += 10000000) {
		fprintf(ctx->fp, "M %.2f %.2f V %.2f ",
			svg_get_x_from_nsec(ctx, nsec), ctx->cur_y,
			ctx->cur_y + 10.0);
	}
	fprintf(ctx->fp, "\" class=\"major_tick\" />\n");
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
svg_context_init(struct svg_context *ctx, struct graph_data *gdata)
{
	double margin = 10.0;
	double left_pad = 50.0;

	ctx->time_range.a = 0;
	ctx->time_range.b = timespec_sub_to_nsec(&gdata->end, &gdata->begin);

	ctx->width = 1300;
	ctx->height = 400;
	ctx->begin = gdata->begin;
	ctx->offset_x = margin + left_pad;

	ctx->nsec_to_x = (ctx->width - 2 * margin - left_pad) /
			 (ctx->time_range.b - ctx->time_range.a);

	ctx->line_step_y = 20.0;

	ctx->cur_y = 50.5;
}

int
graph_data_to_svg(struct graph_data *gdata, const char *filename)
{
	struct output_graph *og;
	struct svg_context ctx;

	svg_context_init(&ctx, gdata);

	ctx.fp = fopen(filename, "w");
	if (!ctx.fp)
		return ERROR;

	if (headers_to_svg(&ctx) < 0)
		return ERROR;

	time_scale_to_svg(&ctx);
	ctx.cur_y += 30.0;

	for (og = gdata->output; og; og = og->next)
		if (output_graph_to_svg(og, &ctx) < 0)
			return ERROR;

	footers_to_svg(&ctx);

	if (fclose(ctx.fp) != 0)
		return ERROR;

	return 0;
}

