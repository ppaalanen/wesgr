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

#include <json.h>

#include "wesgr.h"

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
	{ "core_repaint_finished", debug_handler },
	{ "core_repaint_begin", debug_handler },
	{ "core_repaint_posted", debug_handler },
	{ "core_repaint_req", debug_handler },
	{ NULL, NULL }
};

