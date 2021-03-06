/*
 * Copyright © 2014 Red Hat, Inc.
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

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include "linux/input.h"

#include "evdev-mt-touchpad.h"

#define DEFAULT_SCROLL_LOCK_TIMEOUT 300 /* ms */
/* Use a reasonably large threshold until locked into scrolling mode, to
   avoid accidentally locking in scrolling mode when trying to use the entire
   touchpad to move the pointer. The user can wait for the timeout to trigger
   to do a small scroll. */
/* In mm for touchpads with valid resolution, see tp_init_accel() */
#define DEFAULT_SCROLL_THRESHOLD 10.0

enum scroll_event {
	SCROLL_EVENT_TOUCH,
	SCROLL_EVENT_MOTION,
	SCROLL_EVENT_RELEASE,
	SCROLL_EVENT_TIMEOUT,
	SCROLL_EVENT_POSTED,
};

static uint32_t
tp_touch_get_edge(struct tp_dispatch *tp, struct tp_touch *touch)
{
	uint32_t edge = EDGE_NONE;

	if (tp->scroll.method != LIBINPUT_CONFIG_SCROLL_EDGE)
		return EDGE_NONE;

	if (touch->x > tp->scroll.right_edge)
		edge |= EDGE_RIGHT;

	if (touch->y > tp->scroll.bottom_edge)
		edge |= EDGE_BOTTOM;

	return edge;
}

static void
tp_edge_scroll_set_state(struct tp_dispatch *tp,
			 struct tp_touch *t,
			 enum tp_edge_scroll_touch_state state)
{
	libinput_timer_cancel(&t->scroll.timer);

	t->scroll.edge_state = state;

	switch (state) {
	case EDGE_SCROLL_TOUCH_STATE_NONE:
		t->scroll.edge = EDGE_NONE;
		t->scroll.threshold = DEFAULT_SCROLL_THRESHOLD;
		break;
	case EDGE_SCROLL_TOUCH_STATE_EDGE_NEW:
		t->scroll.edge = tp_touch_get_edge(tp, t);
		libinput_timer_set(&t->scroll.timer,
				   t->millis + DEFAULT_SCROLL_LOCK_TIMEOUT);
		break;
	case EDGE_SCROLL_TOUCH_STATE_EDGE:
		t->scroll.threshold = 0.01; /* Do not allow 0.0 events */
		break;
	case EDGE_SCROLL_TOUCH_STATE_AREA:
		t->scroll.edge = EDGE_NONE;
		tp_set_pointer(tp, t);
		break;
	}
}

static void
tp_edge_scroll_handle_none(struct tp_dispatch *tp,
			   struct tp_touch *t,
			   enum scroll_event event)
{
	struct libinput *libinput = tp->device->base.seat->libinput;

	switch (event) {
	case SCROLL_EVENT_TOUCH:
		if (tp_touch_get_edge(tp, t)) {
			tp_edge_scroll_set_state(tp, t,
					EDGE_SCROLL_TOUCH_STATE_EDGE_NEW);
		} else {
			tp_edge_scroll_set_state(tp, t,
					EDGE_SCROLL_TOUCH_STATE_AREA);
		}
		break;
	case SCROLL_EVENT_MOTION:
	case SCROLL_EVENT_RELEASE:
	case SCROLL_EVENT_TIMEOUT:
	case SCROLL_EVENT_POSTED:
		log_bug_libinput(libinput,
				 "unexpected scroll event %d in none state\n",
				 event);
		break;
	}
}

static void
tp_edge_scroll_handle_edge_new(struct tp_dispatch *tp,
			       struct tp_touch *t,
			       enum scroll_event event)
{
	struct libinput *libinput = tp->device->base.seat->libinput;

