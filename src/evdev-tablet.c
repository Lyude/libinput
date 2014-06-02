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
#include "config.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "evdev-tablet.h"

#define tablet_set_status(tablet,s) (tablet->status |= (s));
#define tablet_unset_status(tablet,s) (tablet->status &= ~(s));
#define tablet_has_status(tablet,s) (!!(tablet->status & s))

#define tablet_get_pressed_buttons(tablet,field) \
	(tablet->state.field & ~(tablet->prev_state.field))
#define tablet_get_released_buttons(tablet,field) \
	(tablet->prev_state.field & ~(tablet->state.field))

static void
tablet_process_absolute(struct tablet_dispatch *tablet,
			struct evdev_device *device,
			struct input_event *e,
			uint32_t time)
{
	switch (e->code) {
	case ABS_X:
	case ABS_Y:
	case ABS_PRESSURE:
	case ABS_DISTANCE:
	case ABS_TILT_X:
	case ABS_TILT_Y:
	case ABS_RX:
	case ABS_RY:
	case ABS_RZ:
	case ABS_WHEEL:
	case ABS_THROTTLE:
		set_bit(tablet->changed_axes, e->code);
		tablet_set_status(tablet, TABLET_AXES_UPDATED);
		break;
	default:
		log_info("Unhandled ABS event code 0x%x\n", e->code);
		break;
	}
}

static inline enum libinput_tablet_axis
evcode_to_axis(const uint32_t evcode)
{
	enum libinput_tablet_axis axis;

	switch (evcode) {
	case ABS_X:
		axis = LIBINPUT_TABLET_AXIS_X;
		break;
	case ABS_Y:
		axis = LIBINPUT_TABLET_AXIS_Y;
		break;
	case ABS_DISTANCE:
		axis = LIBINPUT_TABLET_AXIS_DISTANCE;
		break;
	case ABS_PRESSURE:
		axis = LIBINPUT_TABLET_AXIS_PRESSURE;
		break;
	case ABS_TILT_X:
		axis = LIBINPUT_TABLET_AXIS_TILT_HORIZONTAL;
		break;
	case ABS_TILT_Y:
		axis = LIBINPUT_TABLET_AXIS_TILT_VERTICAL;
		break;
	default:
		axis = -1;
		break;
	}

	return axis;
}

static void
tablet_update_tool(struct tablet_dispatch *tablet,
		   int32_t tool,
		   bool enabled)
{
	assert(tool != LIBINPUT_TOOL_NONE);

	if (enabled && tool != tablet->state.tool_type)
		tablet->state.tool_type = tool;
	else if (!enabled && tool == tablet->state.tool_type) {
		tablet->state.tool_type = LIBINPUT_TOOL_NONE;
		tablet_set_status(tablet, TABLET_TOOL_LEFT_PROXIMITY);
	}
}

static inline double
normalize_pressure(const struct input_absinfo * absinfo) {
	double range = absinfo->maximum - absinfo->minimum + 1;
	double value = (absinfo->value + absinfo->minimum) / range;

	return value;
}

static inline double
normalize_tilt(const struct input_absinfo * absinfo) {
	double range = absinfo->maximum - absinfo->minimum + 1;
	double value = (absinfo->value + absinfo->minimum) / range;

	/* Map to the (-1, 1) range */
	return (value * 2) - 1;
}

static void
tablet_notify_axes(struct tablet_dispatch *tablet,
		   struct evdev_device *device,
		   uint32_t time)
{
	struct libinput_device *base = &device->base;
	unsigned char changed_axes[NCHARS(LIBINPUT_TABLET_AXIS_CNT + 1)] = { 0 };
	bool axis_update_needed = false;
	
	/* A lot of the ABS axes don't apply to tablets, so we loop through the
	 * values in here so we don't waste time checking axes that will never
	 * update
	 */
	uint32_t check_axes[] = {
		ABS_X,
		ABS_Y,
		ABS_DISTANCE,
		ABS_PRESSURE,
		ABS_TILT_X,
		ABS_TILT_Y,
	};
	uint32_t *evcode;

	ARRAY_FOR_EACH(check_axes, evcode) {
		const struct input_absinfo *absinfo;
		enum libinput_tablet_axis axis;

		if (!bit_is_set(tablet->changed_axes, *evcode))
			continue;

		absinfo = libevdev_get_abs_info(device->evdev, *evcode);
		axis = evcode_to_axis(*evcode);

		switch (*evcode) {
		case ABS_X:
		case ABS_Y:
		case ABS_DISTANCE:
			tablet->axes[axis] = absinfo->value;
			break;
		case ABS_PRESSURE:
			tablet->axes[axis] = normalize_pressure(absinfo);
			break;
		case ABS_TILT_X:
		case ABS_TILT_Y:
			tablet->axes[axis] = normalize_tilt(absinfo);
			break;
		default:
			log_bug_libinput("Unhandled axis update with evcode %x\n",
					 *evcode);
		}

		absinfo = libevdev_get_abs_info(device->evdev, *evcode);

		set_bit(changed_axes, axis);
		clear_bit(tablet->changed_axes, *evcode);
		axis_update_needed = true;
	}

	if (axis_update_needed)
		tablet_notify_axis(base, time, changed_axes, tablet->axes);
}

