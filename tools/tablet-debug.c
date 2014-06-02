/*
 * Copyright © 2014 Lyude
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

#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <termios.h>

#include <linux/input.h>

#include <ncurses.h>
#include <panel.h>

#include <libinput.h>
#include <libudev.h>

#include "tablet-debug.h"
#include "../src/libinput-util.h"

#define bool_to_string(bool_) (bool_ ? "True" : "False")

static struct udev *udev;
static struct libinput *li;
static const char *seat = "seat0";

static int tablet_count = 0;

struct tablet_panel {
	struct list link;
	WINDOW *window;
	PANEL *panel;
	struct libinput_device *dev;

	bool stylus_touching;

	double x;
	double y;

	struct libinput_tool *current_tool;

	double tilt_vertical;
	double tilt_horizontal;

	double pressure;
	double distance;

	enum libinput_button_state stylus_button[2];
	enum libinput_button_state pad_button[7];
};

struct list tablet_panels;
struct tablet_panel *current_panel;

WINDOW *placeholder_window;
PANEL *placeholder_panel;

WINDOW *seat_count_window;
PANEL *seat_count_panel;

short seat_count[KEY_MAX];

static int
open_restricted(const char *path, int flags, void *user_data)
{
	int fd=open(path, flags);
	return fd < 0 ? -errno : fd;
}

static void
close_restricted(int fd, void *user_data)
{
	close(fd);
}

const static struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted
};

static int
open_udev()
{
	printf("Setting up udev...\n");

	udev = udev_new();
	if (!udev) {
		fprintf(stderr, "Failed to initialize udev\n");
		return 1;
	}

	li = libinput_udev_create_for_seat(&interface, NULL, udev, seat);
	if (!li) {
		fprintf(stderr, "Failed to initialize context from udev\n");
		return 1;
	}

	return 0;
}

static inline void
update_display()
{
	update_panels();
	doupdate();
}

static void
update_line(WINDOW * window,
	    int line,
	    const char *format,
	    ...)
{
	va_list args;

	wmove(window, line, 0);

	va_start(args, format);
	vwprintw(window, format, args); 
	va_end(args);

	wclrtoeol(window);
}

static const char *
string_from_tool_type(struct libinput_tool *tool)
{
	const char *tool_str;

	switch (libinput_tool_get_type(tool)) {
	case LIBINPUT_TOOL_NONE:
		tool_str = "None";
		break;
	case LIBINPUT_TOOL_PEN:
		tool_str = "Pen";
		break;
	case LIBINPUT_TOOL_ERASER:
		tool_str = "Eraser";
		break;
	case LIBINPUT_TOOL_BRUSH:
		tool_str = "Brush";
		break;
	case LIBINPUT_TOOL_PENCIL:
		tool_str = "Pencil";
		break;
	case LIBINPUT_TOOL_AIRBRUSH:
		tool_str = "Airbrush";
		break;
	case LIBINPUT_TOOL_FINGER:
		tool_str = "Finger";
		break;
	case LIBINPUT_TOOL_MOUSE:
		tool_str = "Mouse";
		break;
	case LIBINPUT_TOOL_LENS:
		tool_str = "Lens";
		break;
	default:
		tool_str = "???";
		break;
	}

	return tool_str;
}

static void
paint_placeholder_panel()
{
	wclear(placeholder_window);
	wprintw(placeholder_window,
		"There are no currently detected tablets.\n"
		"Make sure your tablet is plugged in, and that you have the "
		"right permissions.");
}

static void
paint_panel(struct tablet_panel *panel)
{
	const char* tool_str;
	int32_t tool_serial;

	wclear(panel->window);
	wresize(panel->window, 0, WIN_TABLET_PANEL_WIDTH);

	mvwprintw(panel->window,
		  TABLET_SYSTEM_NAME_ROW,
		  0,
		  TABLET_SYSTEM_NAME_FIELD,
		  libinput_device_get_sysname(panel->dev));
	mvwprintw(panel->window,
		  TABLET_STYLUS_TOUCHING_ROW,
		  0,
		  TABLET_STYLUS_TOUCHING_FIELD,
		  bool_to_string(panel->stylus_touching));

	if (panel->current_tool != NULL) {
		tool_str = string_from_tool_type(panel->current_tool);
		tool_serial = libinput_tool_get_serial(panel->current_tool);
	} else {
		tool_str = "None";
		tool_serial = 0;
	}

	mvwprintw(panel->window,
		  TABLET_TOOL_NAME_ROW,
		  0,
		  TABLET_TOOL_NAME_FIELD,
		  tool_str);
	mvwprintw(panel->window,
		  TABLET_TOOL_SERIAL_ROW,
		  0,
		  TABLET_TOOL_SERIAL_FIELD,
		  tool_serial);

	mvwprintw(panel->window,
		  TABLET_X_AND_Y_ROW,
		  0,
		  TABLET_X_AND_Y_FIELD,
		  panel->x,
		  panel->y);

	mvwprintw(panel->window,
		  TABLET_TILT_VERTICAL_ROW,
		  0,
		  TABLET_TILT_VERTICAL_FIELD,
		  panel->tilt_vertical);
	mvwprintw(panel->window,
		  TABLET_TILT_HORIZONTAL_ROW,
		  0,
		  TABLET_TILT_HORIZONTAL_FIELD,
		  panel->tilt_horizontal);

	mvwprintw(panel->window,
		  TABLET_DISTANCE_ROW,
		  0,
		  TABLET_DISTANCE_FIELD,
		  panel->distance);
	mvwprintw(panel->window,
		  TABLET_PRESSURE_ROW,
		  0,
		  TABLET_PRESSURE_FIELD,
		  panel->pressure);

	mvwprintw(panel->window,
		  TABLET_STYLUS_BUTTONS_ROW,
		  0,
		  TABLET_STYLUS_BUTTONS_FIELD,
		  bool_to_string(panel->stylus_button[0]),
		  bool_to_string(panel->stylus_button[1]));
	mvwprintw(panel->window,
		  TABLET_PAD_BUTTONS_1_TO_3_ROW,
		  0,
		  TABLET_PAD_BUTTONS_1_TO_3_FIELD,
		  bool_to_string(panel->pad_button[0]),
		  bool_to_string(panel->pad_button[1]),
		  bool_to_string(panel->pad_button[2]));
	mvwprintw(panel->window,
		  TABLET_PAD_BUTTONS_4_TO_6_ROW,
		  0,
		  TABLET_PAD_BUTTONS_4_TO_6_FIELD,
		  bool_to_string(panel->pad_button[3]),
		  bool_to_string(panel->pad_button[4]),
		  bool_to_string(panel->pad_button[5]));
	mvwprintw(panel->window,
		  TABLET_PAD_BUTTON_7_ROW,
		  0,
		  TABLET_PAD_BUTTON_7_FIELD,
		  bool_to_string(panel->pad_button[6]));
}

static void
paint_seat_count_window()
{
	wclear(seat_count_window);
	wresize(seat_count_window, 0, WIN_SEAT_COUNT_WIDTH);

	wprintw(seat_count_window, "Button seat counts:");
	mvwprintw(seat_count_window,
		  SEAT_COUNT_BTN_TOUCH_ROW,
		  0,
		  SEAT_COUNT_BTN_TOUCH_FIELD,
		  seat_count[BTN_TOUCH]);
	mvwprintw(seat_count_window,
		  SEAT_COUNT_STYLUS_BUTTONS_ROW,
		  0,
		  SEAT_COUNT_STYLUS_BUTTONS_FIELD,
		  seat_count[BTN_STYLUS],
		  seat_count[BTN_STYLUS2]);
	mvwprintw(seat_count_window,
		  SEAT_COUNT_PAD_BUTTONS_1_TO_3_ROW,
		  0,
		  SEAT_COUNT_PAD_BUTTONS_1_TO_3_FIELD,
		  seat_count[BTN_0],
		  seat_count[BTN_1],
		  seat_count[BTN_2]);
	mvwprintw(seat_count_window,
		  SEAT_COUNT_PAD_BUTTONS_4_TO_6_ROW,
		  0,
		  SEAT_COUNT_PAD_BUTTONS_4_TO_6_FIELD,
		  seat_count[BTN_3],
		  seat_count[BTN_4],
		  seat_count[BTN_5]);
	mvwprintw(seat_count_window,
		  SEAT_COUNT_PAD_BUTTON_7_ROW,
		  0,
		  SEAT_COUNT_PAD_BUTTON_7_FIELD,
		  seat_count[BTN_6]);
}

static struct tablet_panel *
new_tablet_panel(struct libinput_device *dev)
{
	struct tablet_panel *panel;

	panel = zalloc(sizeof(struct tablet_panel));
	panel->window = newwin(0,
			       WIN_TABLET_PANEL_WIDTH,
			       0,
			       WIN_TABLET_PANEL_BEGIN_X);
	panel->panel = new_panel(panel->window);
	panel->dev = dev;

	set_panel_userptr(panel->panel, panel);
	list_insert(&tablet_panels, &panel->link);

	return panel;
}

static void
tablet_panel_destroy(struct tablet_panel *panel)
{
	del_panel(panel->panel);
	delwin(panel->window);
	list_remove(&panel->link);
	
	if (panel->current_tool)
		libinput_tool_unref(panel->current_tool);

	free(panel);
}

static void
next_panel()
{
	struct tablet_panel *next_panel;

	if (current_panel->link.next == &tablet_panels)
		next_panel = container_of(tablet_panels.next,
					  current_panel,
					  link);
	else
		next_panel = container_of(current_panel->link.next,
					  current_panel,
					  link);

	show_panel(next_panel->panel);
	current_panel = next_panel;
}

static void
prev_panel()
{
	struct tablet_panel *prev_panel;

	if (current_panel->link.prev == &tablet_panels)
		prev_panel = container_of(tablet_panels.prev,
					  current_panel,
					  link);
	else
		prev_panel = container_of(current_panel->link.prev,
					  current_panel,
					  link);
	show_panel(prev_panel->panel);
	current_panel = prev_panel;
}

static void
handle_new_device(struct libinput_event *ev,
		  struct libinput_device *dev)
{
	struct tablet_panel *panel;

	libinput_device_ref(dev);

	panel = new_tablet_panel(dev);
	bottom_panel(panel->panel);
	paint_panel(panel);

	/* If this is our only tablet, get rid of the placeholder panel */
	if (++tablet_count == 1) {
		hide_panel(placeholder_panel);
		current_panel = panel;
		update_display();
	}

	libinput_device_set_user_data(dev, panel);
}

