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

struct json_object;

struct graph_data {
};

struct parse_context {
	struct graph_data *gdata;
};

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

#endif /* WESGR_H */

