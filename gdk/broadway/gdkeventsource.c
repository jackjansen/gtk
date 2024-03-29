/* GDK - The GIMP Drawing Kit
 * Copyright (C) 2009 Carlos Garnacho <carlosg@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gdkeventsource.h"

#include "gdkinternals.h"

#include <stdlib.h>

static gboolean gdk_event_source_prepare  (GSource     *source,
                                           gint        *timeout);
static gboolean gdk_event_source_check    (GSource     *source);
static gboolean gdk_event_source_dispatch (GSource     *source,
                                           GSourceFunc  callback,
                                           gpointer     user_data);
static void     gdk_event_source_finalize (GSource     *source);

#define HAS_FOCUS(toplevel)                           \
  ((toplevel)->has_focus || (toplevel)->has_pointer_focus)

struct _GdkEventSource
{
  GSource source;

  GdkDisplay *display;
  GPollFD event_poll_fd;
};

static GSourceFuncs event_funcs = {
  gdk_event_source_prepare,
  gdk_event_source_check,
  gdk_event_source_dispatch,
  gdk_event_source_finalize
};

static GList *event_sources = NULL;

static gboolean
gdk_event_source_prepare (GSource *source,
                          gint    *timeout)
{
  GdkDisplay *display = ((GdkEventSource*) source)->display;
  gboolean retval;

  gdk_threads_enter ();

  *timeout = -1;
  retval = (_gdk_event_queue_find_first (display) != NULL);

  gdk_threads_leave ();

  return retval;
}

static gboolean
gdk_event_source_check (GSource *source)
{
  GdkEventSource *event_source = (GdkEventSource*) source;
  gboolean retval;

  gdk_threads_enter ();

  if (event_source->event_poll_fd.revents & G_IO_IN)
    retval = (_gdk_event_queue_find_first (event_source->display) != NULL);
  else
    retval = FALSE;

  gdk_threads_leave ();

  return retval;
}

void
_gdk_broadway_events_got_input (GdkDisplay *display,
				BroadwayInputMsg *message)
{
  GdkBroadwayDisplay *display_broadway = GDK_BROADWAY_DISPLAY (display);
  GdkScreen *screen;
  GdkWindow *window;
  GdkEvent *event = NULL;
  GList *node;

  switch (message->base.type) {
  case 'e': /* Enter */
    display_broadway->last_x = message->pointer.root_x;
    display_broadway->last_y = message->pointer.root_y;
    display_broadway->last_state = message->pointer.state;
    display_broadway->real_mouse_in_toplevel =
      g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->pointer.mouse_window_id));

    window = g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->pointer.event_window_id));

    /* TODO: Unset when it dies */
    display_broadway->mouse_in_toplevel = window;
    if (window)
      {
	event = gdk_event_new (GDK_ENTER_NOTIFY);
	event->crossing.window = g_object_ref (window);
	event->crossing.time = message->base.time;
	event->crossing.x = message->pointer.win_x;
	event->crossing.y = message->pointer.win_y;
	event->crossing.x_root = message->pointer.root_x;
	event->crossing.y_root = message->pointer.root_y;
	event->crossing.state = message->pointer.state;
	event->crossing.mode = message->crossing.mode;
	event->crossing.detail = GDK_NOTIFY_ANCESTOR;
	gdk_event_set_device (event, display->core_pointer);

	node = _gdk_event_queue_append (display, event);
	_gdk_windowing_got_event (display, node, event, message->base.serial);

	event = gdk_event_new (GDK_FOCUS_CHANGE);
	event->focus_change.window = g_object_ref (window);
	event->focus_change.in = TRUE;
	gdk_event_set_device (event, display->core_pointer);

	node = _gdk_event_queue_append (display, event);
	_gdk_windowing_got_event (display, node, event, message->base.serial);
      }
    break;
  case 'l': /* Leave */
    display_broadway->last_x = message->pointer.root_x;
    display_broadway->last_y = message->pointer.root_y;
    display_broadway->last_state = message->pointer.state;
    display_broadway->real_mouse_in_toplevel =
      g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->pointer.mouse_window_id));

    window = g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->pointer.event_window_id));

    display_broadway->mouse_in_toplevel = NULL;
    if (window)
      {
	event = gdk_event_new (GDK_LEAVE_NOTIFY);
	event->crossing.window = g_object_ref (window);
	event->crossing.time = message->base.time;
	event->crossing.x = message->pointer.win_x;
	event->crossing.y = message->pointer.win_y;
	event->crossing.x_root = message->pointer.root_x;
	event->crossing.y_root = message->pointer.root_y;
	event->crossing.state = message->pointer.state;
	event->crossing.mode = message->crossing.mode;
	event->crossing.detail = GDK_NOTIFY_ANCESTOR;
	gdk_event_set_device (event, display->core_pointer);

	node = _gdk_event_queue_append (display, event);
	_gdk_windowing_got_event (display, node, event, message->base.serial);

	event = gdk_event_new (GDK_FOCUS_CHANGE);
	event->focus_change.window = g_object_ref (window);
	event->focus_change.in = FALSE;
	gdk_event_set_device (event, display->core_pointer);

	node = _gdk_event_queue_append (display, event);
	_gdk_windowing_got_event (display, node, event, message->base.serial);
      }
    break;
  case 'm': /* Mouse move */
    display_broadway->last_x = message->pointer.root_x;
    display_broadway->last_y = message->pointer.root_y;
    display_broadway->last_state = message->pointer.state;
    display_broadway->real_mouse_in_toplevel =
      g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->pointer.mouse_window_id));

    if (_gdk_broadway_moveresize_handle_event (display, message))
      break;

    window = g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->pointer.event_window_id));
    if (window)
      {
	event = gdk_event_new (GDK_MOTION_NOTIFY);
	event->motion.window = g_object_ref (window);
	event->motion.time = message->base.time;
	event->motion.x = message->pointer.win_x;
	event->motion.y = message->pointer.win_y;
	event->motion.x_root = message->pointer.root_x;
	event->motion.y_root = message->pointer.root_y;
	event->motion.state = message->pointer.state;
	gdk_event_set_device (event, display->core_pointer);

	node = _gdk_event_queue_append (display, event);
	_gdk_windowing_got_event (display, node, event, message->base.serial);
      }

    break;
  case 'b':
  case 'B':
    display_broadway->last_x = message->pointer.root_x;
    display_broadway->last_y = message->pointer.root_y;
    display_broadway->last_state = message->pointer.state;
    display_broadway->real_mouse_in_toplevel =
      g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->pointer.mouse_window_id));

    if (message->base.type != 'b' &&
	_gdk_broadway_moveresize_handle_event (display, message))
      break;

    window = g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->pointer.event_window_id));
    if (window)
      {
	event = gdk_event_new (message->base.type == 'b' ? GDK_BUTTON_PRESS : GDK_BUTTON_RELEASE);
	event->button.window = g_object_ref (window);
	event->button.time = message->base.time;
	event->button.x = message->pointer.win_x;
	event->button.y = message->pointer.win_y;
	event->button.x_root = message->pointer.root_x;
	event->button.y_root = message->pointer.root_y;
	event->button.button = message->button.button;
	event->button.state = message->pointer.state;
	gdk_event_set_device (event, display->core_pointer);

	node = _gdk_event_queue_append (display, event);
	_gdk_windowing_got_event (display, node, event, message->base.serial);
      }

    break;
  case 's':
    display_broadway->last_x = message->pointer.root_x;
    display_broadway->last_y = message->pointer.root_y;
    display_broadway->last_state = message->pointer.state;
    display_broadway->real_mouse_in_toplevel =
      g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->pointer.mouse_window_id));

    window = g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->pointer.event_window_id));
    if (window)
      {
	event = gdk_event_new (GDK_SCROLL);
	event->scroll.window = g_object_ref (window);
	event->scroll.time = message->base.time;
	event->scroll.x = message->pointer.win_x;
	event->scroll.y = message->pointer.win_y;
	event->scroll.x_root = message->pointer.root_x;
	event->scroll.y_root = message->pointer.root_y;
	event->scroll.direction = message->scroll.dir == 0 ? GDK_SCROLL_UP : GDK_SCROLL_DOWN;
	gdk_event_set_device (event, display->core_pointer);

	node = _gdk_event_queue_append (display, event);
	_gdk_windowing_got_event (display, node, event, message->base.serial);
      }

    break;
  case 'k':
  case 'K':
    window = display_broadway->mouse_in_toplevel;

    if (window)
      {
	event = gdk_event_new (message->base.type == 'k' ? GDK_KEY_PRESS : GDK_KEY_RELEASE);
	event->key.window = g_object_ref (window);
	event->key.time = message->base.time;
	event->key.keyval = message->key.key;
	event->key.state = message->key.state;
	event->key.hardware_keycode = message->key.key;
	event->key.length = 0;
	gdk_event_set_device (event, display->core_pointer);

	display_broadway->last_state = message->key.state;

	node = _gdk_event_queue_append (display, event);
	_gdk_windowing_got_event (display, node, event, message->base.serial);
      }

    break;
  case 'g':
  case 'u':
    _gdk_display_device_grab_update (display, display->core_pointer, NULL, message->base.serial);
    break;

  case 'w':
    window = g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->configure_notify.id));
    if (window)
      {
	window->x = message->configure_notify.x;
	window->y = message->configure_notify.y;
	window->width = message->configure_notify.width;
	window->height = message->configure_notify.height;
	_gdk_window_update_size (window);
	_gdk_broadway_window_resize_surface (window);

	event = gdk_event_new (GDK_CONFIGURE);
	event->configure.window = g_object_ref (window);
	event->configure.x = message->configure_notify.x;
	event->configure.y = message->configure_notify.y;
	event->configure.width = message->configure_notify.width;
	event->configure.height = message->configure_notify.height;

	node = _gdk_event_queue_append (display, event);
	_gdk_windowing_got_event (display, node, event, message->base.serial);

	if (window->resize_count >= 1)
	  {
	    window->resize_count -= 1;

	    if (window->resize_count == 0)
	      _gdk_broadway_moveresize_configure_done (display, window);
	  }
      }
    break;

  case 'W':
    window = g_hash_table_lookup (display_broadway->id_ht, GINT_TO_POINTER (message->delete_notify.id));
    if (window)
      {
	event = gdk_event_new (GDK_DELETE);
	event->any.window = g_object_ref (window);

	node = _gdk_event_queue_append (display, event);
	_gdk_windowing_got_event (display, node, event, message->base.serial);
      }
    break;

  case 'd':
    screen = gdk_display_get_default_screen (display);
    window = gdk_screen_get_root_window (screen);
    window->width = message->screen_resize_notify.width;
    window->height = message->screen_resize_notify.height;

    _gdk_window_update_size (window);
    _gdk_broadway_screen_size_changed (screen, &message->screen_resize_notify);
    break;

  default:
    g_printerr ("Unknown input command %c\n", message->base.type);
    break;
  }
}