static void
handle_removed_device(struct libinput_event *ev,
		      struct libinput_device *dev)
{
	struct tablet_panel *panel = libinput_device_get_user_data(dev);

	if (--tablet_count == 0) {
		show_panel(placeholder_panel);
		update_display();
	}
	else if (current_panel == panel)
		next_panel();

	tablet_panel_destroy(libinput_device_get_user_data(dev));
	libinput_device_unref(dev);
}

static void
handle_tool_update(struct libinput_event_tablet *ev,
		   struct libinput_device *dev)
{
	struct tablet_panel *panel;
	struct libinput_tool *tool;

	panel = libinput_device_get_user_data(dev);
	tool = libinput_event_tablet_get_tool(ev);

	update_line(panel->window,
		    TABLET_TOOL_NAME_ROW,
		    TABLET_TOOL_NAME_FIELD,
		    string_from_tool_type(tool));
	update_line(panel->window,
		    TABLET_TOOL_SERIAL_ROW,
		    TABLET_TOOL_SERIAL_FIELD,
		    libinput_tool_get_serial(tool));

	if (panel->current_tool)
		libinput_tool_unref(panel->current_tool);

	panel->current_tool = tool;
	libinput_tool_ref(tool);
}

static void
handle_axis_update(struct libinput_event_tablet *ev,
		   struct libinput_device *dev)
{
	struct tablet_panel *panel;

