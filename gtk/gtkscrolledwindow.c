/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

#include "config.h"

#include "gtkscrolledwindow.h"

#include "gtkadjustment.h"
#include "gtkbindings.h"
#include "gtkdnd.h"
#include "gtkintl.h"
#include "gtkmain.h"
#include "gtkmarshalers.h"
#include "gtkprivate.h"
#include "gtkscrollable.h"
#include "gtkscrollbar.h"
#include "gtktypebuiltins.h"
#include "gtkviewport.h"
#include "gtkwidgetprivate.h"
#include "gtkwindow.h"
#include "a11y/gtkscrolledwindowaccessible.h"

#include <math.h>

/**
 * SECTION:gtkscrolledwindow
 * @Short_description: Adds scrollbars to its child widget
 * @Title: GtkScrolledWindow
 * @See_also: #GtkScrollable, #GtkViewport, #GtkAdjustment
 *
 * #GtkScrolledWindow is a #GtkBin subclass: it's a container
 * the accepts a single child widget. #GtkScrolledWindow adds scrollbars
 * to the child widget and optionally draws a beveled frame around the
 * child widget.
 *
 * The scrolled window can work in two ways. Some widgets have native
 * scrolling support; these widgets implement the #GtkScrollable interface.
 * Widgets with native scroll support include #GtkTreeView, #GtkTextView,
 * and #GtkLayout.
 *
 * For widgets that lack native scrolling support, the #GtkViewport
 * widget acts as an adaptor class, implementing scrollability for child
 * widgets that lack their own scrolling capabilities. Use #GtkViewport
 * to scroll child widgets such as #GtkGrid, #GtkBox, and so on.
 *
 * If a widget has native scrolling abilities, it can be added to the
 * #GtkScrolledWindow with gtk_container_add(). If a widget does not, you
 * must first add the widget to a #GtkViewport, then add the #GtkViewport
 * to the scrolled window. The convenience function
 * gtk_scrolled_window_add_with_viewport() does exactly this, so you can
 * ignore the presence of the viewport.
 *
 * The position of the scrollbars is controlled by the scroll
 * adjustments. See #GtkAdjustment for the fields in an adjustment - for
 * #GtkScrollbar, used by #GtkScrolledWindow, the "value" field
 * represents the position of the scrollbar, which must be between the
 * "lower" field and "upper - page_size." The "page_size" field
 * represents the size of the visible scrollable area. The
 * "step_increment" and "page_increment" fields are used when the user
 * asks to step down (using the small stepper arrows) or page down (using
 * for example the PageDown key).
 *
 * If a #GtkScrolledWindow doesn't behave quite as you would like, or
 * doesn't have exactly the right layout, it's very possible to set up
 * your own scrolling with #GtkScrollbar and for example a #GtkGrid.
 */


/* scrolled window policy and size requisition handling:
 *
 * gtk size requisition works as follows:
 *   a widget upon size-request reports the width and height that it finds
 *   to be best suited to display its contents, including children.
 *   the width and/or height reported from a widget upon size requisition
 *   may be overidden by the user by specifying a width and/or height
 *   other than 0 through gtk_widget_set_size_request().
 *
 * a scrolled window needs (for implementing all three policy types) to
 * request its width and height based on two different rationales.
 * 1)   the user wants the scrolled window to just fit into the space
 *      that it gets allocated for a specifc dimension.
 * 1.1) this does not apply if the user specified a concrete value
 *      value for that specific dimension by either specifying usize for the
 *      scrolled window or for its child.
 * 2)   the user wants the scrolled window to take as much space up as
 *      is desired by the child for a specifc dimension (i.e. POLICY_NEVER).
 *
 * also, kinda obvious:
 * 3)   a user would certainly not have choosen a scrolled window as a container
 *      for the child, if the resulting allocation takes up more space than the
 *      child would have allocated without the scrolled window.
 *
 * conclusions:
 * A) from 1) follows: the scrolled window shouldn't request more space for a
 *    specifc dimension than is required at minimum.
 * B) from 1.1) follows: the requisition may be overidden by usize of the scrolled
 *    window (done automatically) or by usize of the child (needs to be checked).
 * C) from 2) follows: for POLICY_NEVER, the scrolled window simply reports the
 *    child's dimension.
 * D) from 3) follows: the scrolled window child's minimum width and minimum height
 *    under A) at least correspond to the space taken up by its scrollbars.
 */

#define DEFAULT_SCROLLBAR_SPACING  3
#define TOUCH_BYPASS_CAPTURED_THRESHOLD 30

/* Kinetic scrolling */
#define FRAME_INTERVAL (1000 / 60)
#define MAX_OVERSHOOT_DISTANCE 50
#define FRICTION_DECELERATION 0.003
#define OVERSHOOT_INVERSE_ACCELERATION 0.003
#define RELEASE_EVENT_TIMEOUT 1000

struct _GtkScrolledWindowPrivate
{
  GtkWidget     *hscrollbar;
  GtkWidget     *vscrollbar;

  GtkCornerType  real_window_placement;
  guint16  shadow_type;

  guint    window_placement_set   : 1;
  guint    hscrollbar_policy      : 2;
  guint    vscrollbar_policy      : 2;
  guint    hscrollbar_visible     : 1;
  guint    vscrollbar_visible     : 1;
  guint    window_placement       : 2;
  guint    focus_out              : 1; /* Flag used by ::move-focus-out implementation */

  gint     min_content_width;
  gint     min_content_height;

  /* Kinetic scrolling */
  GdkEvent              *button_press_event;
  GdkWindow             *overshoot_window;
  GdkDevice             *drag_device;
  guint                  kinetic_scrolling         : 1;
  guint                  capture_button_press      : 1;
  guint                  in_drag                   : 1;
  guint                  last_button_event_valid   : 1;

  guint                  release_timeout_id;
  guint                  deceleration_id;

  gdouble                last_button_event_x_root;
  gdouble                last_button_event_y_root;

  gdouble                last_motion_event_x_root;
  gdouble                last_motion_event_y_root;
  guint32                last_motion_event_time;

  gdouble                x_velocity;
  gdouble                y_velocity;

  gdouble                unclamped_hadj_value;
  gdouble                unclamped_vadj_value;
};

typedef struct
{
  GtkScrolledWindow     *scrolled_window;
  gint64                 last_deceleration_time;

  gdouble                x_velocity;
  gdouble                y_velocity;
  gdouble                vel_cosine;
  gdouble                vel_sine;
} KineticScrollData;

enum {
  PROP_0,
  PROP_HADJUSTMENT,
  PROP_VADJUSTMENT,
  PROP_HSCROLLBAR_POLICY,
  PROP_VSCROLLBAR_POLICY,
  PROP_WINDOW_PLACEMENT,
  PROP_WINDOW_PLACEMENT_SET,
  PROP_SHADOW_TYPE,
  PROP_MIN_CONTENT_WIDTH,
  PROP_MIN_CONTENT_HEIGHT,
  PROP_KINETIC_SCROLLING
};

/* Signals */
enum
{
  SCROLL_CHILD,
  MOVE_FOCUS_OUT,
  LAST_SIGNAL
};

static void     gtk_scrolled_window_set_property       (GObject           *object,
                                                        guint              prop_id,
                                                        const GValue      *value,
                                                        GParamSpec        *pspec);
static void     gtk_scrolled_window_get_property       (GObject           *object,
                                                        guint              prop_id,
                                                        GValue            *value,
                                                        GParamSpec        *pspec);

static void     gtk_scrolled_window_destroy            (GtkWidget         *widget);
static void     gtk_scrolled_window_screen_changed     (GtkWidget         *widget,
                                                        GdkScreen         *previous_screen);
static gboolean gtk_scrolled_window_draw               (GtkWidget         *widget,
                                                        cairo_t           *cr);
static void     gtk_scrolled_window_size_allocate      (GtkWidget         *widget,
                                                        GtkAllocation     *allocation);
static gboolean gtk_scrolled_window_scroll_event       (GtkWidget         *widget,
                                                        GdkEventScroll    *event);
static gboolean gtk_scrolled_window_captured_event     (GtkWidget         *widget,
                                                        GdkEvent          *event);
static gboolean gtk_scrolled_window_focus              (GtkWidget         *widget,
                                                        GtkDirectionType   direction);
static void     gtk_scrolled_window_add                (GtkContainer      *container,
                                                        GtkWidget         *widget);
static void     gtk_scrolled_window_remove             (GtkContainer      *container,
                                                        GtkWidget         *widget);
static void     gtk_scrolled_window_forall             (GtkContainer      *container,
                                                        gboolean           include_internals,
                                                        GtkCallback        callback,
                                                        gpointer           callback_data);
static gboolean gtk_scrolled_window_scroll_child       (GtkScrolledWindow *scrolled_window,
                                                        GtkScrollType      scroll,
                                                        gboolean           horizontal);
static void     gtk_scrolled_window_move_focus_out     (GtkScrolledWindow *scrolled_window,
                                                        GtkDirectionType   direction_type);

static void     gtk_scrolled_window_relative_allocation(GtkWidget         *widget,
                                                        GtkAllocation     *allocation);
static void     gtk_scrolled_window_adjustment_changed (GtkAdjustment     *adjustment,
                                                        gpointer           data);
static void     gtk_scrolled_window_adjustment_value_changed (GtkAdjustment     *adjustment,
                                                              gpointer           data);

static void  gtk_scrolled_window_update_real_placement (GtkScrolledWindow *scrolled_window);

static void  gtk_scrolled_window_get_preferred_width   (GtkWidget           *widget,
							gint                *minimum_size,
							gint                *natural_size);
static void  gtk_scrolled_window_get_preferred_height  (GtkWidget           *widget,
							gint                *minimum_size,
							gint                *natural_size);
static void  gtk_scrolled_window_get_preferred_height_for_width  (GtkWidget           *layout,
							gint                 width,
							gint                *minimum_height,
							gint                *natural_height);
static void  gtk_scrolled_window_get_preferred_width_for_height  (GtkWidget           *layout,
							gint                 width,
							gint                *minimum_height,
							gint                *natural_height);

static void  gtk_scrolled_window_realize               (GtkWidget           *widget);
static void  gtk_scrolled_window_unrealize             (GtkWidget           *widget);
static void  gtk_scrolled_window_map                   (GtkWidget           *widget);
static void  gtk_scrolled_window_unmap                 (GtkWidget           *widget);

static void  gtk_scrolled_window_grab_notify           (GtkWidget           *widget,
                                                        gboolean             was_grabbed);

static gboolean _gtk_scrolled_window_set_adjustment_value      (GtkScrolledWindow *scrolled_window,
                                                                GtkAdjustment     *adjustment,
                                                                gdouble            value,
                                                                gboolean           allow_overshooting,
                                                                gboolean           snap_to_border);

static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE (GtkScrolledWindow, gtk_scrolled_window, GTK_TYPE_BIN)


static void
add_scroll_binding (GtkBindingSet  *binding_set,
		    guint           keyval,
		    GdkModifierType mask,
		    GtkScrollType   scroll,
		    gboolean        horizontal)
{
  guint keypad_keyval = keyval - GDK_KEY_Left + GDK_KEY_KP_Left;
  
  gtk_binding_entry_add_signal (binding_set, keyval, mask,
                                "scroll-child", 2,
                                GTK_TYPE_SCROLL_TYPE, scroll,
				G_TYPE_BOOLEAN, horizontal);
  gtk_binding_entry_add_signal (binding_set, keypad_keyval, mask,
                                "scroll-child", 2,
                                GTK_TYPE_SCROLL_TYPE, scroll,
				G_TYPE_BOOLEAN, horizontal);
}

static void
add_tab_bindings (GtkBindingSet    *binding_set,
		  GdkModifierType   modifiers,
		  GtkDirectionType  direction)
{
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_Tab, modifiers,
                                "move-focus-out", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Tab, modifiers,
                                "move-focus-out", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
}