	switch (event) {
	case SCROLL_EVENT_TOUCH:
		log_bug_libinput(libinput,
				 "unexpected scroll event %d in edge new state\n",
				 event);
		break;
	case SCROLL_EVENT_MOTION:
		t->scroll.edge &= tp_touch_get_edge(tp, t);
		if (!t->scroll.edge)
			tp_edge_scroll_set_state(tp, t,
					EDGE_SCROLL_TOUCH_STATE_AREA);
		break;
	case SCROLL_EVENT_RELEASE:
		tp_edge_scroll_set_state(tp, t, EDGE_SCROLL_TOUCH_STATE_NONE);
		break;
	case SCROLL_EVENT_TIMEOUT:
	case SCROLL_EVENT_POSTED:
		tp_edge_scroll_set_state(tp, t, EDGE_SCROLL_TOUCH_STATE_EDGE);
		break;
	}
}

static void
tp_edge_scroll_handle_edge(struct tp_dispatch *tp,
			   struct tp_touch *t,
			   enum scroll_event event)
{
	struct libinput *libinput = tp->device->base.seat->libinput;

	switch (event) {
	case SCROLL_EVENT_TOUCH:
	case SCROLL_EVENT_TIMEOUT:
		log_bug_libinput(libinput,
				 "unexpected scroll event %d in edge state\n",
				 event);
		break;
	case SCROLL_EVENT_MOTION:
		/* If started at the bottom right, decide in which dir to scroll */
		if (t->scroll.edge == (EDGE_RIGHT | EDGE_BOTTOM)) {
			t->scroll.edge &= tp_touch_get_edge(tp, t);
			if (!t->scroll.edge)
				tp_edge_scroll_set_state(tp, t,
						EDGE_SCROLL_TOUCH_STATE_AREA);
		}
		break;
	case SCROLL_EVENT_RELEASE:
		tp_edge_scroll_set_state(tp, t, EDGE_SCROLL_TOUCH_STATE_NONE);
		break;
	case SCROLL_EVENT_POSTED:
		break;
	}
}

static void
tp_edge_scroll_handle_area(struct tp_dispatch *tp,
			   struct tp_touch *t,
			   enum scroll_event event)
{
	struct libinput *libinput = tp->device->base.seat->libinput;

	switch (event) {
	case SCROLL_EVENT_TOUCH:
	case SCROLL_EVENT_TIMEOUT:
	case SCROLL_EVENT_POSTED:
		log_bug_libinput(libinput,
				 "unexpected scroll event %d in area state\n",
				 event);
		break;
	case SCROLL_EVENT_MOTION:
		break;
	case SCROLL_EVENT_RELEASE:
		tp_edge_scroll_set_state(tp, t, EDGE_SCROLL_TOUCH_STATE_NONE);
		break;
	}
}

static void
tp_edge_scroll_handle_event(struct tp_dispatch *tp,
			    struct tp_touch *t,
			    enum scroll_event event)
{
	switch (t->scroll.edge_state) {
	case EDGE_SCROLL_TOUCH_STATE_NONE:
		tp_edge_scroll_handle_none(tp, t, event);
		break;
	case EDGE_SCROLL_TOUCH_STATE_EDGE_NEW:
		tp_edge_scroll_handle_edge_new(tp, t, event);
		break;
	case EDGE_SCROLL_TOUCH_STATE_EDGE:
		tp_edge_scroll_handle_edge(tp, t, event);
		break;
	case EDGE_SCROLL_TOUCH_STATE_AREA:
		tp_edge_scroll_handle_area(tp, t, event);
		break;
	}
}

static void
tp_edge_scroll_handle_timeout(uint64_t now, void *data)
{
	struct tp_touch *t = data;

	tp_edge_scroll_handle_event(t->tp, t, SCROLL_EVENT_TIMEOUT);
}

