
/*
 * Copyright © 2013 Red Hat, Inc.
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

#include <config.h>

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <unistd.h>

#include "litest.h"

static int open_func_count = 0;
static int close_func_count = 0;

static int open_restricted(const char *path, int flags, void *data)
{
	int fd;
	open_func_count++;
	fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}
static void close_restricted(int fd, void *data)
{
	close_func_count++;
	close(fd);
}

const struct libinput_interface simple_interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};


START_TEST(path_create_NULL)
{
	struct libinput *li;
	const struct libinput_interface interface;

	open_func_count = 0;
	close_func_count = 0;

	li = libinput_path_create_context(NULL, NULL);
	ck_assert(li == NULL);
	li = libinput_path_create_context(&interface, NULL);
	ck_assert(li != NULL);
	libinput_destroy(li);

	ck_assert_int_eq(open_func_count, 0);
	ck_assert_int_eq(close_func_count, 0);

	open_func_count = 0;
	close_func_count = 0;
}
END_TEST

START_TEST(path_create_invalid)
{
	struct libinput *li;
	struct libinput_device *device;
	const char *path = "/tmp";

	open_func_count = 0;
	close_func_count = 0;

	li = libinput_path_create_context(&simple_interface, NULL);
	ck_assert(li != NULL);
	device = libinput_path_add_device(li, path);
	ck_assert(device == NULL);

	ck_assert_int_eq(open_func_count, 0);
	ck_assert_int_eq(close_func_count, 0);

	libinput_destroy(li);
	ck_assert_int_eq(close_func_count, 0);

	open_func_count = 0;
	close_func_count = 0;
}
END_TEST

START_TEST(path_create_destroy)
{
	struct libinput *li;
	struct libinput_device *device;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	int rc;
	void *userdata = &rc;

	evdev = libevdev_new();
	ck_assert(evdev != NULL);

	libevdev_set_name(evdev, "test device");
	libevdev_enable_event_code(evdev, EV_KEY, BTN_LEFT, NULL);
	libevdev_enable_event_code(evdev, EV_KEY, BTN_RIGHT, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_X, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_Y, NULL);

	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput);
	ck_assert_int_eq(rc, 0);
	libevdev_free(evdev);

	li = libinput_path_create_context(&simple_interface, userdata);
	ck_assert(li != NULL);
	ck_assert(libinput_get_user_data(li) == userdata);

	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput));
	ck_assert(device != NULL);

	ck_assert_int_eq(open_func_count, 1);

	libevdev_uinput_destroy(uinput);
	libinput_destroy(li);
	ck_assert_int_eq(close_func_count, 1);

	open_func_count = 0;
	close_func_count = 0;
}
END_TEST

START_TEST(path_added_seat)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_device *device;
	struct libinput_seat *seat;
	const char *seat_name;
	enum libinput_event_type type;

	libinput_dispatch(li);

	event = libinput_get_event(li);
	ck_assert(event != NULL);

	type = libinput_event_get_type(event);
	ck_assert_int_eq(type, LIBINPUT_EVENT_DEVICE_ADDED);

	device = libinput_event_get_device(event);
	seat = libinput_device_get_seat(device);
	ck_assert(seat != NULL);

	seat_name = libinput_seat_get_logical_name(seat);
	ck_assert_int_eq(strcmp(seat_name, "default"), 0);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(path_added_device)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_device *device;

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
			break;
		}

		libinput_event_destroy(event);
	}

	ck_assert(event != NULL);

	device = libinput_event_get_device(event);
	ck_assert(device != NULL);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(path_add_device)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_device *device;
	const char *sysname1 = NULL, *sysname2 = NULL;

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
			ck_assert(sysname1 == NULL);
			device = libinput_event_get_device(event);
			ck_assert(device != NULL);
			sysname1 = libinput_device_get_sysname(device);
		}

		libinput_event_destroy(event);
	}

	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(dev->uinput));
	ck_assert(device != NULL);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
			ck_assert(sysname2 == NULL);
			device = libinput_event_get_device(event);
			ck_assert(device != NULL);
			sysname2 = libinput_device_get_sysname(device);
		}

		libinput_event_destroy(event);
	}

	ck_assert_int_eq(strcmp(sysname1, sysname2), 0);

	libinput_event_destroy(event);
}
END_TEST

START_TEST(path_add_invalid_path)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_device *device;

	litest_drain_events(li);

	device = libinput_path_add_device(li, "/tmp/");
	ck_assert(device == NULL);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li)))
		ck_abort();
}
END_TEST

START_TEST(path_device_sysname)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_event *ev;
	struct libinput_device *device;
	const char *sysname;

	libinput_dispatch(dev->libinput);

	while ((ev = libinput_get_event(dev->libinput))) {
		if (libinput_event_get_type(ev) != LIBINPUT_EVENT_DEVICE_ADDED)
			continue;

		device = libinput_event_get_device(ev);
		sysname = libinput_device_get_sysname(device);
		ck_assert(sysname != NULL && strlen(sysname) > 1);
		ck_assert(strchr(sysname, '/') == NULL);
		ck_assert_int_eq(strncmp(sysname, "event", 5), 0);

		libinput_event_destroy(ev);
	}
}
END_TEST

START_TEST(path_remove_device)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_device *device;
	int remove_event = 0;

	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(dev->uinput));
	ck_assert(device != NULL);
	litest_drain_events(li);

	libinput_path_remove_device(device);
	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_DEVICE_REMOVED)
			remove_event++;

		libinput_event_destroy(event);
	}

	ck_assert_int_eq(remove_event, 1);
}
END_TEST

START_TEST(path_double_remove_device)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_device *device;
	int remove_event = 0;

	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(dev->uinput));
	ck_assert(device != NULL);
	litest_drain_events(li);

	libinput_path_remove_device(device);
	libinput_path_remove_device(device);
	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_DEVICE_REMOVED)
			remove_event++;

		libinput_event_destroy(event);
	}

	ck_assert_int_eq(remove_event, 1);
}
END_TEST

START_TEST(path_suspend)
{
	struct libinput *li;
	struct libinput_device *device;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	int rc;
	void *userdata = &rc;

	evdev = libevdev_new();
	ck_assert(evdev != NULL);

	libevdev_set_name(evdev, "test device");
	libevdev_enable_event_code(evdev, EV_KEY, BTN_LEFT, NULL);
	libevdev_enable_event_code(evdev, EV_KEY, BTN_RIGHT, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_X, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_Y, NULL);

	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput);
	ck_assert_int_eq(rc, 0);
	libevdev_free(evdev);

	li = libinput_path_create_context(&simple_interface, userdata);
	ck_assert(li != NULL);

	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput));
	ck_assert(device != NULL);

	libinput_suspend(li);
	libinput_resume(li);

	libevdev_uinput_destroy(uinput);
	libinput_destroy(li);

	open_func_count = 0;
	close_func_count = 0;
}
END_TEST

START_TEST(path_double_suspend)
{
	struct libinput *li;
	struct libinput_device *device;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	int rc;
	void *userdata = &rc;

	evdev = libevdev_new();
	ck_assert(evdev != NULL);

	libevdev_set_name(evdev, "test device");
	libevdev_enable_event_code(evdev, EV_KEY, BTN_LEFT, NULL);
	libevdev_enable_event_code(evdev, EV_KEY, BTN_RIGHT, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_X, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_Y, NULL);

	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput);
	ck_assert_int_eq(rc, 0);
	libevdev_free(evdev);

	li = libinput_path_create_context(&simple_interface, userdata);
	ck_assert(li != NULL);

	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput));
	ck_assert(device != NULL);

	libinput_suspend(li);
	libinput_suspend(li);
	libinput_resume(li);

	libevdev_uinput_destroy(uinput);
	libinput_destroy(li);

	open_func_count = 0;
	close_func_count = 0;
}
END_TEST

START_TEST(path_double_resume)
{
	struct libinput *li;
	struct libinput_device *device;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput;
	int rc;
	void *userdata = &rc;

	evdev = libevdev_new();
	ck_assert(evdev != NULL);

	libevdev_set_name(evdev, "test device");
	libevdev_enable_event_code(evdev, EV_KEY, BTN_LEFT, NULL);
	libevdev_enable_event_code(evdev, EV_KEY, BTN_RIGHT, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_X, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_Y, NULL);

	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput);
	ck_assert_int_eq(rc, 0);
	libevdev_free(evdev);

	li = libinput_path_create_context(&simple_interface, userdata);
	ck_assert(li != NULL);

	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput));
	ck_assert(device != NULL);

	libinput_suspend(li);
	libinput_resume(li);
	libinput_resume(li);

	libevdev_uinput_destroy(uinput);
	libinput_destroy(li);

	open_func_count = 0;
	close_func_count = 0;
}
END_TEST

START_TEST(path_add_device_suspend_resume)
{
	struct libinput *li;
	struct libinput_device *device;
	struct libinput_event *event;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput1, *uinput2;
	int rc;
	int nevents;
	void *userdata = &rc;

	evdev = libevdev_new();
	ck_assert(evdev != NULL);

	libevdev_set_name(evdev, "test device");
	libevdev_enable_event_code(evdev, EV_KEY, BTN_LEFT, NULL);
	libevdev_enable_event_code(evdev, EV_KEY, BTN_RIGHT, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_X, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_Y, NULL);

	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput1);
	ck_assert_int_eq(rc, 0);

	libevdev_set_name(evdev, "test device 2");
	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput2);
	ck_assert_int_eq(rc, 0);

	libevdev_free(evdev);

	li = libinput_path_create_context(&simple_interface, userdata);
	ck_assert(li != NULL);

	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput1));
	ck_assert(device != NULL);
	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput2));

	libinput_dispatch(li);

	nevents = 0;
	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);
		ck_assert_int_eq(type, LIBINPUT_EVENT_DEVICE_ADDED);
		libinput_event_destroy(event);
		nevents++;
	}

	ck_assert_int_eq(nevents, 2);


	libinput_suspend(li);
	libinput_dispatch(li);

	nevents = 0;
	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);
		ck_assert_int_eq(type, LIBINPUT_EVENT_DEVICE_REMOVED);
		libinput_event_destroy(event);
		nevents++;
	}

	ck_assert_int_eq(nevents, 2);

	libinput_resume(li);
	libinput_dispatch(li);

	nevents = 0;
	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);
		ck_assert_int_eq(type, LIBINPUT_EVENT_DEVICE_ADDED);
		libinput_event_destroy(event);
		nevents++;
	}

	ck_assert_int_eq(nevents, 2);

	libevdev_uinput_destroy(uinput1);
	libevdev_uinput_destroy(uinput2);
	libinput_destroy(li);

	open_func_count = 0;
	close_func_count = 0;
}
END_TEST

START_TEST(path_add_device_suspend_resume_fail)
{
	struct libinput *li;
	struct libinput_device *device;
	struct libinput_event *event;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput1, *uinput2;
	int rc;
	int nevents;
	void *userdata = &rc;

	evdev = libevdev_new();
	ck_assert(evdev != NULL);

	libevdev_set_name(evdev, "test device");
	libevdev_enable_event_code(evdev, EV_KEY, BTN_LEFT, NULL);
	libevdev_enable_event_code(evdev, EV_KEY, BTN_RIGHT, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_X, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_Y, NULL);

	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput1);
	ck_assert_int_eq(rc, 0);

	libevdev_set_name(evdev, "test device 2");
	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput2);
	ck_assert_int_eq(rc, 0);

	libevdev_free(evdev);

	li = libinput_path_create_context(&simple_interface, userdata);
	ck_assert(li != NULL);

	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput1));
	ck_assert(device != NULL);
	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput2));
	ck_assert(device != NULL);

	libinput_dispatch(li);

	nevents = 0;
	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);
		ck_assert_int_eq(type, LIBINPUT_EVENT_DEVICE_ADDED);
		libinput_event_destroy(event);
		nevents++;
	}

	ck_assert_int_eq(nevents, 2);


	libinput_suspend(li);
	libinput_dispatch(li);

	nevents = 0;
	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);
		ck_assert_int_eq(type, LIBINPUT_EVENT_DEVICE_REMOVED);
		libinput_event_destroy(event);
		nevents++;
	}

	ck_assert_int_eq(nevents, 2);

	/* now drop one of the devices */
	libevdev_uinput_destroy(uinput1);
	rc = libinput_resume(li);
	ck_assert_int_eq(rc, -1);

	libinput_dispatch(li);

	nevents = 0;
	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);
		/* We expect one device being added, second one fails,
		 * causing a removed event for the first one */
		if (type != LIBINPUT_EVENT_DEVICE_ADDED &&
		    type != LIBINPUT_EVENT_DEVICE_REMOVED)
			ck_abort();
		libinput_event_destroy(event);
		nevents++;
	}

	ck_assert_int_eq(nevents, 2);

	libevdev_uinput_destroy(uinput2);
	libinput_destroy(li);

	open_func_count = 0;
	close_func_count = 0;
}
END_TEST