void
_gdk_broadway_display_queue_events (GdkDisplay *display)
{
}

static gboolean
gdk_event_source_dispatch (GSource     *source,
                           GSourceFunc  callback,
                           gpointer     user_data)
{
  GdkDisplay *display = ((GdkEventSource*) source)->display;
  GdkEvent *event;

  gdk_threads_enter ();

  event = gdk_display_get_event (display);

  if (event)
    {
      _gdk_event_emit (event);

      gdk_event_free (event);
    }

  gdk_threads_leave ();

  return TRUE;
}

static void
gdk_event_source_finalize (GSource *source)
{
  GdkEventSource *event_source = (GdkEventSource *)source;

  event_sources = g_list_remove (event_sources, event_source);
}

GSource *
_gdk_broadway_event_source_new (GdkDisplay *display)
{
  GSource *source;
  GdkEventSource *event_source;
  char *name;

  source = g_source_new (&event_funcs, sizeof (GdkEventSource));
  name = g_strdup_printf ("GDK Broadway Event source (%s)",
			  gdk_display_get_name (display));
  g_source_set_name (source, name);
  g_free (name);
  event_source = (GdkEventSource *) source;
  event_source->display = display;

  g_source_set_priority (source, GDK_PRIORITY_EVENTS);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  event_sources = g_list_prepend (event_sources, source);

  return source;
}