static void
gtk_scrolled_window_class_init (GtkScrolledWindowClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;
  GtkBindingSet *binding_set;

  widget_class = (GtkWidgetClass*) class;
  container_class = (GtkContainerClass*) class;

  gobject_class->set_property = gtk_scrolled_window_set_property;
  gobject_class->get_property = gtk_scrolled_window_get_property;

  widget_class->destroy = gtk_scrolled_window_destroy;
  widget_class->screen_changed = gtk_scrolled_window_screen_changed;
  widget_class->draw = gtk_scrolled_window_draw;
  widget_class->size_allocate = gtk_scrolled_window_size_allocate;
  widget_class->scroll_event = gtk_scrolled_window_scroll_event;
  widget_class->focus = gtk_scrolled_window_focus;
  widget_class->get_preferred_width = gtk_scrolled_window_get_preferred_width;
  widget_class->get_preferred_height = gtk_scrolled_window_get_preferred_height;
  widget_class->get_preferred_height_for_width = gtk_scrolled_window_get_preferred_height_for_width;
  widget_class->get_preferred_width_for_height = gtk_scrolled_window_get_preferred_width_for_height;
  widget_class->realize = gtk_scrolled_window_realize;
  widget_class->unrealize = gtk_scrolled_window_unrealize;
  widget_class->map = gtk_scrolled_window_map;
  widget_class->unmap = gtk_scrolled_window_unmap;
  widget_class->grab_notify = gtk_scrolled_window_grab_notify;

  container_class->add = gtk_scrolled_window_add;
  container_class->remove = gtk_scrolled_window_remove;
  container_class->forall = gtk_scrolled_window_forall;
  gtk_container_class_handle_border_width (container_class);

  class->scrollbar_spacing = -1;

  class->scroll_child = gtk_scrolled_window_scroll_child;
  class->move_focus_out = gtk_scrolled_window_move_focus_out;
  
  g_object_class_install_property (gobject_class,
				   PROP_HADJUSTMENT,
				   g_param_spec_object ("hadjustment",
							P_("Horizontal Adjustment"),
							P_("The GtkAdjustment for the horizontal position"),
							GTK_TYPE_ADJUSTMENT,
							GTK_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class,
				   PROP_VADJUSTMENT,
				   g_param_spec_object ("vadjustment",
							P_("Vertical Adjustment"),
							P_("The GtkAdjustment for the vertical position"),
							GTK_TYPE_ADJUSTMENT,
							GTK_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class,
                                   PROP_HSCROLLBAR_POLICY,
                                   g_param_spec_enum ("hscrollbar-policy",
                                                      P_("Horizontal Scrollbar Policy"),
                                                      P_("When the horizontal scrollbar is displayed"),
						      GTK_TYPE_POLICY_TYPE,
						      GTK_POLICY_AUTOMATIC,
                                                      GTK_PARAM_READABLE | GTK_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_VSCROLLBAR_POLICY,
                                   g_param_spec_enum ("vscrollbar-policy",
                                                      P_("Vertical Scrollbar Policy"),
                                                      P_("When the vertical scrollbar is displayed"),
						      GTK_TYPE_POLICY_TYPE,
						      GTK_POLICY_AUTOMATIC,
                                                      GTK_PARAM_READABLE | GTK_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
                                   PROP_WINDOW_PLACEMENT,
                                   g_param_spec_enum ("window-placement",
                                                      P_("Window Placement"),
                                                      P_("Where the contents are located with respect to the scrollbars. This property only takes effect if \"window-placement-set\" is TRUE."),
						      GTK_TYPE_CORNER_TYPE,
						      GTK_CORNER_TOP_LEFT,
                                                      GTK_PARAM_READABLE | GTK_PARAM_WRITABLE));
  
  /**
   * GtkScrolledWindow:window-placement-set:
   *
   * Whether "window-placement" should be used to determine the location 
   * of the contents with respect to the scrollbars. Otherwise, the 
   * "gtk-scrolled-window-placement" setting is used.
   *
   * Since: 2.10
   */
  g_object_class_install_property (gobject_class,
                                   PROP_WINDOW_PLACEMENT_SET,
                                   g_param_spec_boolean ("window-placement-set",
					   		 P_("Window Placement Set"),
							 P_("Whether \"window-placement\" should be used to determine the location of the contents with respect to the scrollbars."),
							 FALSE,
							 GTK_PARAM_READABLE | GTK_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_SHADOW_TYPE,
                                   g_param_spec_enum ("shadow-type",
                                                      P_("Shadow Type"),
                                                      P_("Style of bevel around the contents"),
						      GTK_TYPE_SHADOW_TYPE,
						      GTK_SHADOW_NONE,
                                                      GTK_PARAM_READABLE | GTK_PARAM_WRITABLE));

  /**
   * GtkScrolledWindow:scrollbars-within-bevel:
   *
   * Whether to place scrollbars within the scrolled window's bevel.
   *
   * Since: 2.12
   */
  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_boolean ("scrollbars-within-bevel",
							         P_("Scrollbars within bevel"),
							         P_("Place scrollbars within the scrolled window's bevel"),
							         FALSE,
							         GTK_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_int ("scrollbar-spacing",
							     P_("Scrollbar spacing"),
							     P_("Number of pixels between the scrollbars and the scrolled window"),
							     0,
							     G_MAXINT,
							     DEFAULT_SCROLLBAR_SPACING,
							     GTK_PARAM_READABLE));

  /**
   * GtkScrolledWindow:min-content-width:
   *
   * The minimum content width of @scrolled_window, or -1 if not set.
   *
   * Since: 3.0
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MIN_CONTENT_WIDTH,
                                   g_param_spec_int ("min-content-width",
                                                     P_("Minimum Content Width"),
                                                     P_("The minimum width that the scrolled window will allocate to its content"),
                                                     -1, G_MAXINT, -1,
                                                     GTK_PARAM_READWRITE));

  /**
   * GtkScrolledWindow:min-content-height:
   *
   * The minimum content height of @scrolled_window, or -1 if not set.
   *
   * Since: 3.0
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MIN_CONTENT_HEIGHT,
                                   g_param_spec_int ("min-content-height",
                                                     P_("Minimum Content Height"),
                                                     P_("The minimum height that the scrolled window will allocate to its content"),
                                                     -1, G_MAXINT, -1,
                                                     GTK_PARAM_READWRITE));

  /**
   * GtkScrolledWindow:kinetic-scrolling:
   *
   * The kinetic scrolling behavior flags. Kinetic scrolling
   * only applies to devices with source %GDK_SOURCE_TOUCHSCREEN
   *
   * Since: 3.4
   */
  g_object_class_install_property (gobject_class,
                                   PROP_KINETIC_SCROLLING,
                                   g_param_spec_boolean ("kinetic-scrolling",
                                                         P_("Kinetic Scrolling"),
                                                         P_("Kinetic scrolling mode."),
                                                         TRUE,
                                                         GTK_PARAM_READABLE |
                                                         GTK_PARAM_WRITABLE));
  /**
   * GtkScrolledWindow::scroll-child:
   * @scrolled_window: a #GtkScrolledWindow
   * @scroll: a #GtkScrollType describing how much to scroll
   * @horizontal: whether the keybinding scrolls the child
   *   horizontally or not
   *
   * The ::scroll-child signal is a
   * <link linkend="keybinding-signals">keybinding signal</link>
   * which gets emitted when a keybinding that scrolls is pressed.
   * The horizontal or vertical adjustment is updated which triggers a
   * signal that the scrolled windows child may listen to and scroll itself.
   */
  signals[SCROLL_CHILD] =
    g_signal_new (I_("scroll-child"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (GtkScrolledWindowClass, scroll_child),
                  NULL, NULL,
                  _gtk_marshal_BOOLEAN__ENUM_BOOLEAN,
                  G_TYPE_BOOLEAN, 2,
                  GTK_TYPE_SCROLL_TYPE,
		  G_TYPE_BOOLEAN);

  /**
   * GtkScrolledWindow::move-focus-out:
   * @scrolled_window: a #GtkScrolledWindow
   * @direction_type: either %GTK_DIR_TAB_FORWARD or
   *   %GTK_DIR_TAB_BACKWARD
   *
   * The ::move-focus-out signal is a
   * <link linkend="keybinding-signals">keybinding signal</link>
   * which gets emitted when focus is moved away from the scrolled
   * window by a keybinding.
   * The #GtkWidget::move-focus signal is emitted with @direction_type
   * on this scrolled windows toplevel parent in the container hierarchy.
   * The default bindings for this signal are
   * <keycombo><keycap>Tab</keycap><keycap>Ctrl</keycap></keycombo>
   * and
   * <keycombo><keycap>Tab</keycap><keycap>Ctrl</keycap><keycap>Shift</keycap></keycombo>.
   */
  signals[MOVE_FOCUS_OUT] =
    g_signal_new (I_("move-focus-out"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (GtkScrolledWindowClass, move_focus_out),
                  NULL, NULL,
                  _gtk_marshal_VOID__ENUM,
                  G_TYPE_NONE, 1,
                  GTK_TYPE_DIRECTION_TYPE);
  
  binding_set = gtk_binding_set_by_class (class);

  add_scroll_binding (binding_set, GDK_KEY_Left,  GDK_CONTROL_MASK, GTK_SCROLL_STEP_BACKWARD, TRUE);
  add_scroll_binding (binding_set, GDK_KEY_Right, GDK_CONTROL_MASK, GTK_SCROLL_STEP_FORWARD,  TRUE);
  add_scroll_binding (binding_set, GDK_KEY_Up,    GDK_CONTROL_MASK, GTK_SCROLL_STEP_BACKWARD, FALSE);
  add_scroll_binding (binding_set, GDK_KEY_Down,  GDK_CONTROL_MASK, GTK_SCROLL_STEP_FORWARD,  FALSE);

  add_scroll_binding (binding_set, GDK_KEY_Page_Up,   GDK_CONTROL_MASK, GTK_SCROLL_PAGE_BACKWARD, TRUE);
  add_scroll_binding (binding_set, GDK_KEY_Page_Down, GDK_CONTROL_MASK, GTK_SCROLL_PAGE_FORWARD,  TRUE);
  add_scroll_binding (binding_set, GDK_KEY_Page_Up,   0,                GTK_SCROLL_PAGE_BACKWARD, FALSE);
  add_scroll_binding (binding_set, GDK_KEY_Page_Down, 0,                GTK_SCROLL_PAGE_FORWARD,  FALSE);

  add_scroll_binding (binding_set, GDK_KEY_Home, GDK_CONTROL_MASK, GTK_SCROLL_START, TRUE);
  add_scroll_binding (binding_set, GDK_KEY_End,  GDK_CONTROL_MASK, GTK_SCROLL_END,   TRUE);
  add_scroll_binding (binding_set, GDK_KEY_Home, 0,                GTK_SCROLL_START, FALSE);
  add_scroll_binding (binding_set, GDK_KEY_End,  0,                GTK_SCROLL_END,   FALSE);

  add_tab_bindings (binding_set, GDK_CONTROL_MASK, GTK_DIR_TAB_FORWARD);
  add_tab_bindings (binding_set, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_DIR_TAB_BACKWARD);

  g_type_class_add_private (class, sizeof (GtkScrolledWindowPrivate));

  gtk_widget_class_set_accessible_type (widget_class, GTK_TYPE_SCROLLED_WINDOW_ACCESSIBLE);
}

static void
gtk_scrolled_window_init (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv;

  scrolled_window->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE (scrolled_window,
                                                              GTK_TYPE_SCROLLED_WINDOW,
                                                              GtkScrolledWindowPrivate);

  gtk_widget_set_has_window (GTK_WIDGET (scrolled_window), FALSE);
  gtk_widget_set_can_focus (GTK_WIDGET (scrolled_window), TRUE);

  priv->hscrollbar = NULL;
  priv->vscrollbar = NULL;
  priv->hscrollbar_policy = GTK_POLICY_AUTOMATIC;
  priv->vscrollbar_policy = GTK_POLICY_AUTOMATIC;
  priv->hscrollbar_visible = FALSE;
  priv->vscrollbar_visible = FALSE;
  priv->focus_out = FALSE;
  priv->window_placement = GTK_CORNER_TOP_LEFT;
  gtk_scrolled_window_update_real_placement (scrolled_window);
  priv->min_content_width = -1;
  priv->min_content_height = -1;

  gtk_scrolled_window_set_kinetic_scrolling (scrolled_window, TRUE);
  gtk_scrolled_window_set_capture_button_press (scrolled_window, TRUE);
}

/**
 * gtk_scrolled_window_new:
 * @hadjustment: (allow-none): horizontal adjustment
 * @vadjustment: (allow-none): vertical adjustment
 *
 * Creates a new scrolled window.
 *
 * The two arguments are the scrolled window's adjustments; these will be
 * shared with the scrollbars and the child widget to keep the bars in sync 
 * with the child. Usually you want to pass %NULL for the adjustments, which 
 * will cause the scrolled window to create them for you.
 *
 * Returns: a new scrolled window
 */
GtkWidget*
gtk_scrolled_window_new (GtkAdjustment *hadjustment,
			 GtkAdjustment *vadjustment)
{
  GtkWidget *scrolled_window;

  if (hadjustment)
    g_return_val_if_fail (GTK_IS_ADJUSTMENT (hadjustment), NULL);

  if (vadjustment)
    g_return_val_if_fail (GTK_IS_ADJUSTMENT (vadjustment), NULL);

  scrolled_window = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
				    "hadjustment", hadjustment,
				    "vadjustment", vadjustment,
				    NULL);

  return scrolled_window;
}

/**
 * gtk_scrolled_window_set_hadjustment:
 * @scrolled_window: a #GtkScrolledWindow
 * @hadjustment: horizontal scroll adjustment
 *
 * Sets the #GtkAdjustment for the horizontal scrollbar.
 */
void
gtk_scrolled_window_set_hadjustment (GtkScrolledWindow *scrolled_window,
				     GtkAdjustment     *hadjustment)
{
  GtkScrolledWindowPrivate *priv;
  GtkBin *bin;
  GtkWidget *child;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));
  if (hadjustment)
    g_return_if_fail (GTK_IS_ADJUSTMENT (hadjustment));
  else
    hadjustment = (GtkAdjustment*) g_object_new (GTK_TYPE_ADJUSTMENT, NULL);

  bin = GTK_BIN (scrolled_window);
  priv = scrolled_window->priv;

  if (!priv->hscrollbar)
    {
      gtk_widget_push_composite_child ();
      priv->hscrollbar = gtk_scrollbar_new (GTK_ORIENTATION_HORIZONTAL, hadjustment);
      gtk_widget_set_composite_name (priv->hscrollbar, "hscrollbar");
      gtk_widget_pop_composite_child ();

      gtk_widget_set_parent (priv->hscrollbar, GTK_WIDGET (scrolled_window));
      g_object_ref (priv->hscrollbar);
      gtk_widget_show (priv->hscrollbar);
    }
  else
    {
      GtkAdjustment *old_adjustment;

      old_adjustment = gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar));
      if (old_adjustment == hadjustment)
	return;

      g_signal_handlers_disconnect_by_func (old_adjustment,
					    gtk_scrolled_window_adjustment_changed,
					    scrolled_window);
      gtk_range_set_adjustment (GTK_RANGE (priv->hscrollbar),
				hadjustment);
    }
  hadjustment = gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar));
  g_signal_connect (hadjustment,
		    "changed",
		    G_CALLBACK (gtk_scrolled_window_adjustment_changed),
		    scrolled_window);
  g_signal_connect (hadjustment,
		    "value-changed",
		    G_CALLBACK (gtk_scrolled_window_adjustment_value_changed),
		    scrolled_window);
  gtk_scrolled_window_adjustment_changed (hadjustment, scrolled_window);
  gtk_scrolled_window_adjustment_value_changed (hadjustment, scrolled_window);

  child = gtk_bin_get_child (bin);
  if (GTK_IS_SCROLLABLE (child))
    gtk_scrollable_set_hadjustment (GTK_SCROLLABLE (child), hadjustment);

  g_object_notify (G_OBJECT (scrolled_window), "hadjustment");
}

/**
 * gtk_scrolled_window_set_vadjustment:
 * @scrolled_window: a #GtkScrolledWindow
 * @vadjustment: vertical scroll adjustment
 *
 * Sets the #GtkAdjustment for the vertical scrollbar.
 */
void
gtk_scrolled_window_set_vadjustment (GtkScrolledWindow *scrolled_window,
                                     GtkAdjustment     *vadjustment)
{
  GtkScrolledWindowPrivate *priv;
  GtkBin *bin;
  GtkWidget *child;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));
  if (vadjustment)
    g_return_if_fail (GTK_IS_ADJUSTMENT (vadjustment));
  else
    vadjustment = (GtkAdjustment*) g_object_new (GTK_TYPE_ADJUSTMENT, NULL);

  bin = GTK_BIN (scrolled_window);
  priv = scrolled_window->priv;

  if (!priv->vscrollbar)
    {
      gtk_widget_push_composite_child ();
      priv->vscrollbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, vadjustment);
      gtk_widget_set_composite_name (priv->vscrollbar, "vscrollbar");
      gtk_widget_pop_composite_child ();

      gtk_widget_set_parent (priv->vscrollbar, GTK_WIDGET (scrolled_window));
      g_object_ref (priv->vscrollbar);
      gtk_widget_show (priv->vscrollbar);
    }
  else
    {
      GtkAdjustment *old_adjustment;
      
      old_adjustment = gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar));
      if (old_adjustment == vadjustment)
	return;

      g_signal_handlers_disconnect_by_func (old_adjustment,
					    gtk_scrolled_window_adjustment_changed,
					    scrolled_window);
      gtk_range_set_adjustment (GTK_RANGE (priv->vscrollbar),
				vadjustment);
    }
  vadjustment = gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar));
  g_signal_connect (vadjustment,
		    "changed",
		    G_CALLBACK (gtk_scrolled_window_adjustment_changed),
		    scrolled_window);
  g_signal_connect (vadjustment,
		    "value-changed",
		    G_CALLBACK (gtk_scrolled_window_adjustment_value_changed),
		    scrolled_window);
  gtk_scrolled_window_adjustment_changed (vadjustment, scrolled_window);
  gtk_scrolled_window_adjustment_value_changed (vadjustment, scrolled_window);

  child = gtk_bin_get_child (bin);
  if (GTK_IS_SCROLLABLE (child))
    gtk_scrollable_set_vadjustment (GTK_SCROLLABLE (child), vadjustment);

  g_object_notify (G_OBJECT (scrolled_window), "vadjustment");
}