START_TEST(path_add_device_suspend_resume_remove_device)
{
	struct libinput *li;
	struct libinput_device *device;
	struct libinput_event *event;
	struct libevdev *evdev;
	struct libevdev_uinput *uinput1, *uinput2;
	int rc;
	int nevents;
	void *userdata = &rc;

	evdev = libevdev_new();
	ck_assert(evdev != NULL);

	libevdev_set_name(evdev, "test device");
	libevdev_enable_event_code(evdev, EV_KEY, BTN_LEFT, NULL);
	libevdev_enable_event_code(evdev, EV_KEY, BTN_RIGHT, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_X, NULL);
	libevdev_enable_event_code(evdev, EV_REL, REL_Y, NULL);

	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput1);
	ck_assert_int_eq(rc, 0);

	libevdev_set_name(evdev, "test device 2");
	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput2);
	ck_assert_int_eq(rc, 0);

	libevdev_free(evdev);

	li = libinput_path_create_context(&simple_interface, userdata);
	ck_assert(li != NULL);

	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput1));
	ck_assert(device != NULL);
	device = libinput_path_add_device(li,
					  libevdev_uinput_get_devnode(uinput2));

	libinput_device_ref(device);
	libinput_dispatch(li);

	nevents = 0;
	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);
		ck_assert_int_eq(type, LIBINPUT_EVENT_DEVICE_ADDED);
		libinput_event_destroy(event);
		nevents++;
	}

	ck_assert_int_eq(nevents, 2);


	libinput_suspend(li);
	libinput_dispatch(li);

	nevents = 0;
	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);
		ck_assert_int_eq(type, LIBINPUT_EVENT_DEVICE_REMOVED);
		libinput_event_destroy(event);
		nevents++;
	}

	ck_assert_int_eq(nevents, 2);

	/* now drop and remove one of the devices */
	libevdev_uinput_destroy(uinput2);
	libinput_path_remove_device(device);
	libinput_device_unref(device);

	rc = libinput_resume(li);
	ck_assert_int_eq(rc, 0);

	libinput_dispatch(li);

	nevents = 0;
	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);
		ck_assert_int_eq(type, LIBINPUT_EVENT_DEVICE_ADDED);
		libinput_event_destroy(event);
		nevents++;
	}

	ck_assert_int_eq(nevents, 1);

	libevdev_uinput_destroy(uinput1);
	libinput_destroy(li);

	open_func_count = 0;
	close_func_count = 0;
}
END_TEST