static void
tablet_update_button(struct tablet_dispatch *tablet,
		     uint32_t evcode,
		     uint32_t enable)
{
	uint32_t button, *flags;

	/* XXX: This really depends on the expected buttons fitting in the mask */
	if (evcode >= BTN_MISC && evcode <= BTN_TASK) {
		flags = &tablet->state.pad_buttons;
		button = evcode - BTN_MISC;
	} else if (evcode >= BTN_TOUCH && evcode <= BTN_STYLUS2) {
		flags = &tablet->state.stylus_buttons;
		button = evcode - BTN_TOUCH;
	} else {
		log_info("Unhandled button %s (0x%x)\n",
			 libevdev_event_code_get_name(EV_KEY, evcode), evcode);
		return;
	}

	if (enable)
		(*flags) |= 1 << button;
	else
		(*flags) &= ~(1 << button);
}

static void
tablet_process_key(struct tablet_dispatch *tablet,
		   struct evdev_device *device,
		   struct input_event *e,
		   uint32_t time)
{
	switch (e->code) {
	case BTN_TOOL_PEN:
	case BTN_TOOL_RUBBER:
	case BTN_TOOL_BRUSH:
	case BTN_TOOL_PENCIL:
	case BTN_TOOL_AIRBRUSH:
	case BTN_TOOL_FINGER:
	case BTN_TOOL_MOUSE:
	case BTN_TOOL_LENS:
		/* These codes have an equivalent libinput_tool value */
		tablet_update_tool(tablet, e->code, e->value);
		break;
	case BTN_TOUCH:
		if (e->value) {
			tablet_set_status(tablet, TABLET_STYLUS_IN_CONTACT);
		} else {
			tablet_unset_status(tablet, TABLET_STYLUS_IN_CONTACT);
		}

		/* Fall through */
	case BTN_STYLUS:
	case BTN_STYLUS2:
	default:
		tablet_update_button(tablet, e->code, e->value);
		break;
	}
}

static void
tablet_process_misc(struct tablet_dispatch *tablet,
		    struct evdev_device *device,
		    struct input_event *e,
		    uint32_t time)
{
	switch (e->code) {
	case MSC_SERIAL:
		tablet->state.tool_serial = e->value;
		break;
	default:
		log_info("Unhandled MSC event code 0x%x\n", e->code);
		break;
	}
}

static void
tablet_check_notify_tool(struct tablet_dispatch *tablet,
			 struct evdev_device *device,
			 uint32_t time)
{
	struct libinput_device *base = &device->base;
	struct libinput_tool *tool;
	struct libinput_tool *new_tool = NULL;

	if (tablet->state.tool_type == tablet->prev_state.tool_type ||
	    tablet->state.tool_type == LIBINPUT_TOOL_NONE)
		return;

	/* Check if we already have the tool in our list of tools */
	list_for_each(tool, &tablet->tool_list, link) {
		if (tablet->state.tool_type == tool->type &&
		    tablet->state.tool_serial == tool->serial) {
			libinput_tool_ref(tool);
			new_tool = tool;
			break;
		}
	}

	/* If we didn't already have the tool in our list of tools, add it */
	if (new_tool == NULL) {
		new_tool = zalloc(sizeof *new_tool);
		*new_tool = (struct libinput_tool) {
			.type = tablet->state.tool_type,
			.serial = tablet->state.tool_serial,
			.refcount = 2,
		};

		list_insert(&tablet->tool_list, &new_tool->link);
	}
	
	tablet_notify_tool_update(base, time, new_tool);
}

static void
tablet_notify_button_mask(struct tablet_dispatch *tablet,
			  struct evdev_device *device,
			  uint32_t time,
			  uint32_t buttons,
			  uint32_t button_base,
			  enum libinput_button_state state)
{
	struct libinput_device *base = &device->base;
	int32_t num_button = 0;

	while (buttons) {
		int enabled;

		num_button++;
		enabled = (buttons & 1);
		buttons >>= 1;

		if (!enabled)
			continue;

		tablet_notify_button(base,
				     time,
				     num_button + button_base - 1,
				     state);
	}
}