/**
 * gtk_scrolled_window_get_hadjustment:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Returns the horizontal scrollbar's adjustment, used to connect the
 * horizontal scrollbar to the child widget's horizontal scroll
 * functionality.
 *
 * Returns: (transfer none): the horizontal #GtkAdjustment
 */
GtkAdjustment*
gtk_scrolled_window_get_hadjustment (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv;

  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), NULL);

  priv = scrolled_window->priv;

  return (priv->hscrollbar ?
	  gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar)) :
	  NULL);
}

/**
 * gtk_scrolled_window_get_vadjustment:
 * @scrolled_window: a #GtkScrolledWindow
 * 
 * Returns the vertical scrollbar's adjustment, used to connect the
 * vertical scrollbar to the child widget's vertical scroll functionality.
 * 
 * Returns: (transfer none): the vertical #GtkAdjustment
 */
GtkAdjustment*
gtk_scrolled_window_get_vadjustment (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv;

  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), NULL);

  priv = scrolled_window->priv;

  return (priv->vscrollbar ?
	  gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar)) :
	  NULL);
}

/**
 * gtk_scrolled_window_get_hscrollbar:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Returns the horizontal scrollbar of @scrolled_window.
 *
 * Returns: (transfer none): the horizontal scrollbar of the scrolled window,
 *     or %NULL if it does not have one.
 *
 * Since: 2.8
 */
GtkWidget*
gtk_scrolled_window_get_hscrollbar (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), NULL);

  return scrolled_window->priv->hscrollbar;
}

/**
 * gtk_scrolled_window_get_vscrollbar:
 * @scrolled_window: a #GtkScrolledWindow
 * 
 * Returns the vertical scrollbar of @scrolled_window.
 *
 * Returns: (transfer none): the vertical scrollbar of the scrolled window,
 *     or %NULL if it does not have one.
 *
 * Since: 2.8
 */
GtkWidget*
gtk_scrolled_window_get_vscrollbar (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), NULL);

  return scrolled_window->priv->vscrollbar;
}

/**
 * gtk_scrolled_window_set_policy:
 * @scrolled_window: a #GtkScrolledWindow
 * @hscrollbar_policy: policy for horizontal bar
 * @vscrollbar_policy: policy for vertical bar
 * 
 * Sets the scrollbar policy for the horizontal and vertical scrollbars.
 *
 * The policy determines when the scrollbar should appear; it is a value
 * from the #GtkPolicyType enumeration. If %GTK_POLICY_ALWAYS, the
 * scrollbar is always present; if %GTK_POLICY_NEVER, the scrollbar is
 * never present; if %GTK_POLICY_AUTOMATIC, the scrollbar is present only
 * if needed (that is, if the slider part of the bar would be smaller
 * than the trough - the display is larger than the page size).
 */
void
gtk_scrolled_window_set_policy (GtkScrolledWindow *scrolled_window,
				GtkPolicyType      hscrollbar_policy,
				GtkPolicyType      vscrollbar_policy)
{
  GtkScrolledWindowPrivate *priv;
  GObject *object = G_OBJECT (scrolled_window);
  
  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  priv = scrolled_window->priv;

  if ((priv->hscrollbar_policy != hscrollbar_policy) ||
      (priv->vscrollbar_policy != vscrollbar_policy))
    {
      priv->hscrollbar_policy = hscrollbar_policy;
      priv->vscrollbar_policy = vscrollbar_policy;

      gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));

      g_object_freeze_notify (object);
      g_object_notify (object, "hscrollbar-policy");
      g_object_notify (object, "vscrollbar-policy");
      g_object_thaw_notify (object);
    }
}

/**
 * gtk_scrolled_window_get_policy:
 * @scrolled_window: a #GtkScrolledWindow
 * @hscrollbar_policy: (out) (allow-none): location to store the policy 
 *     for the horizontal scrollbar, or %NULL.
 * @vscrollbar_policy: (out) (allow-none): location to store the policy
 *     for the vertical scrollbar, or %NULL.
 * 
 * Retrieves the current policy values for the horizontal and vertical
 * scrollbars. See gtk_scrolled_window_set_policy().
 */
void
gtk_scrolled_window_get_policy (GtkScrolledWindow *scrolled_window,
				GtkPolicyType     *hscrollbar_policy,
				GtkPolicyType     *vscrollbar_policy)
{
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  priv = scrolled_window->priv;

  if (hscrollbar_policy)
    *hscrollbar_policy = priv->hscrollbar_policy;
  if (vscrollbar_policy)
    *vscrollbar_policy = priv->vscrollbar_policy;
}

static void
gtk_scrolled_window_update_real_placement (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkSettings *settings;

  settings = gtk_widget_get_settings (GTK_WIDGET (scrolled_window));

  if (priv->window_placement_set || settings == NULL)
    priv->real_window_placement = priv->window_placement;
  else
    g_object_get (settings,
		  "gtk-scrolled-window-placement",
		  &priv->real_window_placement,
		  NULL);
}

static void
gtk_scrolled_window_set_placement_internal (GtkScrolledWindow *scrolled_window,
					    GtkCornerType      window_placement)
{
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;

  if (priv->window_placement != window_placement)
    {
      priv->window_placement = window_placement;

      gtk_scrolled_window_update_real_placement (scrolled_window);
      gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));
      
      g_object_notify (G_OBJECT (scrolled_window), "window-placement");
    }
}

static void
gtk_scrolled_window_set_placement_set (GtkScrolledWindow *scrolled_window,
				       gboolean           placement_set,
				       gboolean           emit_resize)
{
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;

  if (priv->window_placement_set != placement_set)
    {
      priv->window_placement_set = placement_set;

      gtk_scrolled_window_update_real_placement (scrolled_window);
      if (emit_resize)
        gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));

      g_object_notify (G_OBJECT (scrolled_window), "window-placement-set");
    }
}

/**
 * gtk_scrolled_window_set_placement:
 * @scrolled_window: a #GtkScrolledWindow
 * @window_placement: position of the child window
 *
 * Sets the placement of the contents with respect to the scrollbars
 * for the scrolled window.
 * 
 * The default is %GTK_CORNER_TOP_LEFT, meaning the child is
 * in the top left, with the scrollbars underneath and to the right.
 * Other values in #GtkCornerType are %GTK_CORNER_TOP_RIGHT,
 * %GTK_CORNER_BOTTOM_LEFT, and %GTK_CORNER_BOTTOM_RIGHT.
 *
 * See also gtk_scrolled_window_get_placement() and
 * gtk_scrolled_window_unset_placement().
 */
void
gtk_scrolled_window_set_placement (GtkScrolledWindow *scrolled_window,
				   GtkCornerType      window_placement)
{
  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  gtk_scrolled_window_set_placement_set (scrolled_window, TRUE, FALSE);
  gtk_scrolled_window_set_placement_internal (scrolled_window, window_placement);
}

/**
 * gtk_scrolled_window_get_placement:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Gets the placement of the contents with respect to the scrollbars
 * for the scrolled window. See gtk_scrolled_window_set_placement().
 *
 * Return value: the current placement value.
 *
 * See also gtk_scrolled_window_set_placement() and
 * gtk_scrolled_window_unset_placement().
 **/
GtkCornerType
gtk_scrolled_window_get_placement (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), GTK_CORNER_TOP_LEFT);

  return scrolled_window->priv->window_placement;
}

/**
 * gtk_scrolled_window_unset_placement:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Unsets the placement of the contents with respect to the scrollbars
 * for the scrolled window. If no window placement is set for a scrolled
 * window, it obeys the "gtk-scrolled-window-placement" XSETTING.
 *
 * See also gtk_scrolled_window_set_placement() and
 * gtk_scrolled_window_get_placement().
 *
 * Since: 2.10
 **/
void
gtk_scrolled_window_unset_placement (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  priv = scrolled_window->priv;

  if (priv->window_placement_set)
    {
      priv->window_placement_set = FALSE;

      gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));

      g_object_notify (G_OBJECT (scrolled_window), "window-placement-set");
    }
}

/**
 * gtk_scrolled_window_set_shadow_type:
 * @scrolled_window: a #GtkScrolledWindow
 * @type: kind of shadow to draw around scrolled window contents
 *
 * Changes the type of shadow drawn around the contents of
 * @scrolled_window.
 * 
 **/
void
gtk_scrolled_window_set_shadow_type (GtkScrolledWindow *scrolled_window,
				     GtkShadowType      type)
{
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));
  g_return_if_fail (type >= GTK_SHADOW_NONE && type <= GTK_SHADOW_ETCHED_OUT);

  priv = scrolled_window->priv;

  if (priv->shadow_type != type)
    {
      priv->shadow_type = type;

      if (gtk_widget_is_drawable (GTK_WIDGET (scrolled_window)))
	gtk_widget_queue_draw (GTK_WIDGET (scrolled_window));

      gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));

      g_object_notify (G_OBJECT (scrolled_window), "shadow-type");
    }
}

/**
 * gtk_scrolled_window_get_shadow_type:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Gets the shadow type of the scrolled window. See 
 * gtk_scrolled_window_set_shadow_type().
 *
 * Return value: the current shadow type
 **/
GtkShadowType
gtk_scrolled_window_get_shadow_type (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_NONE);

  return scrolled_window->priv->shadow_type;
}

/**
 * gtk_scrolled_window_set_kinetic_scrolling:
 * @scrolled_window: a #GtkScrolledWindow
 * @kinetic_scrolling: %TRUE to enable kinetic scrolling
 *
 * Turns kinetic scrolling on or off.
 * Kinetic scrolling only applies to devices with source
 * %GDK_SOURCE_TOUCHSCREEN.
 *
 * Since: 3.4
 **/
void
gtk_scrolled_window_set_kinetic_scrolling (GtkScrolledWindow *scrolled_window,
                                           gboolean           kinetic_scrolling)
{
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  priv = scrolled_window->priv;
  if (priv->kinetic_scrolling == kinetic_scrolling)
    return;

  priv->kinetic_scrolling = kinetic_scrolling;
  if (priv->kinetic_scrolling)
    {
      _gtk_widget_set_captured_event_handler (GTK_WIDGET (scrolled_window),
                                              gtk_scrolled_window_captured_event);
    }
  else
    {
      _gtk_widget_set_captured_event_handler (GTK_WIDGET (scrolled_window), NULL);
      if (priv->release_timeout_id)
        {
          g_source_remove (priv->release_timeout_id);
          priv->release_timeout_id = 0;
        }
      if (priv->deceleration_id)
        {
          g_source_remove (priv->deceleration_id);
          priv->deceleration_id = 0;
        }
    }
  g_object_notify (G_OBJECT (scrolled_window), "kinetic-scrolling");
}

/**
 * gtk_scrolled_window_get_kinetic_scrolling:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Returns the specified kinetic scrolling behavior.
 *
 * Return value: the scrolling behavior flags.
 *
 * Since: 3.4
 */
gboolean
gtk_scrolled_window_get_kinetic_scrolling (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), FALSE);

  return scrolled_window->priv->kinetic_scrolling;
}

/**
 * gtk_scrolled_window_set_capture_button_press:
 * @scrolled_window: a #GtkScrolledWindow
 * @capture_button_press: %TRUE to capture button presses
 *
 * Changes the behaviour of @scrolled_window wrt. to the initial
 * event that possibly starts kinetic scrolling. When @capture_button_press
 * is set to %TRUE, the event is captured by the scrolled window, and
 * then later replayed if it is meant to go to the child widget.
 *
 * This should be enabled if any child widgets perform non-reversible
 * actions on #GtkWidget::button-press-event. If they don't, and handle
 * additionally handle #GtkWidget::grab-broken-event, it might be better
 * to set @capture_button_press to %FALSE.
 *
 * This setting only has an effect if kinetic scrolling is enabled.
 *
 * Since: 3.4
 */
void
gtk_scrolled_window_set_capture_button_press (GtkScrolledWindow *scrolled_window,
                                              gboolean           capture_button_press)
{
  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  scrolled_window->priv->capture_button_press = capture_button_press;
}

/**
 * gtk_scrolled_window_get_capture_button_press:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Return whether button presses are captured during kinetic
 * scrolling. See gtk_scrolled_window_set_capture_button_press().
 *
 * Returns: %TRUE if button presses are captured during kinetic scrolling
 *
 * Since: 3.4
 */
gboolean
gtk_scrolled_window_get_capture_button_press (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), FALSE);

  return scrolled_window->priv->capture_button_press;
}


static void
gtk_scrolled_window_destroy (GtkWidget *widget)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;

  if (priv->hscrollbar)
    {
      g_signal_handlers_disconnect_by_func (gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar)),
					    gtk_scrolled_window_adjustment_changed,
					    scrolled_window);
      gtk_widget_unparent (priv->hscrollbar);
      gtk_widget_destroy (priv->hscrollbar);
      g_object_unref (priv->hscrollbar);
      priv->hscrollbar = NULL;
    }
  if (priv->vscrollbar)
    {
      g_signal_handlers_disconnect_by_func (gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar)),
					    gtk_scrolled_window_adjustment_changed,
					    scrolled_window);
      gtk_widget_unparent (priv->vscrollbar);
      gtk_widget_destroy (priv->vscrollbar);
      g_object_unref (priv->vscrollbar);
      priv->vscrollbar = NULL;
    }

  if (priv->release_timeout_id)
    {
      g_source_remove (priv->release_timeout_id);
      priv->release_timeout_id = 0;
    }
  if (priv->deceleration_id)
    {
      g_source_remove (priv->deceleration_id);
      priv->deceleration_id = 0;
    }

  if (priv->button_press_event)
    {
      gdk_event_free (priv->button_press_event);
      priv->button_press_event = NULL;
    }

  GTK_WIDGET_CLASS (gtk_scrolled_window_parent_class)->destroy (widget);
}