int main (int argc, char **argv) {

	litest_add("path:create", path_create_NULL, LITEST_ANY, LITEST_ANY);
	litest_add("path:create", path_create_invalid, LITEST_ANY, LITEST_ANY);
	litest_add("path:create", path_create_destroy, LITEST_ANY, LITEST_ANY);
	litest_add("path:suspend", path_suspend, LITEST_ANY, LITEST_ANY);
	litest_add("path:suspend", path_double_suspend, LITEST_ANY, LITEST_ANY);
	litest_add("path:suspend", path_double_resume, LITEST_ANY, LITEST_ANY);
	litest_add("path:suspend", path_add_device_suspend_resume, LITEST_ANY, LITEST_ANY);
	litest_add("path:suspend", path_add_device_suspend_resume_fail, LITEST_ANY, LITEST_ANY);
	litest_add("path:suspend", path_add_device_suspend_resume_remove_device, LITEST_ANY, LITEST_ANY);
	litest_add("path:seat events", path_added_seat, LITEST_ANY, LITEST_ANY);
	litest_add("path:device events", path_added_device, LITEST_ANY, LITEST_ANY);
	litest_add("path:device events", path_device_sysname, LITEST_ANY, LITEST_ANY);
	litest_add("path:device events", path_add_device, LITEST_ANY, LITEST_ANY);
	litest_add("path:device events", path_add_invalid_path, LITEST_ANY, LITEST_ANY);
	litest_add("path:device events", path_remove_device, LITEST_ANY, LITEST_ANY);
	litest_add("path:device events", path_double_remove_device, LITEST_ANY, LITEST_ANY);

	return litest_run(argc, argv);
}