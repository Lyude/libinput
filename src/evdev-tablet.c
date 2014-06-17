/*
 * Copyright © 2014 Red Hat, Inc.
 * Copyright © 2014 Stephen Chandler "Lyude" Paul
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
#include "config.h"
#include "evdev-tablet.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#define tablet_set_status(tablet_,s_) ((tablet_)->status |= (s_))
#define tablet_unset_status(tablet_,s_) ((tablet_)->status &= ~(s_))
#define tablet_has_status(tablet_,s_) (!!((tablet_)->status & (s_)))

static void
tablet_process_absolute(struct tablet_dispatch *tablet,
			struct evdev_device *device,
			struct input_event *e,
			uint32_t time)
{
	enum libinput_tablet_axis axis;

	switch (e->code) {
	case ABS_X:
	case ABS_Y:
		axis = evcode_to_axis(e->code);
		if (axis == LIBINPUT_TABLET_AXIS_NONE) {
			log_bug_libinput("Invalid ABS event code %#x\n",
					 e->code);
			break;
		}

		tablet->absinfo[axis] = libevdev_get_abs_info(device->evdev,
							      e->code);

		set_bit(tablet->changed_axes, axis);
		tablet_set_status(tablet, TABLET_AXES_UPDATED);
		break;
	default:
		log_info("Unhandled ABS event code %#x\n", e->code);
		break;
	}
}

static void
tablet_check_notify_axes(struct tablet_dispatch *tablet,
			 struct evdev_device *device,
			 uint32_t time)
{
	struct libinput_device *base = &device->base;
	bool axis_update_needed = false;
	int a;

	for (a = 0; a < LIBINPUT_TABLET_AXIS_CNT; a++) {
		if (!bit_is_set(tablet->changed_axes, a))
			continue;

		switch (a) {
		case LIBINPUT_TABLET_AXIS_X:
		case LIBINPUT_TABLET_AXIS_Y:
			tablet->axes[a] = tablet->absinfo[a]->value;
			break;
		default:
			log_bug_libinput("Invalid axis update: %d\n", a);
			break;
		}

		axis_update_needed = true;
	}

	if (axis_update_needed) {
		tablet_notify_axis(base, time, tablet->changed_axes, tablet->axes);
		memset(tablet->changed_axes, 0, sizeof(tablet->changed_axes));
	}
}

static void
tablet_flush(struct tablet_dispatch *tablet,
	     struct evdev_device *device,
	     uint32_t time)
{
	if (tablet_has_status(tablet, TABLET_AXES_UPDATED)) {
		tablet_check_notify_axes(tablet, device, time);
		tablet_unset_status(tablet, TABLET_AXES_UPDATED);
	}
}

static void
tablet_process(struct evdev_dispatch *dispatch,
	       struct evdev_device *device,
	       struct input_event *e,
	       uint64_t time)
{
	struct tablet_dispatch *tablet =
		(struct tablet_dispatch *)dispatch;

	switch (e->type) {
	case EV_ABS:
		tablet_process_absolute(tablet, device, e, time);
		break;
	case EV_SYN:
		tablet_flush(tablet, device, time);
		break;
	default:
		log_error("Unexpected event type %#x\n", e->type);
		break;
	}
}

static void
tablet_destroy(struct evdev_dispatch *dispatch)
{
	struct tablet_dispatch *tablet =
		(struct tablet_dispatch*)dispatch;

	free(tablet);
}

static struct evdev_dispatch_interface tablet_interface = {
	tablet_process,
	tablet_destroy
};

static int
tablet_init(struct tablet_dispatch *tablet,
	    struct evdev_device *device)
{
	tablet->base.interface = &tablet_interface;
	tablet->device = device;
	tablet->status = TABLET_NONE;

	return 0;
}

struct evdev_dispatch *
evdev_tablet_create(struct evdev_device *device)
{
	struct tablet_dispatch *tablet;

	tablet = zalloc(sizeof *tablet);
	if (!tablet)
		return NULL;

	if (tablet_init(tablet, device) != 0) {
		tablet_destroy(&tablet->base);
		return NULL;
	}

	return &tablet->base;
}
