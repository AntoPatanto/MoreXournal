/*
 * Xournal++
 *
 * PopupWindow base class
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <algorithm>
#include <memory>

#include <gtk/gtk.h>

#include "util/Assert.h"
#include "util/gtk4_helper.h"

namespace xoj::popup {

/**
 * @brief The class PopupWindowWrapper allows a safe non-blocking creation and display of a popup window.
 * It shows the popup (upon a call to show()) and tasks a callback function to actually delete the popup once it has
 * been closed by the user.
 */
template <class PopupType>
class PopupWindowWrapper {
public:
    PopupWindowWrapper() = delete;
    PopupWindowWrapper(const PopupWindowWrapper&) = delete;
    PopupWindowWrapper(PopupWindowWrapper&&) = delete;

    template <class... Args>
    PopupWindowWrapper(Args&&... args) {
        popup = new PopupType(std::forward<Args>(args)...);
    }
    ~PopupWindowWrapper() { delete popup; }

    void show(GtkWindow* parent) {
        gtk_window_set_transient_for(popup->getWindow(), parent);
        gtk_window_set_modal(popup->getWindow(), true);

#if GTK_MAJOR_VERSION == 3
        gtk_window_set_position(popup->getWindow(), GTK_WIN_POS_CENTER_ON_PARENT);
        gtk_widget_show(GTK_WIDGET(popup->getWindow()));
#ifdef __APPLE__
        // On the GTK 3 Quartz backend GTK_WIN_POS_CENTER_ON_PARENT can place dialogs at (0, 0);
        // explicitly centre over the mapped parent window and clamp inside its monitor workarea.
        if (parent != nullptr && gtk_widget_get_mapped(GTK_WIDGET(parent))) {
            gint px = 0;
            gint py = 0;
            gint pw = 0;
            gint ph = 0;
            gint dw = 0;
            gint dh = 0;
            gtk_window_get_position(parent, &px, &py);
            gtk_window_get_size(parent, &pw, &ph);
            gtk_window_get_size(popup->getWindow(), &dw, &dh);
            if (pw > 0 && ph > 0 && dw > 0 && dh > 0) {
                gint x = px + (pw - dw) / 2;
                gint y = py + (ph - dh) / 2;
                if (GdkWindow* parentGdk = gtk_widget_get_window(GTK_WIDGET(parent))) {
                    GdkDisplay* display = gdk_window_get_display(parentGdk);
                    GdkMonitor* monitor = display ? gdk_display_get_monitor_at_window(display, parentGdk) : nullptr;
                    if (monitor != nullptr) {
                        GdkRectangle wa{};
                        gdk_monitor_get_workarea(monitor, &wa);
                        if (wa.width > 0 && wa.height > 0) {
                            x = std::clamp(x, wa.x, std::max(wa.x, wa.x + wa.width - dw));
                            y = std::clamp(y, wa.y, std::max(wa.y, wa.y + wa.height - dh));
                        }
                    }
                }
                gtk_window_move(popup->getWindow(), x, y);
            }
        }
#endif
        g_signal_connect(popup->getWindow(), "delete-event",
                         G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer popup) -> gboolean {
                             delete reinterpret_cast<PopupType*>(popup);
                             return true;  // Block the default callback: we destroy the window via ~GtkWindowUPtr()
                         }),
                         popup);
#else
        gtk_window_present(popup->getWindow());
        g_signal_connect(popup->getWindow(), "close-request", G_CALLBACK(+[](GtkWindow*, gpointer popup) -> gboolean {
                             delete reinterpret_cast<PopupType*>(popup);
                             return true;  // Block the default callback: we destroy the window via ~GtkWindowUPtr()
                         }),
                         popup);
#endif

        /*
         * The actual popup must outlive this wrapper (so the main loop can go on).
         * As a consequence, once the popup is shown, this wrapper does not own the popup anymore.
         * The popup will get destroy by the signal connected above.
         */
        popup = nullptr;
    }
    void showNative(GtkWindow* parent, bool modal = true) {
        gtk_native_dialog_set_transient_for(popup->getNativeDialog(), parent);
        gtk_native_dialog_set_modal(popup->getNativeDialog(), modal);

        gtk_native_dialog_show(popup->getNativeDialog());
        /*
         * The actual popup must outlive this wrapper (so the main loop can go on).
         * As a consequence, once the popup is shown, this wrapper does not own the popup anymore.
         * The popup will get destroy by the signal connected above.
         */
        popup = nullptr;
    }

    PopupType* getPopup() const {
        xoj_assert_message(popup, "Do not call getPopup() after show()!");
        return popup;
    }

private:
    PopupType* popup = nullptr;
};
};  // namespace xoj::popup