static void
gtk_scrolled_window_set_property (GObject      *object,
				  guint         prop_id,
				  const GValue *value,
				  GParamSpec   *pspec)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (object);
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;

  switch (prop_id)
    {
    case PROP_HADJUSTMENT:
      gtk_scrolled_window_set_hadjustment (scrolled_window,
					   g_value_get_object (value));
      break;
    case PROP_VADJUSTMENT:
      gtk_scrolled_window_set_vadjustment (scrolled_window,
					   g_value_get_object (value));
      break;
    case PROP_HSCROLLBAR_POLICY:
      gtk_scrolled_window_set_policy (scrolled_window,
				      g_value_get_enum (value),
				      priv->vscrollbar_policy);
      break;
    case PROP_VSCROLLBAR_POLICY:
      gtk_scrolled_window_set_policy (scrolled_window,
				      priv->hscrollbar_policy,
				      g_value_get_enum (value));
      break;
    case PROP_WINDOW_PLACEMENT:
      gtk_scrolled_window_set_placement_internal (scrolled_window,
		      				  g_value_get_enum (value));
      break;
    case PROP_WINDOW_PLACEMENT_SET:
      gtk_scrolled_window_set_placement_set (scrolled_window,
		      			     g_value_get_boolean (value),
					     TRUE);
      break;
    case PROP_SHADOW_TYPE:
      gtk_scrolled_window_set_shadow_type (scrolled_window,
					   g_value_get_enum (value));
      break;
    case PROP_MIN_CONTENT_WIDTH:
      gtk_scrolled_window_set_min_content_width (scrolled_window,
                                                 g_value_get_int (value));
      break;
    case PROP_MIN_CONTENT_HEIGHT:
      gtk_scrolled_window_set_min_content_height (scrolled_window,
                                                  g_value_get_int (value));
      break;
    case PROP_KINETIC_SCROLLING:
      gtk_scrolled_window_set_kinetic_scrolling (scrolled_window,
                                                 g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_scrolled_window_get_property (GObject    *object,
				  guint       prop_id,
				  GValue     *value,
				  GParamSpec *pspec)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (object);
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;

  switch (prop_id)
    {
    case PROP_HADJUSTMENT:
      g_value_set_object (value,
			  G_OBJECT (gtk_scrolled_window_get_hadjustment (scrolled_window)));
      break;
    case PROP_VADJUSTMENT:
      g_value_set_object (value,
			  G_OBJECT (gtk_scrolled_window_get_vadjustment (scrolled_window)));
      break;
    case PROP_WINDOW_PLACEMENT:
      g_value_set_enum (value, priv->window_placement);
      break;
    case PROP_WINDOW_PLACEMENT_SET:
      g_value_set_boolean (value, priv->window_placement_set);
      break;
    case PROP_SHADOW_TYPE:
      g_value_set_enum (value, priv->shadow_type);
      break;
    case PROP_HSCROLLBAR_POLICY:
      g_value_set_enum (value, priv->hscrollbar_policy);
      break;
    case PROP_VSCROLLBAR_POLICY:
      g_value_set_enum (value, priv->vscrollbar_policy);
      break;
    case PROP_MIN_CONTENT_WIDTH:
      g_value_set_int (value, priv->min_content_width);
      break;
    case PROP_MIN_CONTENT_HEIGHT:
      g_value_set_int (value, priv->min_content_height);
      break;
    case PROP_KINETIC_SCROLLING:
      g_value_set_boolean (value, priv->kinetic_scrolling);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
traverse_container (GtkWidget *widget,
		    gpointer   data)
{
  if (GTK_IS_SCROLLED_WINDOW (widget))
    {
      gtk_scrolled_window_update_real_placement (GTK_SCROLLED_WINDOW (widget));
      gtk_widget_queue_resize (widget);
    }
  else if (GTK_IS_CONTAINER (widget))
    gtk_container_forall (GTK_CONTAINER (widget), traverse_container, NULL);
}

static void
gtk_scrolled_window_settings_changed (GtkSettings *settings)
{
  GList *list, *l;

  list = gtk_window_list_toplevels ();

  for (l = list; l; l = l->next)
    gtk_container_forall (GTK_CONTAINER (l->data), 
			  traverse_container, NULL);

  g_list_free (list);
}

static void
gtk_scrolled_window_screen_changed (GtkWidget *widget,
				    GdkScreen *previous_screen)
{
  GtkSettings *settings;
  guint window_placement_connection;

  gtk_scrolled_window_update_real_placement (GTK_SCROLLED_WINDOW (widget));

  if (!gtk_widget_has_screen (widget))
    return;

  settings = gtk_widget_get_settings (widget);

  window_placement_connection = 
    GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (settings), 
					 "gtk-scrolled-window-connection"));
  
  if (window_placement_connection)
    return;

  window_placement_connection =
    g_signal_connect (settings, "notify::gtk-scrolled-window-placement",
		      G_CALLBACK (gtk_scrolled_window_settings_changed), NULL);
  g_object_set_data (G_OBJECT (settings), 
		     I_("gtk-scrolled-window-connection"),
		     GUINT_TO_POINTER (window_placement_connection));
}

static void
gtk_scrolled_window_draw_scrollbars_junction (GtkScrolledWindow *scrolled_window,
                                              cairo_t *cr)
{
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkWidget *widget = GTK_WIDGET (scrolled_window);
  GtkAllocation wid_allocation, hscr_allocation, vscr_allocation;
  GtkStyleContext *context;
  GdkRectangle junction_rect;
  gboolean is_rtl, scrollbars_within_bevel;

  is_rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;
  gtk_widget_get_allocation (widget, &wid_allocation);
  gtk_widget_get_allocation (GTK_WIDGET (priv->hscrollbar), &hscr_allocation);
  gtk_widget_get_allocation (GTK_WIDGET (priv->vscrollbar), &vscr_allocation);

  gtk_widget_style_get (widget,
                        "scrollbars-within-bevel", &scrollbars_within_bevel,
                        NULL);
  context = gtk_widget_get_style_context (widget);

  if (scrollbars_within_bevel &&
      priv->shadow_type != GTK_SHADOW_NONE)
    {
      GtkStateFlags state;
      GtkBorder padding, border;

      state = gtk_widget_get_state_flags (widget);

      gtk_style_context_save (context);
      gtk_style_context_add_class (context, GTK_STYLE_CLASS_FRAME);
      gtk_style_context_get_padding (context, state, &padding);
      gtk_style_context_get_border (context, state, &border);
      gtk_style_context_restore (context);

      junction_rect.x = padding.left + border.left;
      junction_rect.y = padding.top + border.top;
    }
  else
    {
      junction_rect.x = 0;
      junction_rect.y = 0;
    }

  junction_rect.width = vscr_allocation.width;
  junction_rect.height = hscr_allocation.height;
  
  if ((is_rtl && 
       (priv->real_window_placement == GTK_CORNER_TOP_RIGHT ||
        priv->real_window_placement == GTK_CORNER_BOTTOM_RIGHT)) ||
      (!is_rtl && 
       (priv->real_window_placement == GTK_CORNER_TOP_LEFT ||
        priv->real_window_placement == GTK_CORNER_BOTTOM_LEFT)))
    junction_rect.x += hscr_allocation.width;

  if (priv->real_window_placement == GTK_CORNER_TOP_LEFT ||
      priv->real_window_placement == GTK_CORNER_TOP_RIGHT)
    junction_rect.y += vscr_allocation.height;

  gtk_style_context_save (context);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_SCROLLBARS_JUNCTION);

  gtk_render_background (context, cr,
                         junction_rect.x, junction_rect.y,
                         junction_rect.width, junction_rect.height);
  gtk_render_frame (context, cr,
                    junction_rect.x, junction_rect.y,
                    junction_rect.width, junction_rect.height);

  gtk_style_context_restore (context);
}

static gboolean
gtk_scrolled_window_draw (GtkWidget *widget,
                          cairo_t   *cr)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkAllocation relative_allocation;
  GtkStyleContext *context;

  context = gtk_widget_get_style_context (widget);
  gtk_scrolled_window_relative_allocation (widget, &relative_allocation);

  gtk_render_background (context, cr,
                         0, 0,
                         gtk_widget_get_allocated_width (widget), gtk_widget_get_allocated_height (widget));

  if (priv->hscrollbar_visible && 
      priv->vscrollbar_visible)
    gtk_scrolled_window_draw_scrollbars_junction (scrolled_window, cr);

  if (priv->shadow_type != GTK_SHADOW_NONE)
    {
      gboolean scrollbars_within_bevel;

      gtk_style_context_save (context);
      gtk_style_context_add_class (context, GTK_STYLE_CLASS_FRAME);

      gtk_widget_style_get (widget, "scrollbars-within-bevel", &scrollbars_within_bevel, NULL);

      if (!scrollbars_within_bevel)
        {
          GtkStateFlags state;
          GtkBorder padding, border;

          state = gtk_widget_get_state_flags (widget);
          gtk_style_context_get_padding (context, state, &padding);
          gtk_style_context_get_border (context, state, &border);

          relative_allocation.x -= padding.left + border.left;
          relative_allocation.y -= padding.top + border.top;
          relative_allocation.width += padding.left + padding.right + border.left + border.right;
          relative_allocation.height += padding.top + padding.bottom + border.top + border.bottom;
        }
      else
        {
          relative_allocation.x = 0;
          relative_allocation.y = 0;
          relative_allocation.width = gtk_widget_get_allocated_width (widget);
          relative_allocation.height = gtk_widget_get_allocated_height (widget);
        }

      gtk_render_frame (context, cr,
                        relative_allocation.x,
                        relative_allocation.y,
			relative_allocation.width,
			relative_allocation.height);

      gtk_style_context_restore (context);
    }

  GTK_WIDGET_CLASS (gtk_scrolled_window_parent_class)->draw (widget, cr);

  return FALSE;
}

static void
gtk_scrolled_window_forall (GtkContainer *container,
			    gboolean	  include_internals,
			    GtkCallback   callback,
			    gpointer      callback_data)
{
  GtkScrolledWindowPrivate *priv;
  GtkScrolledWindow *scrolled_window;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (container));
  g_return_if_fail (callback != NULL);

  GTK_CONTAINER_CLASS (gtk_scrolled_window_parent_class)->forall (container,
					      include_internals,
					      callback,
					      callback_data);
  if (include_internals)
    {
      scrolled_window = GTK_SCROLLED_WINDOW (container);
      priv = scrolled_window->priv;

      if (priv->vscrollbar)
	callback (priv->vscrollbar, callback_data);
      if (priv->hscrollbar)
	callback (priv->hscrollbar, callback_data);
    }
}

static gboolean
gtk_scrolled_window_scroll_child (GtkScrolledWindow *scrolled_window,
				  GtkScrollType      scroll,
				  gboolean           horizontal)
{
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkAdjustment *adjustment = NULL;
  
  switch (scroll)
    {
    case GTK_SCROLL_STEP_UP:
      scroll = GTK_SCROLL_STEP_BACKWARD;
      horizontal = FALSE;
      break;
    case GTK_SCROLL_STEP_DOWN:
      scroll = GTK_SCROLL_STEP_FORWARD;
      horizontal = FALSE;
      break;
    case GTK_SCROLL_STEP_LEFT:
      scroll = GTK_SCROLL_STEP_BACKWARD;
      horizontal = TRUE;
      break;
    case GTK_SCROLL_STEP_RIGHT:
      scroll = GTK_SCROLL_STEP_FORWARD;
      horizontal = TRUE;
      break;
    case GTK_SCROLL_PAGE_UP:
      scroll = GTK_SCROLL_PAGE_BACKWARD;
      horizontal = FALSE;
      break;
    case GTK_SCROLL_PAGE_DOWN:
      scroll = GTK_SCROLL_PAGE_FORWARD;
      horizontal = FALSE;
      break;
    case GTK_SCROLL_PAGE_LEFT:
      scroll = GTK_SCROLL_STEP_BACKWARD;
      horizontal = TRUE;
      break;
    case GTK_SCROLL_PAGE_RIGHT:
      scroll = GTK_SCROLL_STEP_FORWARD;
      horizontal = TRUE;
      break;
    case GTK_SCROLL_STEP_BACKWARD:
    case GTK_SCROLL_STEP_FORWARD:
    case GTK_SCROLL_PAGE_BACKWARD:
    case GTK_SCROLL_PAGE_FORWARD:
    case GTK_SCROLL_START:
    case GTK_SCROLL_END:
      break;
    default:
      g_warning ("Invalid scroll type %u for GtkScrolledWindow::scroll-child", scroll);
      return FALSE;
    }

  if ((horizontal && (!priv->hscrollbar || !priv->hscrollbar_visible)) ||
      (!horizontal && (!priv->vscrollbar || !priv->vscrollbar_visible)))
    return FALSE;

  if (horizontal)
    {
      if (priv->hscrollbar)
	adjustment = gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar));
    }
  else
    {
      if (priv->vscrollbar)
	adjustment = gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar));
    }

  if (adjustment)
    {
      gdouble value = gtk_adjustment_get_value (adjustment);
      
      switch (scroll)
	{
	case GTK_SCROLL_STEP_FORWARD:
	  value += gtk_adjustment_get_step_increment (adjustment);
	  break;
	case GTK_SCROLL_STEP_BACKWARD:
	  value -= gtk_adjustment_get_step_increment (adjustment);
	  break;
	case GTK_SCROLL_PAGE_FORWARD:
	  value += gtk_adjustment_get_page_increment (adjustment);
	  break;
	case GTK_SCROLL_PAGE_BACKWARD:
	  value -= gtk_adjustment_get_page_increment (adjustment);
	  break;
	case GTK_SCROLL_START:
	  value = gtk_adjustment_get_lower (adjustment);
	  break;
	case GTK_SCROLL_END:
	  value = gtk_adjustment_get_upper (adjustment);
	  break;
	default:
	  g_assert_not_reached ();
	  break;
	}

      gtk_adjustment_set_value (adjustment, value);

      return TRUE;
    }

  return FALSE;
}

static void
gtk_scrolled_window_move_focus_out (GtkScrolledWindow *scrolled_window,
				    GtkDirectionType   direction_type)
{
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkWidget *toplevel;
  
  /* Focus out of the scrolled window entirely. We do this by setting
   * a flag, then propagating the focus motion to the notebook.
   */
  toplevel = gtk_widget_get_toplevel (GTK_WIDGET (scrolled_window));
  if (!gtk_widget_is_toplevel (toplevel))
    return;

  g_object_ref (scrolled_window);

  priv->focus_out = TRUE;
  g_signal_emit_by_name (toplevel, "move-focus", direction_type);
  priv->focus_out = FALSE;

  g_object_unref (scrolled_window);
}

