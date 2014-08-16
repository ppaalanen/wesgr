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
	double nsec_to_x;
	double offset_x;
	double line_step_y;

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

static double
svg_get_x(struct svg_context *ctx, const struct timespec *ts)
{
	struct timespec d;
	uint64_t nsec;

	timespec_sub(&d, ts, &ctx->begin);
	nsec = d.tv_sec * NSEC_PER_SEC + d.tv_nsec;

	return ctx->offset_x + ctx->nsec_to_x * nsec;
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
		  const char *svg_id)
{
	struct line_block *lb;

	fprintf(ctx->fp, "<g id=\"%s\">\n", svg_id);

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

static void
headers_to_svg(struct svg_context *ctx, int width, int height)
{
	fprintf(ctx->fp,
	"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\""
	" version=\"1.1\" baseProfile=\"full\">\n"
	"<defs>\n"
	"<style type=\"text/css\"><![CDATA[\n"
	"#layer1 {\n"
	"	stroke: black;\n"
	"	stroke-width: 5;\n"
	"}\n"
	"]]></style>\n"
	"</defs>\n"
	"<rect width=\"100%%\" height=\"100%%\" fill=\"white\" />\n"
	"<g id=\"layer1\">\n",
	width, height);
}

static void
footers_to_svg(struct svg_context *ctx)
{
	fprintf(ctx->fp,
	"</g>\n"
	"</svg>\n");
}

int
graph_data_to_svg(struct graph_data *gdata, const char *filename)
{
	struct output_graph *og;
	struct svg_context ctx;
	struct timespec d;
	uint64_t nsec;

	ctx.fp = fopen(filename, "w");
	if (!ctx.fp)
		return ERROR;

	ctx.begin = gdata->begin;
	ctx.offset_x = 10.0;
	timespec_sub(&d, &gdata->end, &gdata->begin);
	nsec = d.tv_sec * NSEC_PER_SEC + d.tv_nsec;
	ctx.nsec_to_x = 1500.0 / nsec;
	ctx.cur_y = 50.5;
	ctx.line_step_y = 20.0;

	headers_to_svg(&ctx, 1520, 400);

	for (og = gdata->output; og; og = og->next)
		if (output_graph_to_svg(og, &ctx) < 0)
			return ERROR;

	footers_to_svg(&ctx);

	if (fclose(ctx.fp) != 0)
		return ERROR;

	return 0;
}