	panel = libinput_device_get_user_data(dev);

	int a;
	for (a = 0; a <= LIBINPUT_TABLET_AXIS_CNT; a++) {
		int row;
		const char * field;
		double value;

		if (!libinput_event_tablet_axis_has_changed(ev, a))
			continue;

		value = libinput_event_tablet_get_axis_value(ev, a);

		switch (a) {
		case LIBINPUT_TABLET_AXIS_X:
			update_line(panel->window,
				    TABLET_X_AND_Y_ROW,
				    TABLET_X_AND_Y_FIELD,
				    value,
				    panel->y);
			panel->x = value;
			continue;
			break;
		case LIBINPUT_TABLET_AXIS_Y:
			update_line(panel->window,
				    TABLET_X_AND_Y_ROW,
				    TABLET_X_AND_Y_FIELD,
				    panel->x,
				    value);
			panel->y = value;
			continue;
			break;
		case LIBINPUT_TABLET_AXIS_TILT_VERTICAL:
			row = TABLET_TILT_VERTICAL_ROW;
			field = TABLET_TILT_VERTICAL_FIELD;
			panel->tilt_vertical = value;
			break;
		case LIBINPUT_TABLET_AXIS_TILT_HORIZONTAL:
			row = TABLET_TILT_HORIZONTAL_ROW;
			field = TABLET_TILT_HORIZONTAL_FIELD;
			panel->tilt_horizontal = value;
			break;
		case LIBINPUT_TABLET_AXIS_DISTANCE:
			row = TABLET_DISTANCE_ROW;
			field = TABLET_DISTANCE_FIELD;
			panel->distance = value;
			break;
		case LIBINPUT_TABLET_AXIS_PRESSURE:
			row = TABLET_PRESSURE_ROW;
			field = TABLET_PRESSURE_FIELD;
			panel->pressure = value;
			break;
		}
		update_line(panel->window, row, field, value);
	}
}