static void
gtk_scrolled_window_relative_allocation (GtkWidget     *widget,
					 GtkAllocation *allocation)
{
  GtkAllocation widget_allocation;
  GtkScrolledWindow *scrolled_window;
  GtkScrolledWindowPrivate *priv;
  gint sb_spacing;
  gint sb_width;
  gint sb_height;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (allocation != NULL);

  scrolled_window = GTK_SCROLLED_WINDOW (widget);
  priv = scrolled_window->priv;

  /* Get possible scrollbar dimensions */
  sb_spacing = _gtk_scrolled_window_get_scrollbar_spacing (scrolled_window);
  gtk_widget_get_preferred_height (priv->hscrollbar, &sb_height, NULL);
  gtk_widget_get_preferred_width (priv->vscrollbar, &sb_width, NULL);

  gtk_widget_get_allocation (widget, &widget_allocation);

  allocation->x = 0;
  allocation->y = 0;
  allocation->width = widget_allocation.width;
  allocation->height = widget_allocation.height;

  /* Subtract some things from our available allocation size */
  if (priv->shadow_type != GTK_SHADOW_NONE)
    {
      GtkStyleContext *context;
      GtkStateFlags state;
      GtkBorder padding, border;

      context = gtk_widget_get_style_context (widget);
      state = gtk_widget_get_state_flags (widget);

      gtk_style_context_save (context);
      gtk_style_context_add_class (context, GTK_STYLE_CLASS_FRAME);

      gtk_style_context_get_border (context, state, &border);
      gtk_style_context_get_padding (context, state, &padding);

      allocation->x += padding.left + border.left;
      allocation->y += padding.top + border.top;
      allocation->width = MAX (1, allocation->width - (padding.left + border.left + padding.right + border.right));
      allocation->height = MAX (1, allocation->height - (padding.top + border.top + padding.bottom + border.bottom));

      gtk_style_context_restore (context);
    }

  if (priv->vscrollbar_visible)
    {
      gboolean is_rtl;

      is_rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;
  
      if ((!is_rtl && 
	   (priv->real_window_placement == GTK_CORNER_TOP_RIGHT ||
	    priv->real_window_placement == GTK_CORNER_BOTTOM_RIGHT)) ||
	  (is_rtl && 
	   (priv->real_window_placement == GTK_CORNER_TOP_LEFT ||
	    priv->real_window_placement == GTK_CORNER_BOTTOM_LEFT)))
	allocation->x += (sb_width +  sb_spacing);

      allocation->width = MAX (1, allocation->width - (sb_width + sb_spacing));
    }
  if (priv->hscrollbar_visible)
    {

      if (priv->real_window_placement == GTK_CORNER_BOTTOM_LEFT ||
	  priv->real_window_placement == GTK_CORNER_BOTTOM_RIGHT)
	allocation->y += (sb_height + sb_spacing);

      allocation->height = MAX (1, allocation->height - (sb_height + sb_spacing));
    }
}

static gboolean
_gtk_scrolled_window_get_overshoot (GtkScrolledWindow *scrolled_window,
                                    gint              *overshoot_x,
                                    gint              *overshoot_y)
{
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkAdjustment *vadjustment, *hadjustment;
  gdouble lower, upper, x, y;

  /* Vertical overshoot */
  vadjustment = gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar));
  lower = gtk_adjustment_get_lower (vadjustment);
  upper = gtk_adjustment_get_upper (vadjustment) -
    gtk_adjustment_get_page_size (vadjustment);

  if (priv->unclamped_vadj_value < lower)
    y = priv->unclamped_vadj_value - lower;
  else if (priv->unclamped_vadj_value > upper)
    y = priv->unclamped_vadj_value - upper;
  else
    y = 0;

  /* Horizontal overshoot */
  hadjustment = gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar));
  lower = gtk_adjustment_get_lower (hadjustment);
  upper = gtk_adjustment_get_upper (hadjustment) -
    gtk_adjustment_get_page_size (hadjustment);

  if (priv->unclamped_hadj_value < lower)
    x = priv->unclamped_hadj_value - lower;
  else if (priv->unclamped_hadj_value > upper)
    x = priv->unclamped_hadj_value - upper;
  else
    x = 0;

  if (overshoot_x)
    *overshoot_x = x;

  if (overshoot_y)
    *overshoot_y = y;

  return (x != 0 || y != 0);
}

static void
_gtk_scrolled_window_allocate_overshoot_window (GtkScrolledWindow *scrolled_window)
{
  GtkAllocation window_allocation, relative_allocation, allocation;
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkWidget *widget = GTK_WIDGET (scrolled_window);
  gint overshoot_x, overshoot_y;

  if (!gtk_widget_get_realized (widget))
    return;

  gtk_widget_get_allocation (widget, &allocation);
  gtk_scrolled_window_relative_allocation (widget, &relative_allocation);
  _gtk_scrolled_window_get_overshoot (scrolled_window,
                                      &overshoot_x, &overshoot_y);

  window_allocation = relative_allocation;
  window_allocation.x += allocation.x;
  window_allocation.y += allocation.y;

  /* Handle displacement to the left/top by moving the overshoot
   * window, overshooting to the bottom/right is handled in
   * gtk_scrolled_window_allocate_child()
   */
  if (overshoot_x < 0)
    window_allocation.x += -overshoot_x;

  if (overshoot_y < 0)
    window_allocation.y += -overshoot_y;

  window_allocation.width -= ABS (overshoot_x);
  window_allocation.height -= ABS (overshoot_y);

  gdk_window_move_resize (priv->overshoot_window,
                          window_allocation.x, window_allocation.y,
                          window_allocation.width, window_allocation.height);
}

static void
gtk_scrolled_window_allocate_child (GtkScrolledWindow *swindow,
				    GtkAllocation     *relative_allocation)
{
  GtkWidget     *widget = GTK_WIDGET (swindow), *child;
  GtkAllocation  allocation;
  GtkAllocation  child_allocation;
  gint           overshoot_x, overshoot_y;

  child = gtk_bin_get_child (GTK_BIN (widget));

  gtk_widget_get_allocation (widget, &allocation);

  gtk_scrolled_window_relative_allocation (widget, relative_allocation);
  _gtk_scrolled_window_get_overshoot (swindow, &overshoot_x, &overshoot_y);

  /* Handle overshooting to the right/bottom by relocating the
   * widget allocation to negative coordinates, so these edges
   * stick to the overshoot window border.
   */
  if (overshoot_x > 0)
    child_allocation.x = -overshoot_x;
  else
    child_allocation.x = 0;

  if (overshoot_y > 0)
    child_allocation.y = -overshoot_y;
  else
    child_allocation.y = 0;

  child_allocation.width = relative_allocation->width;
  child_allocation.height = relative_allocation->height;

  gtk_widget_size_allocate (child, &child_allocation);
}

static void
gtk_scrolled_window_size_allocate (GtkWidget     *widget,
				   GtkAllocation *allocation)
{
  GtkScrolledWindow *scrolled_window;
  GtkScrolledWindowPrivate *priv;
  GtkStyleContext *context;
  GtkStateFlags state;
  GtkBorder padding, border;
  GtkBin *bin;
  GtkAllocation relative_allocation;
  GtkAllocation child_allocation;
  GtkWidget *child;
  gboolean scrollbars_within_bevel;
  gint sb_spacing;
  gint sb_width;
  gint sb_height;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (widget));
  g_return_if_fail (allocation != NULL);

  scrolled_window = GTK_SCROLLED_WINDOW (widget);
  bin = GTK_BIN (scrolled_window);
  priv = scrolled_window->priv;

  /* Get possible scrollbar dimensions */
  sb_spacing = _gtk_scrolled_window_get_scrollbar_spacing (scrolled_window);
  gtk_widget_get_preferred_height (priv->hscrollbar, &sb_height, NULL);
  gtk_widget_get_preferred_width (priv->vscrollbar, &sb_width, NULL);

  context = gtk_widget_get_style_context (widget);
  state = gtk_widget_get_state_flags (widget);

  gtk_style_context_save (context);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_FRAME);

  gtk_style_context_get_padding (context, state, &padding);
  gtk_style_context_get_border (context, state, &border);

  gtk_widget_style_get (widget, "scrollbars-within-bevel", &scrollbars_within_bevel, NULL);

  gtk_widget_set_allocation (widget, allocation);
  gtk_style_context_restore (context);

  if (priv->hscrollbar_policy == GTK_POLICY_ALWAYS)
    priv->hscrollbar_visible = TRUE;
  else if (priv->hscrollbar_policy == GTK_POLICY_NEVER)
    priv->hscrollbar_visible = FALSE;
  if (priv->vscrollbar_policy == GTK_POLICY_ALWAYS)
    priv->vscrollbar_visible = TRUE;
  else if (priv->vscrollbar_policy == GTK_POLICY_NEVER)
    priv->vscrollbar_visible = FALSE;

  child = gtk_bin_get_child (bin);
  if (child && gtk_widget_get_visible (child))
    {
      gint child_scroll_width;
      gint child_scroll_height;
      gboolean previous_hvis;
      gboolean previous_vvis;
      guint count = 0;

      /* Determine scrollbar visibility first via hfw apis */
      if (gtk_widget_get_request_mode (child) == GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH)
	{
	  if (gtk_scrollable_get_hscroll_policy (GTK_SCROLLABLE (child)) == GTK_SCROLL_MINIMUM)
	    gtk_widget_get_preferred_width (child, &child_scroll_width, NULL);
	  else
	    gtk_widget_get_preferred_width (child, NULL, &child_scroll_width);
	  
	  if (priv->vscrollbar_policy == GTK_POLICY_AUTOMATIC)
	    {
	      /* First try without a vertical scrollbar if the content will fit the height
	       * given the extra width of the scrollbar */
	      if (gtk_scrollable_get_vscroll_policy (GTK_SCROLLABLE (child)) == GTK_SCROLL_MINIMUM)
		gtk_widget_get_preferred_height_for_width (child, 
							   MAX (allocation->width, child_scroll_width), 
							   &child_scroll_height, NULL);
	      else
		gtk_widget_get_preferred_height_for_width (child,
							   MAX (allocation->width, child_scroll_width), 
							   NULL, &child_scroll_height);
	      
	      if (priv->hscrollbar_policy == GTK_POLICY_AUTOMATIC)
		{
		  /* Does the content height fit the allocation height ? */
		  priv->vscrollbar_visible = child_scroll_height > allocation->height;
		  
		  /* Does the content width fit the allocation with minus a possible scrollbar ? */
		  priv->hscrollbar_visible = 
		    child_scroll_width > allocation->width - 
		    (priv->vscrollbar_visible ? sb_width + sb_spacing : 0);
		  
		  /* Now that we've guessed the hscrollbar, does the content height fit
		   * the possible new allocation height ? */
		  priv->vscrollbar_visible = 
		    child_scroll_height > allocation->height - 
		    (priv->hscrollbar_visible ? sb_height + sb_spacing : 0);
		  
		  /* Now that we've guessed the vscrollbar, does the content width fit
		   * the possible new allocation width ? */
		  priv->hscrollbar_visible = 
		    child_scroll_width > allocation->width - 
		    (priv->vscrollbar_visible ? sb_width + sb_spacing : 0);
		}
	      else /* priv->hscrollbar_policy != GTK_POLICY_AUTOMATIC */
		{
		  priv->hscrollbar_visible = priv->hscrollbar_policy != GTK_POLICY_NEVER;
		  priv->vscrollbar_visible = child_scroll_height > allocation->height - 
		    (priv->hscrollbar_visible ? sb_height + sb_spacing : 0);
		}
	    }
	  else /* priv->vscrollbar_policy != GTK_POLICY_AUTOMATIC */
	    {
	      priv->vscrollbar_visible = priv->vscrollbar_policy != GTK_POLICY_NEVER;
	      
	      if (priv->hscrollbar_policy == GTK_POLICY_AUTOMATIC)
		priv->hscrollbar_visible = 
		  child_scroll_width > allocation->width - 
		  (priv->vscrollbar_visible ? 0 : sb_width + sb_spacing);
	      else
		priv->hscrollbar_visible = priv->hscrollbar_policy != GTK_POLICY_NEVER;
	    }
	} 
      else /* GTK_SIZE_REQUEST_WIDTH_FOR_HEIGHT */
	{
	  if (gtk_scrollable_get_vscroll_policy (GTK_SCROLLABLE (child)) == GTK_SCROLL_MINIMUM)
	    gtk_widget_get_preferred_height (child, &child_scroll_height, NULL);
	  else
	    gtk_widget_get_preferred_height (child, NULL, &child_scroll_height);
	  
	  if (priv->hscrollbar_policy == GTK_POLICY_AUTOMATIC)
	    {
	      /* First try without a horizontal scrollbar if the content will fit the width
	       * given the extra height of the scrollbar */
	      if (gtk_scrollable_get_hscroll_policy (GTK_SCROLLABLE (child)) == GTK_SCROLL_MINIMUM)
		gtk_widget_get_preferred_width_for_height (child, 
							   MAX (allocation->height, child_scroll_height), 
							   &child_scroll_width, NULL);
	      else
		gtk_widget_get_preferred_width_for_height (child, 
							   MAX (allocation->height, child_scroll_height), 
							   NULL, &child_scroll_width);
	      
	      if (priv->vscrollbar_policy == GTK_POLICY_AUTOMATIC)
		{
		  /* Does the content width fit the allocation width ? */
		  priv->hscrollbar_visible = child_scroll_width > allocation->width;
		  
		  /* Does the content height fit the allocation with minus a possible scrollbar ? */
		  priv->vscrollbar_visible = 
		    child_scroll_height > allocation->height - 
		    (priv->hscrollbar_visible ? sb_height + sb_spacing : 0);
		  
		  /* Now that we've guessed the vscrollbar, does the content width fit
		   * the possible new allocation width ? */
		  priv->hscrollbar_visible = 
		    child_scroll_width > allocation->width - 
		    (priv->vscrollbar_visible ? sb_width + sb_spacing : 0);
		  
		  /* Now that we've guessed the hscrollbar, does the content height fit
		   * the possible new allocation height ? */
		  priv->vscrollbar_visible = 
		    child_scroll_height > allocation->height - 
		    (priv->hscrollbar_visible ? sb_height + sb_spacing : 0);
		}
	      else /* priv->vscrollbar_policy != GTK_POLICY_AUTOMATIC */
		{
		  priv->vscrollbar_visible = priv->vscrollbar_policy != GTK_POLICY_NEVER;
		  priv->hscrollbar_visible = child_scroll_width > allocation->width - 
		    (priv->vscrollbar_visible ? sb_width + sb_spacing : 0);
		}
	    }
	  else /* priv->hscrollbar_policy != GTK_POLICY_AUTOMATIC */
	    {
	      priv->hscrollbar_visible = priv->hscrollbar_policy != GTK_POLICY_NEVER;
	      
	      if (priv->vscrollbar_policy == GTK_POLICY_AUTOMATIC)
		priv->vscrollbar_visible = 
		  child_scroll_height > allocation->height - 
		  (priv->hscrollbar_visible ? 0 : sb_height + sb_spacing);
	      else
		priv->vscrollbar_visible = priv->vscrollbar_policy != GTK_POLICY_NEVER;
	    }
	}

      /* Now after guessing scrollbar visibility; fall back on the allocation loop which 
       * observes the adjustments to detect scrollbar visibility and also avoids 
       * infinite recursion
       */
      do
	{
	  previous_hvis = priv->hscrollbar_visible;
	  previous_vvis = priv->vscrollbar_visible;
	  gtk_scrolled_window_allocate_child (scrolled_window, &relative_allocation);

	  /* Explicitly force scrollbar visibility checks.
	   *
	   * Since we make a guess above, the child might not decide to update the adjustments 
	   * if they logically did not change since the last configuration
	   */
	  if (priv->hscrollbar)
	    gtk_scrolled_window_adjustment_changed 
	      (gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar)), scrolled_window);

	  if (priv->vscrollbar)
	    gtk_scrolled_window_adjustment_changed 
	      (gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar)), scrolled_window);

	  /* If, after the first iteration, the hscrollbar and the
	   * vscrollbar flip visiblity... or if one of the scrollbars flip
	   * on each itteration indefinitly/infinitely, then we just need both 
	   * at this size.
	   */
	  if ((count &&
	       previous_hvis != priv->hscrollbar_visible &&
	       previous_vvis != priv->vscrollbar_visible) || count > 3)
	    {
	      priv->hscrollbar_visible = TRUE;
	      priv->vscrollbar_visible = TRUE;

	      gtk_scrolled_window_allocate_child (scrolled_window, &relative_allocation);

	      break;
	    }
	  
	  count++;
	}
      while (previous_hvis != priv->hscrollbar_visible ||
	     previous_vvis != priv->vscrollbar_visible);
    }
  else
    {
      priv->hscrollbar_visible = priv->hscrollbar_policy == GTK_POLICY_ALWAYS;
      priv->vscrollbar_visible = priv->vscrollbar_policy == GTK_POLICY_ALWAYS;
      gtk_scrolled_window_relative_allocation (widget, &relative_allocation);
    }

  if (priv->hscrollbar_visible)
    {
      if (!gtk_widget_get_visible (priv->hscrollbar))
	gtk_widget_show (priv->hscrollbar);

      child_allocation.x = relative_allocation.x;
      if (priv->real_window_placement == GTK_CORNER_TOP_LEFT ||
	  priv->real_window_placement == GTK_CORNER_TOP_RIGHT)
	child_allocation.y = (relative_allocation.y +
			      relative_allocation.height +
			      sb_spacing);
      else
	child_allocation.y = relative_allocation.y - sb_spacing - sb_height;

      child_allocation.width = relative_allocation.width;
      child_allocation.height = sb_height;
      child_allocation.x += allocation->x;
      child_allocation.y += allocation->y;

      if (priv->shadow_type != GTK_SHADOW_NONE)
	{
          if (!scrollbars_within_bevel)
            {
              child_allocation.x -= padding.left + border.left;
              child_allocation.width += padding.left + padding.right + border.left + border.right;

              if (priv->real_window_placement == GTK_CORNER_TOP_LEFT ||
                  priv->real_window_placement == GTK_CORNER_TOP_RIGHT)
                child_allocation.y += padding.bottom + border.bottom;
              else
                child_allocation.y -= padding.top + border.top;
            }
	}

      gtk_widget_size_allocate (priv->hscrollbar, &child_allocation);
    }
  else if (gtk_widget_get_visible (priv->hscrollbar))
    gtk_widget_hide (priv->hscrollbar);

  if (priv->vscrollbar_visible)
    {
      if (!gtk_widget_get_visible (priv->vscrollbar))
	gtk_widget_show (priv->vscrollbar);

      if ((gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL && 
	   (priv->real_window_placement == GTK_CORNER_TOP_RIGHT ||
	    priv->real_window_placement == GTK_CORNER_BOTTOM_RIGHT)) ||
	  (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR && 
	   (priv->real_window_placement == GTK_CORNER_TOP_LEFT ||
	    priv->real_window_placement == GTK_CORNER_BOTTOM_LEFT)))
	child_allocation.x = (relative_allocation.x +
			      relative_allocation.width +
			      sb_spacing);
      else
	child_allocation.x = relative_allocation.x - sb_spacing - sb_width;

      child_allocation.y = relative_allocation.y;
      child_allocation.width = sb_width;
      child_allocation.height = relative_allocation.height;
      child_allocation.x += allocation->x;
      child_allocation.y += allocation->y;

      if (priv->shadow_type != GTK_SHADOW_NONE)
	{
          if (!scrollbars_within_bevel)
            {
              child_allocation.y -= padding.top + border.top;
	      child_allocation.height += padding.top + padding.bottom + border.top + border.bottom;

              if ((gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL &&
                   (priv->real_window_placement == GTK_CORNER_TOP_RIGHT ||
                    priv->real_window_placement == GTK_CORNER_BOTTOM_RIGHT)) ||
                  (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR &&
                   (priv->real_window_placement == GTK_CORNER_TOP_LEFT ||
                    priv->real_window_placement == GTK_CORNER_BOTTOM_LEFT)))
                child_allocation.x += padding.right + border.right;
              else
                child_allocation.x -= padding.left + border.left;
            }
        }

      gtk_widget_size_allocate (priv->vscrollbar, &child_allocation);
    }
  else if (gtk_widget_get_visible (priv->vscrollbar))
    gtk_widget_hide (priv->vscrollbar);

  _gtk_scrolled_window_allocate_overshoot_window (scrolled_window);
}

