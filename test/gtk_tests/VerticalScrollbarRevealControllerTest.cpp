/*
 * MoreXournal
 *
 * GTK integration tests for VerticalScrollbarRevealController.
 *
 * @license GNU GPLv2 or later
 */

#include <gtk/gtk.h>

#include "dialog/GtkTest.h"
#include "gui/scroll/VerticalScrollbarRevealController.h"

namespace {

void pumpMainContext(int64_t waitMs) {
    const int64_t deadline = g_get_monotonic_time() + waitMs * 1000;
    while (g_get_monotonic_time() < deadline) {
        while (gtk_events_pending()) {
            gtk_main_iteration();
        }
        g_usleep(10000);
    }
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

}  // namespace

class VerticalScrollbarRevealControllerTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* win = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(win), 400, 400);

        GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
        GtkWidget* area = gtk_drawing_area_new();
        gtk_widget_set_size_request(area, 400, 4000);
        gtk_container_add(GTK_CONTAINER(sw), area);
        gtk_container_add(GTK_CONTAINER(win), sw);

        bool zoomActive = false;
        VerticalScrollbarRevealController controller(GTK_SCROLLED_WINDOW(sw), [&zoomActive]() { return zoomActive; });

        gtk_widget_show_all(sw);
        controller.configure(false, false);

        GtkWidget* vscroll = gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(sw));
        GtkWidget* viewport = gtk_bin_get_child(GTK_BIN(sw));
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));

        gtk_widget_realize(win);
        gtk_widget_realize(sw);
        gtk_widget_realize(viewport);
        gtk_widget_realize(area);

        GtkAllocation alloc{0, 0, 400, 400};
        gtk_widget_size_allocate(win, &alloc);

        // Starts disabled and non-interactive in overlay (scroll-only) mode.
        EXPECT_FALSE(gtk_widget_get_visible(vscroll));
        EXPECT_FALSE(controller.isScrollbarVisible());

        GtkPolicyType hpol = GTK_POLICY_AUTOMATIC;
        GtkPolicyType vpol = GTK_POLICY_AUTOMATIC;
        gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(sw), &hpol, &vpol);
        EXPECT_EQ(vpol, GTK_POLICY_EXTERNAL);

#if GTK_MAJOR_VERSION == 3
        GdkWindow* indicatorWin = gtk_widget_get_parent_window(vscroll);
        GdkWindow* swWin = gtk_widget_get_window(sw);
        if (indicatorWin != nullptr && indicatorWin != swWin) {
            EXPECT_FALSE(gdk_window_is_visible(indicatorWin));
        }
#endif

        // Wait past initial range-change cooldown (200 ms).
        pumpMainContext(230);

        // Pointer motion near the right edge must not reveal the scrollbar or its overlay indicator.
        GdkDisplay* display = gtk_widget_get_display(win);
        GdkSeat* seat = gdk_display_get_default_seat(display);
        if (GdkDevice* device = gdk_seat_get_pointer(seat);
            device != nullptr && gtk_widget_get_window(area) != nullptr) {
            GdkEvent* motion = gdk_event_new(GDK_MOTION_NOTIFY);
            motion->motion.window = GDK_WINDOW(g_object_ref(gtk_widget_get_window(area)));
            motion->motion.send_event = FALSE;
            motion->motion.time = 1000;
            motion->motion.x = 395.0;
            motion->motion.y = 200.0;
            motion->motion.x_root = 395.0;
            motion->motion.y_root = 200.0;
            motion->motion.device = device;
            gdk_event_set_source_device(motion, device);
            gtk_main_do_event(motion);
            gdk_event_free(motion);
        }
        EXPECT_FALSE(gtk_widget_get_visible(vscroll));
#if GTK_MAJOR_VERSION == 3
        if (indicatorWin != nullptr && indicatorWin != swWin) {
            EXPECT_FALSE(gdk_window_is_visible(indicatorWin));
        }
#endif

        // Small micro-movement (< 120 px) must keep the scrollbar disabled and its indicator hidden.
        gtk_adjustment_set_value(vadj, 50.0);
        EXPECT_FALSE(gtk_widget_get_visible(vscroll));
        EXPECT_FALSE(controller.isScrollbarVisible());
#if GTK_MAJOR_VERSION == 3
        if (indicatorWin != nullptr && indicatorWin != swWin) {
            EXPECT_FALSE(gdk_window_is_visible(indicatorWin));
        }
#endif

        // Large jump while zoom is active must not reveal the scrollbar.
        zoomActive = true;
        gtk_adjustment_set_value(vadj, 500.0);
        EXPECT_FALSE(gtk_widget_get_visible(vscroll));
        zoomActive = false;

        // Genuine sustained scrolling (>= 120 px) must reveal and enable the vertical scrollbar.
        gtk_adjustment_set_value(vadj, 560.0);
        gtk_adjustment_set_value(vadj, 620.0);
        gtk_adjustment_set_value(vadj, 680.0);
        EXPECT_TRUE(gtk_widget_get_visible(vscroll));
        EXPECT_TRUE(controller.isScrollbarVisible());
        gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(sw), &hpol, &vpol);
        EXPECT_EQ(vpol, GTK_POLICY_AUTOMATIC);

        // After 1 second of inactivity, it must disable itself again.
        pumpMainContext(1150);
        EXPECT_FALSE(gtk_widget_get_visible(vscroll));
        EXPECT_FALSE(controller.isScrollbarVisible());
        gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(sw), &hpol, &vpol);
        EXPECT_EQ(vpol, GTK_POLICY_EXTERNAL);

        // Disabling fadeout in Preferences makes it always visible (non-overlay).
        controller.configure(false, true);
        EXPECT_TRUE(gtk_widget_get_visible(vscroll));
        EXPECT_FALSE(gtk_scrolled_window_get_overlay_scrolling(GTK_SCROLLED_WINDOW(sw)));

        // Hiding vertical scrollbar in Preferences keeps it hidden even after large scrolls.
        controller.configure(true, false);
        EXPECT_FALSE(gtk_widget_get_visible(vscroll));
        gtk_adjustment_set_value(vadj, 1200.0);
        EXPECT_FALSE(gtk_widget_get_visible(vscroll));

        gtk_widget_destroy(win);
    }
};

TEST_F(VerticalScrollbarRevealControllerTest, revealsOnlyOnSustainedScrollAndAutoHidesAfterOneSecond) {}
