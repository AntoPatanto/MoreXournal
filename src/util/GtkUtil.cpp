#include "util/GtkUtil.h"

#include <algorithm>
#include <string>
#include <string_view>

#include <gtk/gtk.h>

#include "util/Assert.h"
#include "util/raii/GVariantSPtr.h"

namespace xoj::util::gtk {

static GAction* findAction(GtkActionable* w) {
    const char* name = gtk_actionable_get_action_name(w);
    if (!name) {
        g_warning("xoj::util::gtk::findAction: No action name set");
        return nullptr;
    }
    std::string_view namesv = name;
    size_t dotpos = namesv.find(".");
    if (dotpos == std::string_view::npos) {
        g_warning("xoj::util::gtk::findAction: Action name is not of the form \"namespace.name\": %s", name);
        return nullptr;
    }
    std::string groupname(namesv.substr(0, dotpos));
    GActionGroup* win = gtk_widget_get_action_group(GTK_WIDGET(w), groupname.c_str());
    if (!win) {
        // Most likely the widget just got removed from the toplevel
        g_debug("xoj::util::gtk::findAction: could not find action group \"%s\"", groupname.data());
        return nullptr;
    }
    if (!G_IS_ACTION_MAP(win)) {
        g_warning("xoj::util::gtk::findAction: GActionGroup is not a GActionMap");
        return nullptr;
    }
    auto shortname = namesv.substr(dotpos + 1);
    return g_action_map_lookup_action(G_ACTION_MAP(win), shortname.data());
}

void setToggleButtonUnreleasable(GtkToggleButton* btn) {
    // "hierarchy-change" is emitted when the widget is added to/removed from a toplevel's descendance
    // We use this to connect to the suitable GAction signals once the widget has been added to the toolbar
    g_signal_connect(btn, "hierarchy-changed", G_CALLBACK(+[](GtkWidget* btn, GtkWidget*, gpointer) {
                         if (!GTK_IS_WINDOW(gtk_widget_get_toplevel(btn))) {
                             return;  // The widget was removed from the hierarchy
                         }
                         GAction* action = findAction(GTK_ACTIONABLE(btn));
                         if (!action) {
                             return;
                         }

                         g_signal_connect_object(
                                 btn, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer a) {
                                     xoj::util::GVariantSPtr state(g_action_get_state(G_ACTION(a)), xoj::util::adopt);
                                     GVariant* target = gtk_actionable_get_action_target_value(GTK_ACTIONABLE(btn));
                                     if (bool active = g_variant_equal(state.get(), target);
                                         active && !gtk_toggle_button_get_active(btn)) {
                                         gtk_toggle_button_set_active(btn, true);
                                     }
                                 }),
                                 action, GConnectFlags(0));
                     }),
                     nullptr);
}

void setWidgetFollowActionEnabled(GtkWidget* w, GAction* a) {
    g_signal_connect_object(a, "notify::enabled", G_CALLBACK(+[](GObject* a, GParamSpec*, gpointer w) {
                                bool b = g_action_get_enabled(G_ACTION(a));
                                gtk_widget_set_sensitive(GTK_WIDGET(w), b);
                            }),
                            w, GConnectFlags(0));
    gtk_widget_set_sensitive(w, g_action_get_enabled(a));
}

std::optional<double> getWidgetDPI(GtkWidget* w) {
    GdkWindow* win = gtk_widget_get_window(w);
    GdkDisplay* disp = gtk_widget_get_display(w);
    if (win && disp) {
        GdkMonitor* mon = gdk_display_get_monitor_at_window(disp, win);
        if (mon) {
            int h = gdk_monitor_get_height_mm(mon);
            GdkRectangle r;
            gdk_monitor_get_geometry(mon, &r);
            // Some setups (e.g. RDP sessions or missing EDID data on Windows) report a null physical size, which
            // would produce an infinite or NaN DPI and break e.g. pasted image scaling
            if (h <= 0 || r.height <= 0) {
                g_debug("Automatic DPI conf: invalid monitor data - height %d mm - pixel height %d", h, r.height);
                return std::nullopt;
            }
            auto res = static_cast<double>(r.height) * 25.4 / static_cast<double>(h);
            g_debug("Automatic DPI conf: monitor height %d mm - DPI = %f", h, res);
            // I assume some monitors may not return the right value: clamp to avoid wild values that could cause very
            // bad performances. Not sure what good clamping values should be here.
            // DPI > 720 should use HiDPI scaling but smaller devices (tablet/phone) can have high DPI values
            res = std::clamp(res, 36., 720.);
            return res;
        }
    }
    return std::nullopt;
}


