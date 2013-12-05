/*
 * Copyright © 2013 Intel Corporation
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

#ifndef _UDEV_SEAT_H_
#define _UDEV_SEAT_H_

#include "config.h"

#include <libudev.h>

struct udev_seat {
	struct libinput_seat base;
	char *seat_name;
};

struct udev_input {
	struct libinput base;
	struct udev *udev;
	struct udev_monitor *udev_monitor;
	struct libinput_source *udev_monitor_source;
	char *seat_id;
};

int udev_input_process_event(struct libinput_event);
int udev_input_enable(struct udev_input *input);
void udev_input_disable(struct udev_input *input);
void udev_input_destroy(struct udev_input *input);

void udev_seat_destroy(struct udev_seat *seat);

#endif