static gboolean
gtk_scrolled_window_scroll_event (GtkWidget      *widget,
				  GdkEventScroll *event)
{
  GtkScrolledWindowPrivate *priv;
  GtkScrolledWindow *scrolled_window;
  gboolean handled = FALSE;
  gdouble delta_x;
  gdouble delta_y;
  gdouble delta;

  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  scrolled_window = GTK_SCROLLED_WINDOW (widget);
  priv = scrolled_window->priv;

  if (gdk_event_get_scroll_deltas ((GdkEvent *) event, &delta_x, &delta_y))
    {
      if (delta_x != 0.0 && priv->hscrollbar &&
          gtk_widget_get_visible (priv->hscrollbar))
        {
          GtkAdjustment *adj;
          gdouble new_value;
          gdouble page_size;
          gdouble scroll_unit;

          adj = gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar));
          page_size = gtk_adjustment_get_page_size (adj);
          scroll_unit = pow (page_size, 2.0 / 3.0);

          new_value = CLAMP (gtk_adjustment_get_value (adj) + delta_x * scroll_unit,
                             gtk_adjustment_get_lower (adj),
                             gtk_adjustment_get_upper (adj) -
                             gtk_adjustment_get_page_size (adj));

          gtk_adjustment_set_value (adj, new_value);

          handled = TRUE;
        }

      if (delta_y != 0.0 && priv->vscrollbar &&
          gtk_widget_get_visible (priv->vscrollbar))
        {
          GtkAdjustment *adj;
          gdouble new_value;
          gdouble page_size;
          gdouble scroll_unit;

          adj = gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar));
          page_size = gtk_adjustment_get_page_size (adj);
          scroll_unit = pow (page_size, 2.0 / 3.0);

          new_value = CLAMP (gtk_adjustment_get_value (adj) + delta_y * scroll_unit,
                             gtk_adjustment_get_lower (adj),
                             gtk_adjustment_get_upper (adj) -
                             gtk_adjustment_get_page_size (adj));

          gtk_adjustment_set_value (adj, new_value);

          handled = TRUE;
        }
    }
  else
    {
      GtkWidget *range;

      if (event->direction == GDK_SCROLL_UP || event->direction == GDK_SCROLL_DOWN)
        range = priv->vscrollbar;
      else
        range = priv->hscrollbar;

      if (range && gtk_widget_get_visible (range))
        {
          GtkAdjustment *adj = gtk_range_get_adjustment (GTK_RANGE (range));
          gdouble new_value;

          delta = _gtk_range_get_wheel_delta (GTK_RANGE (range), event);

          new_value = CLAMP (gtk_adjustment_get_value (adj) + delta,
                             gtk_adjustment_get_lower (adj),
                             gtk_adjustment_get_upper (adj) -
                             gtk_adjustment_get_page_size (adj));

          gtk_adjustment_set_value (adj, new_value);

          handled = TRUE;
        }
    }

  return handled;
}

static gboolean
_gtk_scrolled_window_set_adjustment_value (GtkScrolledWindow *scrolled_window,
                                           GtkAdjustment     *adjustment,
                                           gdouble            value,
                                           gboolean           allow_overshooting,
                                           gboolean           snap_to_border)
{
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  gdouble lower, upper, *prev_value;

  lower = gtk_adjustment_get_lower (adjustment);
  upper = gtk_adjustment_get_upper (adjustment) -
    gtk_adjustment_get_page_size (adjustment);

  if (adjustment == gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar)))
    prev_value = &priv->unclamped_hadj_value;
  else if (adjustment == gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar)))
    prev_value = &priv->unclamped_vadj_value;
  else
    return FALSE;

  if (snap_to_border)
    {
      if (*prev_value < 0 && value > 0)
        value = 0;
      else if (*prev_value > upper && value < upper)
        value = upper;
    }

  if (allow_overshooting)
    {
      lower -= MAX_OVERSHOOT_DISTANCE;
      upper += MAX_OVERSHOOT_DISTANCE;
    }

  *prev_value = CLAMP (value, lower, upper);
  gtk_adjustment_set_value (adjustment, value);

  return (*prev_value != value);
}

static gboolean
scrolled_window_deceleration_cb (gpointer user_data)
{
  KineticScrollData *data = user_data;
  GtkScrolledWindow *scrolled_window = data->scrolled_window;
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkAdjustment *hadjustment, *vadjustment;
  gint old_overshoot_x, old_overshoot_y, overshoot_x, overshoot_y;
  gdouble value;
  gint64 current_time;
  guint elapsed;

  hadjustment = gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar));
  vadjustment = gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar));

  _gtk_scrolled_window_get_overshoot (scrolled_window,
                                      &old_overshoot_x, &old_overshoot_y);

  current_time = g_get_monotonic_time ();
  elapsed = (current_time - data->last_deceleration_time) / 1000;
  data->last_deceleration_time = current_time;

  if (hadjustment && priv->hscrollbar_visible)
    {
      value = priv->unclamped_hadj_value + (data->x_velocity * elapsed);

      if (_gtk_scrolled_window_set_adjustment_value (scrolled_window,
                                                     hadjustment,
                                                     value, TRUE, TRUE))
        data->x_velocity = 0;
    }
  else
    data->x_velocity = 0;

  if (vadjustment && priv->vscrollbar_visible)
    {
      value = priv->unclamped_vadj_value + (data->y_velocity * elapsed);

      if (_gtk_scrolled_window_set_adjustment_value (scrolled_window,
                                                     vadjustment,
                                                     value, TRUE, TRUE))
        data->y_velocity = 0;
    }
  else
    data->y_velocity = 0;

  _gtk_scrolled_window_get_overshoot (scrolled_window,
                                      &overshoot_x, &overshoot_y);

  if (overshoot_x == 0)
    {
      if (old_overshoot_x != 0)
        {
          /* Overshooting finished snapping back */
          data->x_velocity = 0;
        }
      else if (data->x_velocity > 0)
        {
          data->x_velocity -= FRICTION_DECELERATION * elapsed * data->vel_sine;
          data->x_velocity = MAX (0, data->x_velocity);
        }
      else if (data->x_velocity < 0)
        {
          data->x_velocity += FRICTION_DECELERATION * elapsed * data->vel_sine;
          data->x_velocity = MIN (0, data->x_velocity);
        }
    }
  else if (overshoot_x < 0)
    data->x_velocity += OVERSHOOT_INVERSE_ACCELERATION * elapsed;
  else if (overshoot_x > 0)
    data->x_velocity -= OVERSHOOT_INVERSE_ACCELERATION * elapsed;

  if (overshoot_y == 0)
    {
      if (old_overshoot_y != 0)
        {
          /* Overshooting finished snapping back */
          data->y_velocity = 0;
        }
      else if (data->y_velocity > 0)
        {
          data->y_velocity -= FRICTION_DECELERATION * elapsed * data->vel_cosine;
          data->y_velocity = MAX (0, data->y_velocity);
        }
      else if (data->y_velocity < 0)
        {
          data->y_velocity += FRICTION_DECELERATION * elapsed * data->vel_cosine;
          data->y_velocity = MIN (0, data->y_velocity);
        }
    }
  else if (overshoot_y < 0)
    data->y_velocity += OVERSHOOT_INVERSE_ACCELERATION * elapsed;
  else if (overshoot_y > 0)
    data->y_velocity -= OVERSHOOT_INVERSE_ACCELERATION * elapsed;

  if (old_overshoot_x != overshoot_x ||
      old_overshoot_y != overshoot_y)
    {
      if (overshoot_x >= 0 || overshoot_y >= 0)
        {
          /* We need to reallocate the widget to have it at
           * negative offset, so there's a "gravity" on the
           * bottom/right corner
           */
          gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));
        }
      else if (overshoot_x < 0 || overshoot_y < 0)
        _gtk_scrolled_window_allocate_overshoot_window (scrolled_window);
    }

  if (overshoot_x != 0 || overshoot_y != 0 ||
      data->x_velocity != 0 || data->y_velocity != 0)
    return TRUE;
  else
    {
      priv->deceleration_id = 0;
      return FALSE;
    }
}

static void
gtk_scrolled_window_cancel_deceleration (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;

  if (priv->deceleration_id)
    {
      g_source_remove (priv->deceleration_id);
      priv->deceleration_id = 0;
    }
}

static void
gtk_scrolled_window_start_deceleration (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  KineticScrollData *data;
  gdouble angle;

  data = g_new0 (KineticScrollData, 1);
  data->scrolled_window = scrolled_window;
  data->last_deceleration_time = g_get_monotonic_time ();
  data->x_velocity = priv->x_velocity;
  data->y_velocity = priv->y_velocity;

  /* We use sine/cosine as a factor to deceleration x/y components
   * of the vector, so we care about the sign later.
   */
  angle = atan2 (ABS (data->x_velocity), ABS (data->y_velocity));
  data->vel_cosine = cos (angle);
  data->vel_sine = sin (angle);

  scrolled_window->priv->deceleration_id =
    gdk_threads_add_timeout_full (G_PRIORITY_DEFAULT,
                                  FRAME_INTERVAL,
                                  scrolled_window_deceleration_cb,
                                  data, (GDestroyNotify) g_free);
}

static gboolean
gtk_scrolled_window_release_captured_event (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;

  /* Cancel the scrolling and send the button press
   * event to the child widget
   */
  if (!priv->button_press_event)
    return FALSE;

  if (priv->drag_device)
    {
      gtk_device_grab_remove (GTK_WIDGET (scrolled_window), priv->drag_device);
      priv->drag_device = NULL;
    }

  if (priv->capture_button_press)
    {
      GtkWidget *event_widget;

      event_widget = gtk_get_event_widget (priv->button_press_event);

      if (!_gtk_propagate_captured_event (event_widget,
                                          priv->button_press_event,
                                          gtk_bin_get_child (GTK_BIN (scrolled_window))))
        gtk_propagate_event (event_widget, priv->button_press_event);

      gdk_event_free (priv->button_press_event);
      priv->button_press_event = NULL;
    }

  if (_gtk_scrolled_window_get_overshoot (scrolled_window, NULL, NULL))
    gtk_scrolled_window_start_deceleration (scrolled_window);

  return FALSE;
}