#if GTK_MAJOR_VERSION == 3
void setRadioButtonActionName(GtkRadioButton* btn, const char* actionNamespace, const char* actionName) {
    // "hierarchy-change" is emitted when the widget is added to/removed from a toplevel's descendance
    // We use this to connect to the suitable GAction signals once the widget has been added to the toolbar
    struct Data {
        std::string actionNamespace;
        std::string actionName;
    };
    g_signal_connect_data(
            btn, "hierarchy-changed", G_CALLBACK(+[](GtkWidget* btn, GtkWidget*, gpointer d) {
                if (!GTK_IS_WINDOW(gtk_widget_get_toplevel(btn))) {
                    return;  // The widget was removed from the hierarchy
                }
                Data* data = static_cast<Data*>(d);
                GActionGroup* win = gtk_widget_get_action_group(btn, data->actionNamespace.c_str());
                if (!win) {
                    g_warning("Could not find action group \"%s\"", data->actionNamespace.c_str());
                    return;
                }
                xoj_assert(G_IS_ACTION_MAP(win));
                auto* action = g_action_map_lookup_action(G_ACTION_MAP(win), data->actionName.c_str());
                if (!action) {
                    g_warning("Could not find action \"win.%s\"", data->actionName.c_str());
                    return;
                }

                {
                    // btn owns the return GVariant of gtk_actionable_get_action_target_value()
                    GVariant* target = gtk_actionable_get_action_target_value(GTK_ACTIONABLE(btn));
                    // action does not own the return GVariant and it is not floating either!
                    xoj::util::GVariantSPtr state(g_action_get_state(G_ACTION(action)), xoj::util::adopt);
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), g_variant_equal(target, state.get()));
                }

                static auto toggledCallback = +[](GtkToggleButton* btn, gpointer action) {
                    xoj_assert(gtk_actionable_get_action_target_value(GTK_ACTIONABLE(btn)));
                    if (gtk_toggle_button_get_active(btn)) {
                        g_action_change_state(G_ACTION(action),
                                              gtk_actionable_get_action_target_value(GTK_ACTIONABLE(btn)));
                    }
                };
                g_signal_connect_object(btn, "toggled", G_CALLBACK(toggledCallback), action, GConnectFlags(0));

                g_signal_connect_object(
                        action, "notify::state", G_CALLBACK(+[](GObject* action, GParamSpec*, gpointer btn) {
                            // btn owns the return GVariant of gtk_actionable_get_action_target_value()
                            GVariant* target = gtk_actionable_get_action_target_value(GTK_ACTIONABLE(btn));
                            // action does not own the return GVariant and it is not floating either!
                            xoj::util::GVariantSPtr state(g_action_get_state(G_ACTION(action)), xoj::util::adopt);
                            xoj_assert(target);
                            xoj_assert(state);
                            if (g_variant_equal(target, state.get()) &&
                                !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) {
                                g_signal_handlers_block_by_func(btn, (gpointer)toggledCallback, action);
                                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), true);
                                g_signal_handlers_unblock_by_func(btn, (gpointer)toggledCallback, action);
                            }
                        }),
                        btn, GConnectFlags(0));

                setWidgetFollowActionEnabled(btn, action);
            }),
            new Data{actionNamespace, actionName}, +[](gpointer d, GClosure*) { delete static_cast<Data*>(d); },
            GConnectFlags(0));
}

void fixActionableInitialSensitivity(GtkActionable* w) {
    g_signal_connect(w, "hierarchy-changed", G_CALLBACK(+[](GtkWidget* w, GtkWidget*, gpointer) {
                         if (!GTK_IS_WINDOW(gtk_widget_get_toplevel(w))) {
                             return;  // The widget was removed from the hierarchy
                         }
                         if (GAction* action = findAction(GTK_ACTIONABLE(w)); action) {
                             gtk_widget_set_sensitive(w, g_action_get_enabled(action));
                         }
                     }),
                     nullptr);
}

void installScrolledWindowLongPressCrashGuard() {
    static gulong hookId = 0;
    if (hookId != 0) {
        return;
    }

    // Ensure GtkGestureLongPress class is initialized so "cancelled" can be looked up.
    auto* klass = g_type_class_ref(GTK_TYPE_GESTURE_LONG_PRESS);
    const guint signalId = g_signal_lookup("cancelled", GTK_TYPE_GESTURE_LONG_PRESS);
    if (signalId != 0) {
        hookId = g_signal_add_emission_hook(
                signalId, 0,
                +[](GSignalInvocationHint* ihint, guint nParamValues, const GValue* paramValues,
                    gpointer /*userData*/) -> gboolean {
                    if (nParamValues == 0) {
                        return TRUE;
                    }
                    auto* instance = g_value_get_object(&paramValues[0]);
                    if (!GTK_IS_GESTURE(instance)) {
                        return TRUE;
                    }
                    auto* gesture = GTK_GESTURE(instance);
                    auto* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
                    if (!GTK_IS_SCROLLED_WINDOW(widget)) {
                        return TRUE;
                    }

                    // Work around GTK 3 bug (GNOME/gtk#7517): scrolled_window_long_press_cancelled_cb()
                    // dereferences event->type without checking for nullptr after gtk_gesture_get_last_event().
                    // That happens when a touch starts on the scrolled window and a second touch hits a child
                    // GdkWindow (e.g. a scrollbar), updating last_sequence without inserting it into points
                    // before grab-notify cancels the first touch.
                    static const GQuark blockedQuark =
                            g_quark_from_static_string("xoj-gtk-scrolled-window-long-press-blocked");
                    const bool isBlocked = g_object_get_qdata(G_OBJECT(gesture), blockedQuark) != nullptr;
                    GdkEventSequence* seq = gtk_gesture_get_last_updated_sequence(gesture);
                    const bool hasNullEvent = gtk_gesture_get_last_event(gesture, seq) == nullptr;

                    if (hasNullEvent && !isBlocked) {
                        if (g_signal_handlers_block_matched(
                                    gesture, static_cast<GSignalMatchType>(G_SIGNAL_MATCH_ID | G_SIGNAL_MATCH_DATA),
                                    ihint->signal_id, 0, nullptr, nullptr, widget) > 0) {
                            g_object_set_qdata(G_OBJECT(gesture), blockedQuark, GINT_TO_POINTER(1));
                        }
                    } else if (!hasNullEvent && isBlocked) {
                        g_signal_handlers_unblock_matched(
                                gesture, static_cast<GSignalMatchType>(G_SIGNAL_MATCH_ID | G_SIGNAL_MATCH_DATA),
                                ihint->signal_id, 0, nullptr, nullptr, widget);
                        g_object_set_qdata(G_OBJECT(gesture), blockedQuark, nullptr);
                    }

                    return TRUE;
                },
                nullptr, nullptr);
    }
    g_type_class_unref(klass);
}

#endif
};  // namespace xoj::util::gtk