static void
handle_button_update(struct libinput_event_tablet* ev,
		     struct libinput_device *dev)
{
	struct tablet_panel *panel;
	uint32_t button;
	enum libinput_button_state state;
	short button_seat_count;

	panel = libinput_device_get_user_data(dev);
	button = libinput_event_tablet_get_button(ev);
	state = libinput_event_tablet_get_button_state(ev);
	button_seat_count = libinput_event_tablet_get_seat_button_count(ev);

	switch(button) {
	case BTN_TOUCH:
		update_line(panel->window,
			    TABLET_STYLUS_TOUCHING_ROW,
			    TABLET_STYLUS_TOUCHING_FIELD,
			    bool_to_string(state));
		update_line(seat_count_window,
			    SEAT_COUNT_BTN_TOUCH_ROW,
			    SEAT_COUNT_BTN_TOUCH_FIELD,
			    button_seat_count);

		panel->stylus_touching = state;
		seat_count[BTN_TOUCH] = button_seat_count;
		break;
	case BTN_STYLUS:
		update_line(panel->window,
			    TABLET_STYLUS_BUTTONS_ROW,
			    TABLET_STYLUS_BUTTONS_FIELD,
			    bool_to_string(state),
			    bool_to_string(panel->stylus_button[1]));
		update_line(seat_count_window,
			    SEAT_COUNT_STYLUS_BUTTONS_ROW,
			    SEAT_COUNT_STYLUS_BUTTONS_FIELD,
			    button_seat_count,
			    seat_count[BTN_STYLUS2]);

		panel->stylus_button[0] = state;
		seat_count[BTN_STYLUS] = button_seat_count;
		break;
	case BTN_STYLUS2:
		update_line(panel->window,
			    TABLET_STYLUS_BUTTONS_ROW,
			    TABLET_STYLUS_BUTTONS_FIELD,
			    bool_to_string(panel->stylus_button[0]),
			    bool_to_string(state));
		update_line(seat_count_window,
			    SEAT_COUNT_STYLUS_BUTTONS_ROW,
			    SEAT_COUNT_STYLUS_BUTTONS_FIELD,
			    seat_count[BTN_STYLUS],
			    button_seat_count);

		panel->stylus_button[1] = state;
		seat_count[BTN_STYLUS2] = button_seat_count;
		break;
	case BTN_0:
		update_line(panel->window,
			    TABLET_PAD_BUTTONS_1_TO_3_ROW,
			    TABLET_PAD_BUTTONS_1_TO_3_FIELD,
			    bool_to_string(state),
			    bool_to_string(panel->pad_button[1]),
			    bool_to_string(panel->pad_button[2]));
		update_line(seat_count_window,
			    SEAT_COUNT_PAD_BUTTONS_1_TO_3_ROW,
			    SEAT_COUNT_PAD_BUTTONS_1_TO_3_FIELD,
			    button_seat_count,
			    seat_count[BTN_1],
			    seat_count[BTN_2]);

		panel->pad_button[0] = state;
		seat_count[BTN_0] = button_seat_count;
		break;
	case BTN_1:
		update_line(panel->window,
			    TABLET_PAD_BUTTONS_1_TO_3_ROW,
			    TABLET_PAD_BUTTONS_1_TO_3_FIELD,
			    bool_to_string(panel->pad_button[0]),
			    bool_to_string(state),
			    bool_to_string(panel->pad_button[2]));
		update_line(seat_count_window,
			    SEAT_COUNT_PAD_BUTTONS_1_TO_3_ROW,
			    SEAT_COUNT_PAD_BUTTONS_1_TO_3_FIELD,
			    seat_count[BTN_0],
			    button_seat_count,
			    seat_count[BTN_2]);

		panel->pad_button[1] = state;
		seat_count[BTN_1] = button_seat_count;
		break;
	case BTN_2:
		update_line(panel->window,
			    TABLET_PAD_BUTTONS_1_TO_3_ROW,
			    TABLET_PAD_BUTTONS_1_TO_3_FIELD,
			    bool_to_string(panel->pad_button[0]),
			    bool_to_string(panel->pad_button[1]),
			    bool_to_string(state));
		update_line(seat_count_window,
			    SEAT_COUNT_PAD_BUTTONS_1_TO_3_ROW,
			    SEAT_COUNT_PAD_BUTTONS_1_TO_3_FIELD,
			    seat_count[BTN_0],
			    seat_count[BTN_1],
			    button_seat_count);

		panel->pad_button[2] = state;
		seat_count[BTN_2] = button_seat_count;
		break;
	case BTN_3:
		update_line(panel->window,
			    TABLET_PAD_BUTTONS_4_TO_6_ROW,
			    TABLET_PAD_BUTTONS_4_TO_6_FIELD,
			    bool_to_string(state),
			    bool_to_string(panel->pad_button[4]),
			    bool_to_string(panel->pad_button[5]));
		update_line(seat_count_window,
			    SEAT_COUNT_PAD_BUTTONS_4_TO_6_ROW,
			    SEAT_COUNT_PAD_BUTTONS_4_TO_6_FIELD,
			    button_seat_count,
			    seat_count[BTN_4],
			    seat_count[BTN_5]);

		panel->pad_button[3] = state;
		seat_count[BTN_3] = button_seat_count;
		break;
	case BTN_4:
		update_line(panel->window,
			    TABLET_PAD_BUTTONS_4_TO_6_ROW,
			    TABLET_PAD_BUTTONS_4_TO_6_FIELD,
			    bool_to_string(panel->pad_button[3]),
			    bool_to_string(state),
			    bool_to_string(panel->pad_button[5]));
		update_line(seat_count_window,
			    SEAT_COUNT_PAD_BUTTONS_4_TO_6_ROW,
			    SEAT_COUNT_PAD_BUTTONS_4_TO_6_FIELD,
			    seat_count[BTN_3],
			    button_seat_count,
			    seat_count[BTN_5]);

		panel->pad_button[4] = state;
		seat_count[BTN_4] = button_seat_count;
		break;
	case BTN_5:
		update_line(panel->window,
			    TABLET_PAD_BUTTONS_4_TO_6_ROW,
			    TABLET_PAD_BUTTONS_4_TO_6_FIELD,
			    bool_to_string(panel->pad_button[3]),
			    bool_to_string(panel->pad_button[4]),
			    bool_to_string(state));
		update_line(seat_count_window,
			    SEAT_COUNT_PAD_BUTTONS_4_TO_6_ROW,
			    SEAT_COUNT_PAD_BUTTONS_4_TO_6_FIELD,
			    seat_count[BTN_3],
			    seat_count[BTN_4],
			    button_seat_count);

		panel->pad_button[5] = state;
		seat_count[BTN_5] = button_seat_count;
		break;
	case BTN_6:
		update_line(panel->window,
			    TABLET_PAD_BUTTON_7_ROW,
			    TABLET_PAD_BUTTON_7_FIELD,
			    bool_to_string(state));
		update_line(seat_count_window,
			    SEAT_COUNT_PAD_BUTTON_7_ROW,
			    SEAT_COUNT_PAD_BUTTON_7_FIELD,
			    button_seat_count);

		panel->pad_button[6] = state;
		seat_count[BTN_6] = button_seat_count;
		break;
	default:
		return;
	}