static gboolean
gtk_scrolled_window_calculate_velocity (GtkScrolledWindow *scrolled_window,
					GdkEvent          *event)
{
  GtkScrolledWindowPrivate *priv;
  gdouble x_root, y_root;
  guint32 _time;

#define STILL_THRESHOLD 40

  if (!gdk_event_get_root_coords (event, &x_root, &y_root))
    return FALSE;

  priv = scrolled_window->priv;
  _time = gdk_event_get_time (event);

  if (priv->last_motion_event_x_root != x_root ||
      priv->last_motion_event_y_root != y_root ||
      ABS (_time - priv->last_motion_event_time) > STILL_THRESHOLD)
    {
      priv->x_velocity = (priv->last_motion_event_x_root - x_root) /
        (gdouble) (_time - priv->last_motion_event_time);
      priv->y_velocity = (priv->last_motion_event_y_root - y_root) /
        (gdouble) (_time - priv->last_motion_event_time);
    }

  priv->last_motion_event_x_root = x_root;
  priv->last_motion_event_y_root = y_root;
  priv->last_motion_event_time = _time;

#undef STILL_THRESHOLD

  return TRUE;
}

static gboolean
gtk_scrolled_window_captured_button_release (GtkWidget *widget,
                                             GdkEvent  *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkWidget *child;
  gboolean overshoot;
  guint button;
  gdouble x_root, y_root;

  if (gdk_event_get_button (event, &button) && button != 1)
    return FALSE;

  child = gtk_bin_get_child (GTK_BIN (widget));
  if (!child)
    return FALSE;

  gtk_device_grab_remove (widget, priv->drag_device);
  priv->drag_device = NULL;

  if (priv->release_timeout_id)
    {
      g_source_remove (priv->release_timeout_id);
      priv->release_timeout_id = 0;
    }

  overshoot = _gtk_scrolled_window_get_overshoot (scrolled_window, NULL, NULL);

  if (priv->in_drag)
    gdk_device_ungrab (gdk_event_get_device (event), gdk_event_get_time (event));
  else
    {
      /* There hasn't been scrolling at all, so just let the
       * child widget handle the button press normally
       */
      gtk_scrolled_window_release_captured_event (scrolled_window);

      if (!overshoot)
        return FALSE;
    }
  priv->in_drag = FALSE;

  if (priv->button_press_event)
    {
      gdk_event_free (priv->button_press_event);
      priv->button_press_event = NULL;
    }

  gtk_scrolled_window_calculate_velocity (scrolled_window, event);

  /* Zero out vector components without a visible scrollbar */
  if (!priv->hscrollbar_visible)
    priv->x_velocity = 0;
  if (!priv->vscrollbar_visible)
    priv->y_velocity = 0;

  if (priv->x_velocity != 0 || priv->y_velocity != 0 || overshoot)
    {
      gtk_scrolled_window_start_deceleration (scrolled_window);
      priv->x_velocity = priv->y_velocity = 0;
      priv->last_button_event_valid = FALSE;
    }
  else
    {
      gdk_event_get_root_coords (event, &x_root, &y_root);
      priv->last_button_event_x_root = x_root;
      priv->last_button_event_y_root = y_root;
      priv->last_button_event_valid = TRUE;
    }

  if (priv->capture_button_press)
    return TRUE;
  else
    return FALSE;
}

static gboolean
gtk_scrolled_window_captured_motion_notify (GtkWidget *widget,
                                            GdkEvent  *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  gint old_overshoot_x, old_overshoot_y;
  gint new_overshoot_x, new_overshoot_y;
  GtkWidget *child;
  GtkAdjustment *hadjustment;
  GtkAdjustment *vadjustment;
  gdouble dx, dy;
  GdkModifierType state;
  gdouble x_root, y_root;

  gdk_event_get_state (event, &state);
  if (!(state & GDK_BUTTON1_MASK))
    return FALSE;

  child = gtk_bin_get_child (GTK_BIN (widget));
  if (!child)
    return FALSE;

  /* Check if we've passed the drag threshold */
  gdk_event_get_root_coords (event, &x_root, &y_root);
  if (!priv->in_drag)
    {
      if (gtk_drag_check_threshold (widget,
                                    priv->last_button_event_x_root,
                                    priv->last_button_event_y_root,
                                    x_root, y_root))
        {
          if (priv->release_timeout_id)
            {
              g_source_remove (priv->release_timeout_id);
              priv->release_timeout_id = 0;
            }

          priv->last_button_event_valid = FALSE;
          priv->in_drag = TRUE;
        }
      else
        return TRUE;
    }

  gdk_device_grab (priv->drag_device,
                   gtk_widget_get_window (widget),
                   GDK_OWNERSHIP_WINDOW,
                   TRUE,
                   GDK_BUTTON_RELEASE_MASK | GDK_BUTTON1_MOTION_MASK,
                   NULL,
                   gdk_event_get_time (event));

  priv->last_button_event_valid = FALSE;

  if (priv->button_press_event)
    {
      gdk_event_free (priv->button_press_event);
      priv->button_press_event = NULL;
    }

  _gtk_scrolled_window_get_overshoot (scrolled_window,
                                      &old_overshoot_x, &old_overshoot_y);

  hadjustment = gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar));
  if (hadjustment && priv->hscrollbar_visible)
    {
      dx = (priv->last_motion_event_x_root - x_root) + priv->unclamped_hadj_value;
      _gtk_scrolled_window_set_adjustment_value (scrolled_window, hadjustment,
                                                 dx, TRUE, FALSE);
    }

  vadjustment = gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar));
  if (vadjustment && priv->vscrollbar_visible)
    {
      dy = (priv->last_motion_event_y_root - y_root) + priv->unclamped_vadj_value;
      _gtk_scrolled_window_set_adjustment_value (scrolled_window, vadjustment,
                                                 dy, TRUE, FALSE);
    }

  _gtk_scrolled_window_get_overshoot (scrolled_window,
                                      &new_overshoot_x, &new_overshoot_y);

  if (old_overshoot_x != new_overshoot_x ||
      old_overshoot_y != new_overshoot_y)
    {
      if (new_overshoot_x >= 0 || new_overshoot_y >= 0)
        {
          /* We need to reallocate the widget to have it at
           * negative offset, so there's a "gravity" on the
           * bottom/right corner
           */
          gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));
        }
      else if (new_overshoot_x < 0 || new_overshoot_y < 0)
        _gtk_scrolled_window_allocate_overshoot_window (scrolled_window);
    }

  gtk_scrolled_window_calculate_velocity (scrolled_window, event);

  return TRUE;
}

static gboolean
gtk_scrolled_window_captured_button_press (GtkWidget *widget,
                                           GdkEvent  *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkWidget *child;
  GtkWidget *event_widget;
  GdkDevice *source_device;
  GdkInputSource source;
  gdouble x_root, y_root;
  guint button;

  /* If scrollbars are not visible, we don't do kinetic scrolling */
  if (!priv->vscrollbar_visible && !priv->hscrollbar_visible)
    return FALSE;

  source_device = gdk_event_get_source_device (event);
  source = gdk_device_get_source (source_device);

  if (source != GDK_SOURCE_TOUCHSCREEN)
    return FALSE;

  event_widget = gtk_get_event_widget (event);

  /* If there's another scrolled window between the widget
   * receiving the event and this capturing scrolled window,
   * let it handle the events.
   */
  if (widget != gtk_widget_get_ancestor (event_widget, GTK_TYPE_SCROLLED_WINDOW))
    return FALSE;

  /* Check whether the button press is close to the previous one,
   * take that as a shortcut to get the child widget handle events
   */
  gdk_event_get_root_coords (event, &x_root, &y_root);
  if (priv->last_button_event_valid &&
      ABS (x_root - priv->last_button_event_x_root) < TOUCH_BYPASS_CAPTURED_THRESHOLD &&
      ABS (y_root - priv->last_button_event_y_root) < TOUCH_BYPASS_CAPTURED_THRESHOLD)
    {
      priv->last_button_event_valid = FALSE;
      return FALSE;
    }

  priv->last_button_event_x_root = priv->last_motion_event_x_root = x_root;
  priv->last_button_event_y_root = priv->last_motion_event_y_root = y_root;
  priv->last_motion_event_time = gdk_event_get_time (event);
  priv->last_button_event_valid = TRUE;

  if (gdk_event_get_button (event, &button) && button != 1)
    return FALSE;

  child = gtk_bin_get_child (GTK_BIN (widget));
  if (!child)
    return FALSE;

  if (priv->hscrollbar == event_widget || priv->vscrollbar == event_widget)
    return FALSE;

  priv->drag_device = gdk_event_get_device (event);
  gtk_device_grab_add (widget, priv->drag_device, TRUE);

  gtk_scrolled_window_cancel_deceleration (scrolled_window);

  /* Only set the timeout if we're going to store an event */
  if (priv->capture_button_press)
    priv->release_timeout_id =
      gdk_threads_add_timeout (RELEASE_EVENT_TIMEOUT,
                               (GSourceFunc) gtk_scrolled_window_release_captured_event,
                               scrolled_window);

  priv->in_drag = FALSE;

  if (priv->capture_button_press)
    {
      /* Store the button press event in
       * case we need to propagate it later
       */
      priv->button_press_event = gdk_event_copy (event);
      return TRUE;
    }
  else
    return FALSE;
}

static gboolean
gtk_scrolled_window_captured_event (GtkWidget *widget,
                                    GdkEvent  *event)
{
  gboolean retval = FALSE;
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW (widget)->priv;

  if (gdk_window_get_window_type (event->any.window) == GDK_WINDOW_TEMP)
    return FALSE;

  switch (event->type)
    {
    case GDK_TOUCH_BEGIN:
    case GDK_BUTTON_PRESS:
      retval = gtk_scrolled_window_captured_button_press (widget, event);
      break;
    case GDK_TOUCH_END:
    case GDK_BUTTON_RELEASE:
      if (priv->drag_device)
        retval = gtk_scrolled_window_captured_button_release (widget, event);
      else
        priv->last_button_event_valid = FALSE;
      break;
    case GDK_TOUCH_UPDATE:
    case GDK_MOTION_NOTIFY:
      if (priv->drag_device)
        retval = gtk_scrolled_window_captured_motion_notify (widget, event);
      break;
    case GDK_LEAVE_NOTIFY:
    case GDK_ENTER_NOTIFY:
      if (priv->in_drag &&
          event->crossing.mode != GDK_CROSSING_GRAB)
        retval = TRUE;
      break;
    default:
      break;
    }

  return retval;
}

static gboolean
gtk_scrolled_window_focus (GtkWidget        *widget,
			   GtkDirectionType  direction)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkWidget *child;
  gboolean had_focus_child;

  had_focus_child = gtk_container_get_focus_child (GTK_CONTAINER (widget)) != NULL;

  if (priv->focus_out)
    {
      priv->focus_out = FALSE; /* Clear this to catch the wrap-around case */
      return FALSE;
    }
  
  if (gtk_widget_is_focus (widget))
    return FALSE;

  /* We only put the scrolled window itself in the focus chain if it
   * isn't possible to focus any children.
   */
  child = gtk_bin_get_child (GTK_BIN (widget));
  if (child)
    {
      if (gtk_widget_child_focus (child, direction))
	return TRUE;
    }

  if (!had_focus_child && gtk_widget_get_can_focus (widget))
    {
      gtk_widget_grab_focus (widget);
      return TRUE;
    }
  else
    return FALSE;
}

static void
gtk_scrolled_window_adjustment_changed (GtkAdjustment *adjustment,
					gpointer       data)
{
  GtkScrolledWindowPrivate *priv;
  GtkScrolledWindow *scrolled_window;

  g_return_if_fail (adjustment != NULL);
  g_return_if_fail (data != NULL);

  scrolled_window = GTK_SCROLLED_WINDOW (data);
  priv = scrolled_window->priv;

  if (priv->hscrollbar &&
      adjustment == gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar)))
    {
      if (priv->hscrollbar_policy == GTK_POLICY_AUTOMATIC)
	{
	  gboolean visible;

	  visible = priv->hscrollbar_visible;
	  priv->hscrollbar_visible = (gtk_adjustment_get_upper (adjustment) - gtk_adjustment_get_lower (adjustment) >
				      gtk_adjustment_get_page_size (adjustment));

	  if (priv->hscrollbar_visible != visible)
	    gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));
	}
    }
  else if (priv->vscrollbar &&
	   adjustment == gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar)))
    {
      if (priv->vscrollbar_policy == GTK_POLICY_AUTOMATIC)
	{
	  gboolean visible;

	  visible = priv->vscrollbar_visible;
	  priv->vscrollbar_visible = (gtk_adjustment_get_upper (adjustment) - gtk_adjustment_get_lower (adjustment) >
			              gtk_adjustment_get_page_size (adjustment));

	  if (priv->vscrollbar_visible != visible)
	    gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));
	}
    }
}

static void
gtk_scrolled_window_adjustment_value_changed (GtkAdjustment *adjustment,
                                              gpointer       user_data)
{
  GtkScrolledWindow *scrolled_window = user_data;
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;

  /* Allow overshooting for kinetic scrolling operations */
  if (priv->drag_device || priv->deceleration_id)
    return;

  /* Ensure GtkAdjustment and unclamped values are in sync */
  if (priv->vscrollbar &&
      adjustment == gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar)))
    priv->unclamped_vadj_value = gtk_adjustment_get_value (adjustment);
  else if (priv->hscrollbar &&
           adjustment == gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar)))
    priv->unclamped_hadj_value = gtk_adjustment_get_value (adjustment);
}

static void
gtk_scrolled_window_add (GtkContainer *container,
                         GtkWidget    *child)
{
  GtkScrolledWindowPrivate *priv;
  GtkScrolledWindow *scrolled_window;
  GtkBin *bin;
  GtkWidget *child_widget;
  GtkAdjustment *hadj, *vadj;

  bin = GTK_BIN (container);
  child_widget = gtk_bin_get_child (bin);
  g_return_if_fail (child_widget == NULL);

  scrolled_window = GTK_SCROLLED_WINDOW (container);
  priv = scrolled_window->priv;

  if (gtk_widget_get_realized (GTK_WIDGET (bin)))
    gtk_widget_set_parent_window (child, priv->overshoot_window);

  _gtk_bin_set_child (bin, child);
  gtk_widget_set_parent (child, GTK_WIDGET (bin));

  hadj = gtk_range_get_adjustment (GTK_RANGE (priv->hscrollbar));
  vadj = gtk_range_get_adjustment (GTK_RANGE (priv->vscrollbar));

  if (GTK_IS_SCROLLABLE (child))
    g_object_set (child, "hadjustment", hadj, "vadjustment", vadj, NULL);
  else
    g_warning ("gtk_scrolled_window_add(): cannot add non scrollable widget "
               "use gtk_scrolled_window_add_with_viewport() instead");
}