int
tp_edge_scroll_init(struct tp_dispatch *tp, struct evdev_device *device)
{
	struct tp_touch *t;
	int width, height;
	int edge_width, edge_height;

	width = device->abs.absinfo_x->maximum - device->abs.absinfo_x->minimum;
	height = device->abs.absinfo_y->maximum - device->abs.absinfo_y->minimum;

	switch (tp->model) {
	case MODEL_ALPS:
		edge_width = width * .15;
		edge_height = height * .15;
		break;
	case MODEL_APPLETOUCH: /* unibody are all clickpads, so N/A */
		edge_width = width * .085;
		edge_height = height * .085;
		break;
	default:
		/* For elantech and synaptics, note for lenovo #40 series,
		 * e.g. the T440s min/max are the absolute edges, not the
		 * recommended ones as usual with synaptics. But these are
		 * clickpads, so N/A.
		 */
		edge_width = width * .04;
		edge_height = height * .054;
	}

	tp->scroll.right_edge = device->abs.absinfo_x->maximum - edge_width;
	tp->scroll.bottom_edge = device->abs.absinfo_y->maximum - edge_height;

	tp_for_each_touch(tp, t) {
		t->scroll.direction = -1;
		t->scroll.threshold = DEFAULT_SCROLL_THRESHOLD;
		libinput_timer_init(&t->scroll.timer,
				    device->base.seat->libinput,
				    tp_edge_scroll_handle_timeout, t);
	}

	return 0;
}

void
tp_remove_edge_scroll(struct tp_dispatch *tp)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t)
		libinput_timer_cancel(&t->scroll.timer);
}

void
tp_edge_scroll_handle_state(struct tp_dispatch *tp, uint64_t time)
{
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {
		if (!t->dirty)
			continue;

		switch (t->state) {
		case TOUCH_NONE:
			break;
		case TOUCH_BEGIN:
			tp_edge_scroll_handle_event(tp, t, SCROLL_EVENT_TOUCH);
			break;
		case TOUCH_UPDATE:
			tp_edge_scroll_handle_event(tp, t, SCROLL_EVENT_MOTION);
			break;
		case TOUCH_END:
			tp_edge_scroll_handle_event(tp, t, SCROLL_EVENT_RELEASE);
			break;
		}
	}
}

int
tp_edge_scroll_post_events(struct tp_dispatch *tp, uint64_t time)
{
	struct libinput_device *device = &tp->device->base;
	struct tp_touch *t;
	enum libinput_pointer_axis axis;
	double dx, dy, *delta;

	tp_for_each_touch(tp, t) {
		if (!t->dirty)
			continue;

		switch (t->scroll.edge) {
			case EDGE_NONE:
				if (t->scroll.direction != -1) {
					/* Send stop scroll event */
					pointer_notify_axis(device, time,
						t->scroll.direction,
						LIBINPUT_POINTER_AXIS_SOURCE_FINGER,
						0.0);
					t->scroll.direction = -1;
				}
				continue;
			case EDGE_RIGHT:
				axis = LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL;
				delta = &dy;
				break;
			case EDGE_BOTTOM:
				axis = LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL;
				delta = &dx;
				break;
			default: /* EDGE_RIGHT | EDGE_BOTTOM */
				continue; /* Don't know direction yet, skip */
		}

		tp_get_delta(t, &dx, &dy);
		tp_filter_motion(tp, &dx, &dy, NULL, NULL, time);

		if (fabs(*delta) < t->scroll.threshold)
			continue;

		pointer_notify_axis(device, time, axis,
				    LIBINPUT_POINTER_AXIS_SOURCE_FINGER,
				    *delta);
		t->scroll.direction = axis;

		tp_edge_scroll_handle_event(tp, t, SCROLL_EVENT_POSTED);
	}

	return 0; /* Edge touches are suppressed by edge_scroll_touch_active */
}

void
tp_edge_scroll_stop_events(struct tp_dispatch *tp, uint64_t time)
{
	struct libinput_device *device = &tp->device->base;
	struct tp_touch *t;

	tp_for_each_touch(tp, t) {
		if (t->scroll.direction != -1) {
			pointer_notify_axis(device, time,
					    t->scroll.direction,
					    LIBINPUT_POINTER_AXIS_SOURCE_FINGER,
					    0.0);
			t->scroll.direction = -1;
		}
	}
}

int
tp_edge_scroll_touch_active(struct tp_dispatch *tp, struct tp_touch *t)
{
	return t->scroll.edge_state == EDGE_SCROLL_TOUCH_STATE_AREA;
}