	seat_count[button] = button_seat_count;
}

static int
handle_tablet_events()
{
	struct libinput_event *ev;

	libinput_dispatch(li);
	while ((ev = libinput_get_event(li))) {
		struct libinput_device *dev = libinput_event_get_device(ev);

		if (!libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_STYLUS))
			continue;

		switch (libinput_event_get_type(ev)) {
		case LIBINPUT_EVENT_NONE:
			abort();
		case LIBINPUT_EVENT_DEVICE_ADDED:
			handle_new_device(ev, dev);
			break;
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			handle_removed_device(ev, dev);
			break;
		case LIBINPUT_EVENT_TABLET_TOOL_UPDATE:
			handle_tool_update(libinput_event_get_tablet_event(ev),
					   dev);
			break;
		case LIBINPUT_EVENT_TABLET_AXIS:
			handle_axis_update(libinput_event_get_tablet_event(ev),
					   dev);
			break;
		case LIBINPUT_EVENT_TABLET_BUTTON:
			handle_button_update(libinput_event_get_tablet_event(ev),
					     dev);
			break;
		default:
			break;
		}
	}
	
	return 0;
}

static void inline
refresh_current_panel()
{
	clear();
	update_display();
}

static void
mainloop()
{
	struct pollfd fds[3];
	sigset_t mask;

	fds[0].fd = libinput_get_fd(li);
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGWINCH);

	fds[1].fd = signalfd(-1, &mask, SFD_NONBLOCK);
	fds[1].events = POLLIN;
	fds[1].revents = 0;

	fds[2].fd = 0;
	fds[2].events = POLLIN;
	fds[2].revents = 0;

	if (fds[1].fd == -1 ||
	    sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		fprintf(stderr,
			"Failed to set up signal handling (%s)\n",
			strerror(errno));
	}

	/* Create the placeholder panel */
	placeholder_window = newwin(0, 0, 0, 0);
	placeholder_panel = new_panel(placeholder_window);
	paint_placeholder_panel();

	/* Setup the seat count window */
	seat_count_window = newwin(0,
				   WIN_SEAT_COUNT_WIDTH,
				   0,
				   WIN_SEAT_COUNT_BEGIN_X);
	seat_count_panel = new_panel(seat_count_window);
	paint_seat_count_window();

	update_display();

	/* Handle already-pending device added events */
	if (handle_tablet_events())
		fprintf(stderr,
			"Expected device added events on startup but got none. "
			"Maybe you don't have the right permissions?\n");

	while (poll(fds, 3, -1) > -1) {
		if (fds[1].revents) {
			int sig;
			struct tablet_panel *panel;
			
			/* Check what type of signal is waiting */
			sigwait(&mask, &sig);

			if (sig == SIGINT)
				break;

			endwin();
			refresh();

			/* Resize all of the panels and their windows (if we
			 * have any) */
			if (tablet_count != 0) {
				list_for_each(panel, &tablet_panels, link) {
					paint_panel(panel);
				}
			}
			paint_placeholder_panel();
			paint_seat_count_window();

			update_display();
		} else if (fds[2].revents) {
			switch(getch()) {
			case 'q':
			case 27: /* Escape */
				goto exit_mainloop;
			case 'r':
			case KEY_F(5):
				refresh_current_panel();
				break;
			case KEY_LEFT:
				if (tablet_count != 0) {
					prev_panel();
					update_display();
				}
				break;
			case KEY_RIGHT:
				if (tablet_count != 0) {
					next_panel();
					update_display();
				}
				break;
			}
		} else {
			handle_tablet_events();
			update_display();
		}
	}
exit_mainloop:

	close(fds[1].fd);
}

int
main()
{
	printf("libinput tablet debugger\n"
	       "©2014 Lyude <thatslyude@gmail.com>\n"
	       "This is free software; see the source for copying conditions. "
	       "There is NO warranty; not even for MERCHANTABILITY or FITNESS "
	       "FOR A PARTICULAR PURPOSE.\n");

	if (open_udev() == 1) {
		fprintf(stderr, "Cannot continue.\n");
		return 1;
	}

	/* setup ncurses */
	setenv("ESCDELAY", "25", 0);
	initscr();
	keypad(stdscr, true);
	noecho();
	clear();

	list_init(&tablet_panels);

	mainloop();

	endwin();
	udev_unref(udev);

	return 0;	
}