static void
gtk_scrolled_window_remove (GtkContainer *container,
			    GtkWidget    *child)
{
  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (container));
  g_return_if_fail (child != NULL);
  g_return_if_fail (gtk_bin_get_child (GTK_BIN (container)) == child);

  g_object_set (child, "hadjustment", NULL, "vadjustment", NULL, NULL);

  /* chain parent class handler to remove child */
  GTK_CONTAINER_CLASS (gtk_scrolled_window_parent_class)->remove (container, child);
}

/**
 * gtk_scrolled_window_add_with_viewport:
 * @scrolled_window: a #GtkScrolledWindow
 * @child: the widget you want to scroll
 *
 * Used to add children without native scrolling capabilities. This
 * is simply a convenience function; it is equivalent to adding the
 * unscrollable child to a viewport, then adding the viewport to the
 * scrolled window. If a child has native scrolling, use
 * gtk_container_add() instead of this function.
 *
 * The viewport scrolls the child by moving its #GdkWindow, and takes
 * the size of the child to be the size of its toplevel #GdkWindow. 
 * This will be very wrong for most widgets that support native scrolling;
 * for example, if you add a widget such as #GtkTreeView with a viewport,
 * the whole widget will scroll, including the column headings. Thus, 
 * widgets with native scrolling support should not be used with the 
 * #GtkViewport proxy.
 *
 * A widget supports scrolling natively if it implements the
 * #GtkScrollable interface.
 */
void
gtk_scrolled_window_add_with_viewport (GtkScrolledWindow *scrolled_window,
				       GtkWidget         *child)
{
  GtkBin *bin;
  GtkWidget *viewport;
  GtkWidget *child_widget;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));
  g_return_if_fail (GTK_IS_WIDGET (child));
  g_return_if_fail (gtk_widget_get_parent (child) == NULL);

  bin = GTK_BIN (scrolled_window);
  child_widget = gtk_bin_get_child (bin);

  if (child_widget)
    {
      g_return_if_fail (GTK_IS_VIEWPORT (child_widget));
      g_return_if_fail (gtk_bin_get_child (GTK_BIN (child_widget)) == NULL);

      viewport = child_widget;
    }
  else
    {
      viewport =
        gtk_viewport_new (gtk_scrolled_window_get_hadjustment (scrolled_window),
                          gtk_scrolled_window_get_vadjustment (scrolled_window));
      gtk_container_add (GTK_CONTAINER (scrolled_window), viewport);
    }

  gtk_widget_show (viewport);
  gtk_container_add (GTK_CONTAINER (viewport), child);
}

/*
 * _gtk_scrolled_window_get_spacing:
 * @scrolled_window: a scrolled window
 * 
 * Gets the spacing between the scrolled window's scrollbars and
 * the scrolled widget. Used by GtkCombo
 * 
 * Return value: the spacing, in pixels.
 */
gint
_gtk_scrolled_window_get_scrollbar_spacing (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowClass *class;
    
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), 0);

  class = GTK_SCROLLED_WINDOW_GET_CLASS (scrolled_window);

  if (class->scrollbar_spacing >= 0)
    return class->scrollbar_spacing;
  else
    {
      gint scrollbar_spacing;
      
      gtk_widget_style_get (GTK_WIDGET (scrolled_window),
			    "scrollbar-spacing", &scrollbar_spacing,
			    NULL);

      return scrollbar_spacing;
    }
}


static void
gtk_scrolled_window_get_preferred_size (GtkWidget      *widget,
                                        GtkOrientation  orientation,
                                        gint           *minimum_size,
                                        gint           *natural_size)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;
  GtkBin *bin = GTK_BIN (scrolled_window);
  gint extra_width;
  gint extra_height;
  gint scrollbar_spacing;
  GtkRequisition hscrollbar_requisition;
  GtkRequisition vscrollbar_requisition;
  GtkRequisition minimum_req, natural_req;
  GtkWidget *child;
  gint min_child_size, nat_child_size;

  scrollbar_spacing = _gtk_scrolled_window_get_scrollbar_spacing (scrolled_window);

  extra_width = 0;
  extra_height = 0;
  minimum_req.width = 0;
  minimum_req.height = 0;
  natural_req.width = 0;
  natural_req.height = 0;

  gtk_widget_get_preferred_size (priv->hscrollbar,
                                 &hscrollbar_requisition, NULL);
  gtk_widget_get_preferred_size (priv->vscrollbar,
                                 &vscrollbar_requisition, NULL);

  child = gtk_bin_get_child (bin);
  if (child && gtk_widget_get_visible (child))
    {
      if (orientation == GTK_ORIENTATION_HORIZONTAL)
	{
	  gtk_widget_get_preferred_width (child,
                                          &min_child_size,
                                          &nat_child_size);

	  if (priv->hscrollbar_policy == GTK_POLICY_NEVER)
	    {
	      minimum_req.width += min_child_size;
	      natural_req.width += nat_child_size;
	    }
	  else
	    {
              gint min_content_width = priv->min_content_width;

	      if (min_content_width >= 0)
		{
		  minimum_req.width = MAX (minimum_req.width, min_content_width);
		  natural_req.width = MAX (natural_req.width, min_content_width);
		  extra_width = -1;
		}
	      else
		{
		  minimum_req.width += vscrollbar_requisition.width;
		  natural_req.width += vscrollbar_requisition.width;
		}
	    }
	}
      else /* GTK_ORIENTATION_VERTICAL */
	{
	  gtk_widget_get_preferred_height (child,
                                           &min_child_size,
                                           &nat_child_size);

	  if (priv->vscrollbar_policy == GTK_POLICY_NEVER)
	    {
	      minimum_req.height += min_child_size;
	      natural_req.height += nat_child_size;
	    }
	  else
	    {
	      gint min_content_height = priv->min_content_height;

	      if (min_content_height >= 0)
		{
		  minimum_req.height = MAX (minimum_req.height, min_content_height);
		  natural_req.height = MAX (natural_req.height, min_content_height);
		  extra_height = -1;
		}
	      else
		{
		  minimum_req.height += vscrollbar_requisition.height;
		  natural_req.height += vscrollbar_requisition.height;
		}
	    }
	}
    }

  if (priv->hscrollbar_policy == GTK_POLICY_AUTOMATIC ||
      priv->hscrollbar_policy == GTK_POLICY_ALWAYS)
    {
      minimum_req.width = MAX (minimum_req.width, hscrollbar_requisition.width);
      natural_req.width = MAX (natural_req.width, hscrollbar_requisition.width);
      if (!extra_height || priv->hscrollbar_policy == GTK_POLICY_ALWAYS)
	extra_height = scrollbar_spacing + hscrollbar_requisition.height;
    }

  if (priv->vscrollbar_policy == GTK_POLICY_AUTOMATIC ||
      priv->vscrollbar_policy == GTK_POLICY_ALWAYS)
    {
      minimum_req.height = MAX (minimum_req.height, vscrollbar_requisition.height);
      natural_req.height = MAX (natural_req.height, vscrollbar_requisition.height);
      if (!extra_width || priv->vscrollbar_policy == GTK_POLICY_ALWAYS)
	extra_width = scrollbar_spacing + vscrollbar_requisition.width;
    }

  minimum_req.width  += MAX (0, extra_width);
  minimum_req.height += MAX (0, extra_height);
  natural_req.width  += MAX (0, extra_width);
  natural_req.height += MAX (0, extra_height);

  if (priv->shadow_type != GTK_SHADOW_NONE)
    {
      GtkStyleContext *context;
      GtkStateFlags state;
      GtkBorder padding, border;

      context = gtk_widget_get_style_context (GTK_WIDGET (widget));
      state = gtk_widget_get_state_flags (GTK_WIDGET (widget));

      gtk_style_context_save (context);
      gtk_style_context_add_class (context, GTK_STYLE_CLASS_FRAME);

      gtk_style_context_get_padding (context, state, &padding);
      gtk_style_context_get_border (context, state, &border);

      minimum_req.width += padding.left + padding.right + border.left + border.right;
      minimum_req.height += padding.top + padding.bottom + border.top + border.bottom;
      natural_req.width += padding.left + padding.right + border.left + border.right;
      natural_req.height += padding.top + padding.bottom + border.top + border.bottom;

      gtk_style_context_restore (context);
    }

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      if (minimum_size)
	*minimum_size = minimum_req.width;
      if (natural_size)
	*natural_size = natural_req.width;
    }
  else
    {
      if (minimum_size)
	*minimum_size = minimum_req.height;
      if (natural_size)
	*natural_size = natural_req.height;
    }
}

static void     
gtk_scrolled_window_get_preferred_width (GtkWidget *widget,
                                         gint      *minimum_size,
                                         gint      *natural_size)
{
  gtk_scrolled_window_get_preferred_size (widget, GTK_ORIENTATION_HORIZONTAL, minimum_size, natural_size);
}

static void
gtk_scrolled_window_get_preferred_height (GtkWidget *widget,
                                          gint      *minimum_size,
                                          gint      *natural_size)
{  
  gtk_scrolled_window_get_preferred_size (widget, GTK_ORIENTATION_VERTICAL, minimum_size, natural_size);
}

static void
gtk_scrolled_window_get_preferred_height_for_width (GtkWidget *widget,
                                                    gint       width,
                                                    gint      *minimum_height,
                                                    gint      *natural_height)
{
  g_return_if_fail (GTK_IS_WIDGET (widget));

  GTK_WIDGET_GET_CLASS (widget)->get_preferred_height (widget, minimum_height, natural_height);
}

static void
gtk_scrolled_window_get_preferred_width_for_height (GtkWidget *widget,
                                                    gint       height,
                                                    gint      *minimum_width,
                                                    gint      *natural_width)
{
  g_return_if_fail (GTK_IS_WIDGET (widget));

  GTK_WIDGET_GET_CLASS (widget)->get_preferred_width (widget, minimum_width, natural_width);
}

static void
gtk_scrolled_window_realize (GtkWidget *widget)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkAllocation allocation, relative_allocation;
  GdkWindowAttr attributes;
  GtkWidget *child_widget;
  gint attributes_mask;

  gtk_widget_set_realized (widget, TRUE);
  gtk_widget_get_allocation (widget, &allocation);
  gtk_scrolled_window_relative_allocation (widget, &relative_allocation);

  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = allocation.x + relative_allocation.x;
  attributes.y = allocation.y + relative_allocation.y;
  attributes.width = allocation.width;
  attributes.height = allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK |
    GDK_BUTTON_MOTION_MASK | GDK_TOUCH_MASK | GDK_EXPOSURE_MASK;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

  scrolled_window->priv->overshoot_window =
    gdk_window_new (gtk_widget_get_parent_window (widget),
                    &attributes, attributes_mask);
  gdk_window_set_user_data (scrolled_window->priv->overshoot_window, widget);

  child_widget = gtk_bin_get_child (GTK_BIN (widget));

  if (child_widget)
    gtk_widget_set_parent_window (child_widget,
                                  scrolled_window->priv->overshoot_window);

  GTK_WIDGET_CLASS (gtk_scrolled_window_parent_class)->realize (widget);
}

static void
gtk_scrolled_window_unrealize (GtkWidget *widget)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);

  gdk_window_set_user_data (scrolled_window->priv->overshoot_window, NULL);
  gdk_window_destroy (scrolled_window->priv->overshoot_window);
  scrolled_window->priv->overshoot_window = NULL;

  gtk_widget_set_realized (widget, FALSE);

  GTK_WIDGET_CLASS (gtk_scrolled_window_parent_class)->unrealize (widget);
}

static void
gtk_scrolled_window_map (GtkWidget *widget)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);

  gdk_window_show (scrolled_window->priv->overshoot_window);

  GTK_WIDGET_CLASS (gtk_scrolled_window_parent_class)->map (widget);
}

static void
gtk_scrolled_window_unmap (GtkWidget *widget)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);

  gdk_window_hide (scrolled_window->priv->overshoot_window);

  GTK_WIDGET_CLASS (gtk_scrolled_window_parent_class)->unmap (widget);
}

static void
gtk_scrolled_window_grab_notify (GtkWidget *widget,
                                 gboolean   was_grabbed)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = scrolled_window->priv;

  if (priv->drag_device &&
      gtk_widget_device_is_shadowed (widget,
                                     priv->drag_device))
    {
      gdk_device_ungrab (priv->drag_device,
                         gtk_get_current_event_time ());
      priv->drag_device = NULL;
      priv->in_drag = FALSE;

      if (priv->release_timeout_id)
        {
          g_source_remove (priv->release_timeout_id);
          priv->release_timeout_id = 0;
        }

      if (_gtk_scrolled_window_get_overshoot (scrolled_window, NULL, NULL))
        gtk_scrolled_window_start_deceleration (scrolled_window);
      else
        gtk_scrolled_window_cancel_deceleration (scrolled_window);

      priv->last_button_event_valid = FALSE;
    }
}

/**
 * gtk_scrolled_window_get_min_content_width:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Gets the minimum content width of @scrolled_window, or -1 if not set.
 *
 * Returns: the minimum content width
 *
 * Since: 3.0
 */
gint
gtk_scrolled_window_get_min_content_width (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), 0);

  return scrolled_window->priv->min_content_width;
}

/**
 * gtk_scrolled_window_set_min_content_width:
 * @scrolled_window: a #GtkScrolledWindow
 * @width: the minimal content width
 *
 * Sets the minimum width that @scrolled_window should keep visible.
 * Note that this can and (usually will) be smaller than the minimum
 * size of the content.
 *
 * Since: 3.0
 */
void
gtk_scrolled_window_set_min_content_width (GtkScrolledWindow *scrolled_window,
                                           gint               width)
{
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  priv = scrolled_window->priv;

  if (priv->min_content_width != width)
    {
      priv->min_content_width = width;

      gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));

      g_object_notify (G_OBJECT (scrolled_window), "min-content-width");
    }
}

/**
 * gtk_scrolled_window_get_min_content_height:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Gets the minimal content height of @scrolled_window, or -1 if not set.
 *
 * Returns: the minimal content height
 *
 * Since: 3.0
 */
gint
gtk_scrolled_window_get_min_content_height (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), 0);

  return scrolled_window->priv->min_content_height;
}

/**
 * gtk_scrolled_window_set_min_content_height:
 * @scrolled_window: a #GtkScrolledWindow
 * @height: the minimal content height
 *
 * Sets the minimum height that @scrolled_window should keep visible.
 * Note that this can and (usually will) be smaller than the minimum
 * size of the content.
 *
 * Since: 3.0
 */
void
gtk_scrolled_window_set_min_content_height (GtkScrolledWindow *scrolled_window,
                                            gint               height)
{
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  priv = scrolled_window->priv;

  if (priv->min_content_height != height)
    {
      priv->min_content_height = height;

      gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));

      g_object_notify (G_OBJECT (scrolled_window), "min-content-height");
    }
}