static void
tablet_notify_buttons(struct tablet_dispatch *tablet,
		      struct evdev_device *device,
		      uint32_t time,
		      bool post_check)
{
	enum libinput_button_state state;
	int32_t pad_buttons, stylus_buttons;

	if (tablet->state.pad_buttons == tablet->prev_state.pad_buttons &&
	    tablet->state.stylus_buttons == tablet->prev_state.stylus_buttons)
		return;

	if (post_check) {
		/* Only notify button releases */
		state = LIBINPUT_BUTTON_STATE_RELEASED;
		pad_buttons = tablet_get_released_buttons(tablet, pad_buttons);
		stylus_buttons =
			tablet_get_released_buttons(tablet, stylus_buttons);
	} else {
		/* Only notify button presses */
		state = LIBINPUT_BUTTON_STATE_PRESSED;
		pad_buttons = tablet_get_pressed_buttons(tablet, pad_buttons);
		stylus_buttons =
			tablet_get_pressed_buttons(tablet, stylus_buttons);
	}

	tablet_notify_button_mask(tablet, device, time,
				  pad_buttons, BTN_MISC, state);
	tablet_notify_button_mask(tablet, device, time,
				  stylus_buttons, BTN_TOUCH, state);
}

static void
sanitize_tablet_axes(struct tablet_dispatch *tablet)
{
	const struct input_absinfo *distance,
	                           *pressure;

	distance = libevdev_get_abs_info(tablet->device->evdev, ABS_DISTANCE);
	pressure = libevdev_get_abs_info(tablet->device->evdev, ABS_PRESSURE);

	if (distance && pressure &&
	    bit_is_set(tablet->changed_axes, ABS_DISTANCE) &&
	    bit_is_set(tablet->changed_axes, ABS_PRESSURE) &&
	    distance->value != 0 && pressure->value != 0) {
		/* Keep distance and pressure mutually exclusive */
		clear_bit(tablet->changed_axes, ABS_DISTANCE);
	} else if (pressure && bit_is_set(tablet->changed_axes, ABS_PRESSURE) &&
		   !tablet_has_status(tablet, TABLET_STYLUS_IN_CONTACT)) {
		clear_bit(tablet->changed_axes, ABS_PRESSURE);
	}
}

static void
tablet_flush(struct tablet_dispatch *tablet,
	     struct evdev_device *device,
	     uint32_t time)
{
	if (tablet_has_status(tablet, TABLET_TOOL_LEFT_PROXIMITY)) {
		tablet_notify_proximity_out(&device->base, time);

		memset(&tablet->changed_axes, 0, sizeof(tablet->changed_axes));
		memset(&tablet->axes, 0, sizeof(tablet->axes));
		tablet->state.stylus_buttons = 0;


		tablet->status = TABLET_NONE;
	} else {
		/* pre-update notifications */
		tablet_notify_buttons(tablet, device, time, 0);

		tablet_check_notify_tool(tablet, device, time);

		if (tablet_has_status(tablet, TABLET_AXES_UPDATED)) {
			sanitize_tablet_axes(tablet);
			tablet_notify_axes(tablet, device, time);


			tablet_unset_status(tablet, TABLET_AXES_UPDATED);
		}

		/* post-update notifications */
		tablet_notify_buttons(tablet, device, time, 1);
	}

	/* replace previous state */
	tablet->prev_state = tablet->state;
}

static void
tablet_process(struct evdev_dispatch *dispatch,
	       struct evdev_device *device,
	       struct input_event *e,
	       uint64_t time)
{
	struct tablet_dispatch *tablet =
		(struct tablet_dispatch *)dispatch;

	/* If the tablet is out of proximity, ignore all events until we
	 * synchronize */
	if (tablet_has_status(tablet, TABLET_TOOL_LEFT_PROXIMITY) &&
	    e->type != EV_SYN)
		return;

	switch (e->type) {
	case EV_ABS:
		tablet_process_absolute(tablet, device, e, time);
		break;
	case EV_KEY:
		tablet_process_key(tablet, device, e, time);
		break;
	case EV_MSC:
		tablet_process_misc(tablet, device, e, time);
		break;
	case EV_SYN:
		tablet_flush(tablet, device, time);
		break;
	default:
		log_error("Unexpected event type 0x%x\n", e->type);
		break;
	}
}

static void
tablet_destroy(struct evdev_dispatch *dispatch)
{
	struct tablet_dispatch *tablet =
		(struct tablet_dispatch*)dispatch;
	struct libinput_tool *tool, *tmp;

	list_for_each_safe(tool, tmp, &tablet->tool_list, link)
		libinput_tool_unref(tool);
	
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
	tablet->state.tool_type = LIBINPUT_TOOL_NONE;

	list_init(&tablet->tool_list);

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

	return  &tablet->base;
}