/*
 * Copyright © 2014 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "litest.h"
#include "litest-int.h"
#include "libinput-util.h"

static void litest_wacom_cintiq_tablet_setup(void)
{
	struct litest_device *d = litest_create_device(LITEST_WACOM_TABLET);
	litest_set_current_device(d);
}

static struct input_event proximity_in[] = {
	{ .type = EV_ABS, .code = ABS_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_DISTANCE, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_TILT_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_TILT_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_MISC, .value = 2083 },
	{ .type = EV_MSC, .code = MSC_SERIAL, .value = 297797542 },
	{ .type = EV_KEY, .code = BTN_TOOL_PEN, .value = 1 },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
};

static struct input_event proximity_out[] = {
	{ .type = EV_ABS, .code = ABS_X, .value = 0 },
	{ .type = EV_ABS, .code = ABS_Y, .value = 0 },
	{ .type = EV_ABS, .code = ABS_DISTANCE, .value = 0 },
	{ .type = EV_ABS, .code = ABS_TILT_X, .value = 0 },
	{ .type = EV_ABS, .code = ABS_TILT_Y, .value = 0 },
	{ .type = EV_ABS, .code = ABS_MISC, .value = 0 },
	{ .type = EV_MSC, .code = MSC_SERIAL, .value = 297797542 },
	{ .type = EV_KEY, .code = BTN_TOOL_PEN, .value = 0 },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
};

static struct input_event proximity_out_release_buttons[] = {
	{ .type = EV_KEY, .code = BTN_TOOL_PEN, .value = 1 },
	{ .type = EV_KEY, .code = BTN_TOUCH, .value = 1 },
	{ .type = EV_KEY, .code = BTN_STYLUS, .value = 1 },
	{ .type = EV_KEY, .code = BTN_STYLUS2, .value = 1 },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = EV_KEY, .code = BTN_TOOL_PEN, .value = 0 },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = EV_KEY, .code = BTN_TOOL_PEN, .value = 1 },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
};

static struct input_event proximity_out_clear_axes[] = {
	{ .type = EV_KEY, .code = BTN_TOOL_PEN, .value = 1 },
	{ .type = EV_ABS, .code = ABS_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_DISTANCE, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_PRESSURE, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_TILT_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_TILT_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = EV_KEY, .code = BTN_TOOL_PEN, .value = 0 },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = EV_ABS, .code = ABS_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
};

static struct input_event motion[] = {
	{ .type = EV_ABS, .code = ABS_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_DISTANCE, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_PRESSURE, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_TILT_X, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_ABS, .code = ABS_TILT_Y, .value = LITEST_AUTO_ASSIGN },
	{ .type = EV_MSC, .code = MSC_SERIAL, .value = 297797542 },
	{ .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
	{ .type = -1, .code = -1 },
};

static struct litest_device_interface interface = {
	.tablet_proximity_in_events = proximity_in,
	.tablet_proximity_out_events = proximity_out,
	.tablet_proximity_out_release_buttons = proximity_out_release_buttons,
	.tablet_proximity_out_clear_axes = proximity_out_clear_axes,
	.tablet_motion_events = motion,
};

static struct input_absinfo absinfo[] = {
	{ ABS_X, 0, 53020, 4, 200 },
	{ ABS_Y, 0, 33440, 4, 200 },
	{ ABS_Z, -900, 899 },
	{ ABS_RX, 0, 4096 },
	{ ABS_RY, 0, 4096 },
	{ ABS_WHEEL, 0, 1023 },
	{ ABS_PRESSURE, 0, 1023 },
	{ ABS_DISTANCE, 0, 63 },
	{ ABS_TILT_X, 0, 127 },
	{ ABS_TILT_Y, 0, 127 },
	{ ABS_MISC, 0, 0, 0 },
	{ .value = -1 },
};

static struct input_id input_id = {
	.bustype = 0x3,
	.vendor = 0x56a,
	.product = 0xc6,
	.version = 0x113,
};

static int events[] = {
	EV_KEY, BTN_0,
	EV_KEY, BTN_1,
	EV_KEY, BTN_2,
	EV_KEY, BTN_3,
	EV_KEY, BTN_4,
	EV_KEY, BTN_5,
	EV_KEY, BTN_6,
	EV_KEY, BTN_7,
	EV_KEY, BTN_8,
	EV_KEY, BTN_9,
	EV_KEY, BTN_TOOL_PEN,
	EV_KEY, BTN_TOOL_RUBBER,
	EV_KEY, BTN_TOOL_BRUSH,
	EV_KEY, BTN_TOOL_PENCIL,
	EV_KEY, BTN_TOOL_AIRBRUSH,
	EV_KEY, BTN_TOUCH,
	EV_KEY, BTN_STYLUS,
	EV_KEY, BTN_STYLUS2,
	INPUT_PROP_MAX, INPUT_PROP_DIRECT,
	-1, -1,
};

struct litest_test_device litest_wacom_cintiq_tablet_device = {
	.type = LITEST_WACOM_TABLET,
	.features = LITEST_TABLET,
	.shortname = "wacom-cintiq-tablet",
	.setup = litest_wacom_cintiq_tablet_setup,
	.interface = &interface,

	.name = "Wacom Cintiq 12WX",
	.id = &input_id,
	.events = events,
	.absinfo = absinfo,
};