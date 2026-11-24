/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <initializer_list>

#include <gtk/gtk.h>

#include "dialog/GtkTest.h"
#include "util/GtkUtil.h"

#if GTK_MAJOR_VERSION == 3

namespace {

void dispatchTouchEvent(GdkEventType type, GdkWindow* window, GdkDevice* device, guintptr sequenceId, double x,
                        double y, guint32 time) {
    GdkEvent* event = gdk_event_new(type);
    event->touch.window = GDK_WINDOW(g_object_ref(window));
    event->touch.send_event = FALSE;
    event->touch.time = time;
    event->touch.x = x;
    event->touch.y = y;
    event->touch.x_root = x;
    event->touch.y_root = y;
    event->touch.axes = nullptr;
    event->touch.state = 0;
    event->touch.sequence = reinterpret_cast<GdkEventSequence*>(sequenceId);
    event->touch.emulating_pointer = FALSE;
    event->touch.device = device;
    gdk_event_set_source_device(event, device);

    gtk_main_do_event(event);
    gdk_event_free(event);
}

auto findWidgetEventWindow(GtkWidget* widget, std::initializer_list<GdkWindow*> roots) -> GdkWindow* {
    for (GdkWindow* root: roots) {
        if (!root) {
            continue;
        }
        for (GList* l = gdk_window_peek_children(root); l != nullptr; l = l->next) {
            auto* child = GDK_WINDOW(l->data);
            gpointer userData = nullptr;
            gdk_window_get_user_data(child, &userData);
            if (userData == widget) {
                return child;
            }
            for (GList* l2 = gdk_window_peek_children(child); l2 != nullptr; l2 = l2->next) {
                auto* grandchild = GDK_WINDOW(l2->data);
                gpointer grandchildData = nullptr;
                gdk_window_get_user_data(grandchild, &grandchildData);
                if (grandchildData == widget) {
                    return grandchild;
                }
            }
        }
    }
    return gtk_widget_get_window(widget);
}

}  // namespace

class ScrolledWindowLongPressCrashGuardTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        // Installing more than once must be a safe no-op.
        xoj::util::gtk::installScrolledWindowLongPressCrashGuard();
        xoj::util::gtk::installScrolledWindowLongPressCrashGuard();

        GtkWidget* win = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(win), 300, 300);

        GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(sw), TRUE);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_ALWAYS, GTK_POLICY_ALWAYS);

        GtkWidget* area = gtk_drawing_area_new();
        gtk_widget_set_size_request(area, 1000, 1000);
        gtk_container_add(GTK_CONTAINER(sw), area);
        gtk_container_add(GTK_CONTAINER(win), sw);

        GtkWidget* vscroll = gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(sw));
        GtkWidget* viewport = gtk_bin_get_child(GTK_BIN(sw));

        gtk_widget_show_all(sw);
        gtk_widget_realize(win);
        gtk_widget_realize(sw);
        gtk_widget_realize(viewport);
        gtk_widget_realize(area);
        gtk_widget_realize(vscroll);

        GtkAllocation alloc{0, 0, 300, 300};
        gtk_widget_size_allocate(win, &alloc);

        GdkWindow* areaWin = gtk_widget_get_window(area);
        GdkWindow* swWin = gtk_widget_get_window(sw);
        GdkWindow* vscrollWin = gtk_widget_get_window(vscroll);
        GdkWindow* targetScrollWin = findWidgetEventWindow(vscroll, {vscrollWin, swWin});

        ASSERT_NE(areaWin, nullptr);
        ASSERT_NE(targetScrollWin, nullptr);

        GdkDisplay* display = gtk_widget_get_display(win);
        GdkSeat* seat = gdk_display_get_default_seat(display);
        GdkDevice* device = gdk_seat_get_pointer(seat);
        ASSERT_NE(device, nullptr);

        GtkAllocation vsAlloc{};
        gtk_widget_get_allocation(vscroll, &vsAlloc);
        const double scrollY = vsAlloc.height / 2.0;

        // Reproduces GNOME/gtk#7517: touch 1 starts long-press on the scrolled window,
        // then touch 2 hits the scrollbar's child GdkWindow and triggers grab-notify cancellation.
        dispatchTouchEvent(GDK_TOUCH_BEGIN, areaWin, device, 1, 50.0, 50.0, 1000);
        dispatchTouchEvent(GDK_TOUCH_BEGIN, targetScrollWin, device, 2, 5.0, scrollY, 1010);
        dispatchTouchEvent(GDK_TOUCH_END, targetScrollWin, device, 2, 5.0, scrollY, 1020);
        dispatchTouchEvent(GDK_TOUCH_END, areaWin, device, 1, 50.0, 50.0, 1030);

        // Verify the guard unblocks GTK's handler on the next valid drag and scrolling still works.
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));
        const double beforeY = gtk_adjustment_get_value(vadj);
        dispatchTouchEvent(GDK_TOUCH_BEGIN, areaWin, device, 3, 50.0, 200.0, 1100);
        dispatchTouchEvent(GDK_TOUCH_UPDATE, areaWin, device, 3, 50.0, 100.0, 1120);
        dispatchTouchEvent(GDK_TOUCH_END, areaWin, device, 3, 50.0, 100.0, 1140);
        const double afterY = gtk_adjustment_get_value(vadj);
        EXPECT_GT(afterY, beforeY);

        // Trigger the crash sequence a second time to verify re-blocking works.
        dispatchTouchEvent(GDK_TOUCH_BEGIN, areaWin, device, 4, 50.0, 50.0, 1200);
        dispatchTouchEvent(GDK_TOUCH_BEGIN, targetScrollWin, device, 5, 5.0, scrollY, 1210);
        dispatchTouchEvent(GDK_TOUCH_END, targetScrollWin, device, 5, 5.0, scrollY, 1220);
        dispatchTouchEvent(GDK_TOUCH_END, areaWin, device, 4, 50.0, 50.0, 1230);

        gtk_widget_destroy(win);
    }
};

TEST_F(ScrolledWindowLongPressCrashGuardTest, survivesMultiTouchScrollbarGrabCancelAndRecoversScrolling) {}

#endif
