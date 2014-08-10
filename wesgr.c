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

#include <json.h>

#include "wesgr.h"

struct bytebuf {
	uint8_t *data;
	size_t alloc;
	size_t len;
	size_t pos;
};

static void
bytebuf_init(struct bytebuf *bb)
{
	bb->data = NULL;
	bb->alloc = 0;
	bb->len = 0;
	bb->pos = 0;
}

static void
bytebuf_release(struct bytebuf *bb)
{
	free(bb->data);
	bytebuf_init(bb);
}

static int
bytebuf_ensure(struct bytebuf *bb, size_t sz)
{
	uint8_t *data;

	if (bb->alloc >= sz)
		return 0;

	data = realloc(bb->data, sz);
	if (!data)
		return -1;

	bb->data = data;
	bb->alloc = sz;

	return 0;
}

static int
bytebuf_read_from_file(struct bytebuf *bb, FILE *fp, size_t sz)
{
	size_t ret;

	if (bytebuf_ensure(bb, sz) < 0)
		return -1;

	ret = fread(bb->data, 1, sz, fp);
	if (ferror(fp))
		return -1;

	bb->len = ret;
	bb->pos = 0;

	return 0;
}

static int
parse_file(const char *name, struct parse_context *ctx)
{
	int ret = -1;
	struct bytebuf bb;
	FILE *fp;
	struct json_tokener *jtok;
	struct json_object *jobj;

	bytebuf_init(&bb);
	jtok = json_tokener_new();
	if (!jtok)
		return -1;

	fp = fopen(name, "r");
	if (!fp)
		goto out_release;

	while (1) {
		enum json_tokener_error jerr;
		int r;

		jobj = json_tokener_parse_ex(jtok,
					     (char *)(bb.data + bb.pos),
					     bb.len - bb.pos);
		jerr = json_tokener_get_error(jtok);
		if (!jobj && jerr == json_tokener_continue) {
			if (feof(fp)) {
				ret = 0;
				break;
			}

			if (bytebuf_read_from_file(&bb, fp, 8192) < 0)
				break;

			continue;
		}

		if (!jobj) {
			fprintf(stderr, "JSON parse failure\n");
			break;
		}

		bb.pos += jtok->char_offset;

		r = parse_context_process_object(ctx, jobj);
		json_object_put(jobj);

		if (r < 0) {
			fprintf(stderr, "JSON interpretation error\n");
			break;
		}
	}

	fclose(fp);

out_release:
	bytebuf_release(&bb);
	json_tokener_free(jtok);

	return ret;
}

int
main(int argc, char *argv[])
{
	struct graph_data gdata;
	struct parse_context ctx;

	if (argc != 2)
		return 1;

	if (graph_data_init(&gdata) < 0)
		return 1;

	if (parse_context_init(&ctx, &gdata) < 0)
		return 1;

	if (parse_file(argv[1], &ctx) < 0)
		return 1;

	parse_context_release(&ctx);
	graph_data_release(&gdata);

	return 0;
}

