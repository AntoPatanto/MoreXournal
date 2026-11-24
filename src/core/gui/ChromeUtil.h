/*
 * MoreXournal
 *
 * Shared GTK helpers for chrome controls: input classification, scalable icons,
 * contrast-aware colour swatches, line style previews, and popover tooltip suppression
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <algorithm>  // for min, max
#include <cmath>      // for M_PI, pow
#include <optional>   // for optional

#include <gdk/gdk.h>  // for GdkEvent, GdkDevice, GdkRGBA
#include <gtk/gtk.h>  // for GtkWidget, GtkImage, GtkStyleContext

#include "util/Color.h"  // for Color

#include "TouchModeLogic.h"  // for PointerKind, classifyGdkSource, isTouchMenuLayout

namespace xoj::chrome {

namespace detail {
constexpr const char* SCALABLE_ICON_KEY = "mx-scalable-icon";
constexpr const char* SWATCH_COLOR_KEY = "mx-swatch-color";
constexpr const char* SWATCH_HAS_SECONDARY_KEY = "mx-swatch-has-secondary";
constexpr const char* SWATCH_SECONDARY_KEY = "mx-swatch-secondary";
constexpr const char* SWATCH_EMPTY_KEY = "mx-swatch-empty";
constexpr const char* LINE_STYLE_KIND_KEY = "mx-line-style-kind";
constexpr const char* HAD_TOOLTIP_KEY = "mx-had-tooltip";
constexpr const char* SUBMENU_HOOKED_KEY = "mx-submenu-hooked";
constexpr const char* MENU_TOUCH_ACTIVE_KEY = "mx-menu-touch-active";

inline double linearizeChannel(double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

inline double relativeLuminance(double r, double g, double b) {
    return 0.2126 * linearizeChannel(r) + 0.7152 * linearizeChannel(g) + 0.0722 * linearizeChannel(b);
}
}  // namespace detail

/// WCAG 2.x contrast ratio between two opaque RGB colours (1.0 .. 21.0)
inline double contrastRatio(Color a, Color b) {
    const double l1 = detail::relativeLuminance(a.red / 255.0, a.green / 255.0, a.blue / 255.0);
    const double l2 = detail::relativeLuminance(b.red / 255.0, b.green / 255.0, b.blue / 255.0);
    return (std::max(l1, l2) + 0.05) / (std::min(l1, l2) + 0.05);
}

inline double contrastRatioRgba(Color swatch, const GdkRGBA& surface) {
    const double l1 = detail::relativeLuminance(swatch.red / 255.0, swatch.green / 255.0, swatch.blue / 255.0);
    const double l2 = detail::relativeLuminance(surface.red, surface.green, surface.blue);
    return (std::max(l1, l2) + 0.05) / (std::min(l1, l2) + 0.05);
}

/// True when a colour swatch has < 3:1 contrast against the surface behind it and needs a 1 px ring
inline bool needsContrastRing(Color swatch, Color surface) { return contrastRatio(swatch, surface) < 3.0; }

/// Classifies a GDK event using its source device (the master device always reports MOUSE)
inline PointerKind classifyEvent(const GdkEvent* ev) {
    if (!ev) {
        return PointerKind::Other;
    }
    const GdkEventType type = ev->type;
    if (type == GDK_TOUCH_BEGIN || type == GDK_TOUCH_UPDATE || type == GDK_TOUCH_END || type == GDK_TOUCH_CANCEL ||
        gdk_event_get_pointer_emulated(const_cast<GdkEvent*>(ev))) {
        return PointerKind::Touch;
    }
    GdkDevice* src = gdk_event_get_source_device(ev);
    if (!src) {
        return PointerKind::Other;
    }
    return classifyGdkSource(gdk_device_get_source(src));
}

inline bool isTouchEvent(const GdkEvent* ev) { return classifyEvent(ev) == PointerKind::Touch; }

/// Marks a GtkImage so updateScalableIcons() resizes it between 18 px (compact) and 20 px (touch)
inline GtkWidget* tagScalableIcon(GtkWidget* img) {
    g_object_set_data(G_OBJECT(img), detail::SCALABLE_ICON_KEY, GINT_TO_POINTER(1));
    return img;
}

inline GtkWidget* newChromeIcon(const char* iconName, int pixelSize, bool scalable = true) {
    GtkWidget* img = gtk_image_new_from_icon_name(iconName, GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_image_set_pixel_size(GTK_IMAGE(img), pixelSize);
    if (scalable) {
        tagScalableIcon(img);
    }
    return img;
}

/// Updates pixel size of all tagged chrome icons inside `root` (and popovers of menu buttons)
inline void updateScalableIcons(GtkWidget* root, int pixelSize) {
    if (!root) {
        return;
    }
    if (GTK_IS_IMAGE(root) && g_object_get_data(G_OBJECT(root), detail::SCALABLE_ICON_KEY) != nullptr) {
        gtk_image_set_pixel_size(GTK_IMAGE(root), pixelSize);
        return;
    }
    if (GTK_IS_BUTTON(root)) {
        if (GtkWidget* img = gtk_button_get_image(GTK_BUTTON(root))) {
            updateScalableIcons(img, pixelSize);
        }
    }
    if (GTK_IS_MENU_BUTTON(root)) {
        if (GtkPopover* pop = gtk_menu_button_get_popover(GTK_MENU_BUTTON(root))) {
            updateScalableIcons(GTK_WIDGET(pop), pixelSize);
        }
    }
    if (GTK_IS_CONTAINER(root)) {
        gtk_container_forall(
                GTK_CONTAINER(root),
                +[](GtkWidget* child, gpointer data) { updateScalableIcons(child, GPOINTER_TO_INT(data)); },
                GINT_TO_POINTER(pixelSize));
    }
}

/// Suppresses `button`'s tooltip while its own `popup` (GtkPopover or GtkMenu) is visible
inline void hideTooltipWhilePopupOpen(GtkWidget* button, GtkWidget* popup) {
    if (!button || !popup) {
        return;
    }
    g_signal_connect_object(
            popup, "show",
            G_CALLBACK(+[](GtkWidget*, gpointer btn) {
                GtkWidget* w = GTK_WIDGET(btn);
                const gboolean had = gtk_widget_get_has_tooltip(w);
                g_object_set_data(G_OBJECT(w), detail::HAD_TOOLTIP_KEY, GINT_TO_POINTER(had ? 1 : 0));
                if (had) {
                    gtk_widget_set_has_tooltip(w, false);
                }
            }),
            button, GConnectFlags(0));
    g_signal_connect_object(
            popup, "hide",
            G_CALLBACK(+[](GtkWidget*, gpointer btn) {
                GtkWidget* w = GTK_WIDGET(btn);
                if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), detail::HAD_TOOLTIP_KEY)) != 0) {
                    gtk_widget_set_has_tooltip(w, true);
                }
            }),
            button, GConnectFlags(0));
}

/// Recursively applies `.mx-menu` and (when `touch`) `.mx-touch-menu` to a GtkMenu and all its submenus,
/// hooking `show` on submenus so dynamically populated submenus (recent files, plugins) stay styled too.
inline void applyMenuClassesRecursive(GtkWidget* menu, bool touch) {
    if (!menu || !GTK_IS_MENU(menu)) {
        return;
    }
    GtkStyleContext* ctx = gtk_widget_get_style_context(menu);
    gtk_style_context_add_class(ctx, "mx-menu");
    if (touch) {
        gtk_style_context_add_class(ctx, "mx-touch-menu");
    } else {
        gtk_style_context_remove_class(ctx, "mx-touch-menu");
    }
    g_object_set_data(G_OBJECT(menu), detail::MENU_TOUCH_ACTIVE_KEY, GINT_TO_POINTER(touch ? 1 : 0));

    GList* children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList* l = children; l != nullptr; l = l->next) {
        GtkWidget* item = GTK_WIDGET(l->data);
        if (!GTK_IS_MENU_ITEM(item)) {
            continue;
        }
        GtkWidget* sub = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
        if (!sub || !GTK_IS_MENU(sub)) {
            continue;
        }
        if (g_object_get_data(G_OBJECT(sub), detail::SUBMENU_HOOKED_KEY) == nullptr) {
            g_object_set_data(G_OBJECT(sub), detail::SUBMENU_HOOKED_KEY, GINT_TO_POINTER(1));
            g_signal_connect(sub, "show",
                             G_CALLBACK(+[](GtkWidget* subMenu, gpointer) {
                                 bool subTouch = GPOINTER_TO_INT(g_object_get_data(
                                                         G_OBJECT(subMenu), detail::MENU_TOUCH_ACTIVE_KEY)) != 0;
                                 if (GtkWidget* attach = gtk_menu_get_attach_widget(GTK_MENU(subMenu))) {
                                     if (GtkWidget* parentMenu = gtk_widget_get_parent(attach);
                                         parentMenu && GTK_IS_MENU(parentMenu)) {
                                         subTouch = GPOINTER_TO_INT(g_object_get_data(
                                                            G_OBJECT(parentMenu), detail::MENU_TOUCH_ACTIVE_KEY)) != 0;
                                     }
                                 }
                                 applyMenuClassesRecursive(subMenu, subTouch);
                             }),
                             nullptr);
        }
        applyMenuClassesRecursive(sub, touch);
    }
    g_list_free(children);
}

/// Applies `.mx-menu` and `.mx-touch-menu` to a GtkMenu (and every submenu) when opened from touch or TouchMode::ON
inline void updateMenuTouchClass(GtkWidget* menu, TouchMode mode, bool openedFromTouch) {
    applyMenuClassesRecursive(menu, isTouchMenuLayout(mode, openedFromTouch));
}

inline gboolean drawContrastSwatch(GtkWidget* w, cairo_t* cr, gpointer) {
    const double aw = gtk_widget_get_allocated_width(w);
    const double ah = gtk_widget_get_allocated_height(w);
    const double r = std::min(aw, ah) / 2.0 - 0.5;
    const double cx = aw / 2.0;
    const double cy = ah / 2.0;
    const bool empty = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), detail::SWATCH_EMPTY_KEY)) != 0;

    GtkStyleContext* ctx = gtk_widget_get_style_context(w);
    GdkRGBA surface{1.0, 1.0, 1.0, 1.0};
    gtk_style_context_lookup_color(ctx, "mx_surface", &surface);
    GdkRGBA textCol{0.11, 0.12, 0.15, 1.0};
    gtk_style_context_lookup_color(ctx, "mx_text", &textCol);

    cairo_save(cr);
    cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
    cairo_clip(cr);

    bool drawRing = false;
    if (empty) {
        constexpr double cell = 4.0;
        for (int gy = 0; gy * cell < ah; gy++) {
            for (int gx = 0; gx * cell < aw; gx++) {
                const double v = ((gx + gy) % 2 == 0) ? 0.82 : 1.0;
                cairo_set_source_rgb(cr, v, v, v);
                cairo_rectangle(cr, gx * cell, gy * cell, cell, cell);
                cairo_fill(cr);
            }
        }
        drawRing = true;
    } else {
        const Color c(static_cast<uint32_t>(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), detail::SWATCH_COLOR_KEY))));
        Util::cairo_set_source_rgbi(cr, c, 1.0);
        cairo_paint(cr);
        drawRing = contrastRatioRgba(c, surface) < 3.0;

        if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), detail::SWATCH_HAS_SECONDARY_KEY)) != 0) {
            const Color sec(static_cast<uint32_t>(
                    GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), detail::SWATCH_SECONDARY_KEY))));
            Util::cairo_set_source_rgbi(cr, sec, 1.0);
            cairo_move_to(cr, aw, ah);
            cairo_arc(cr, aw - 1.0, ah - 1.0, r, M_PI, 1.5 * M_PI);
            cairo_close_path(cr);
            cairo_fill(cr);
        }
    }
    cairo_restore(cr);

    if (drawRing) {
        cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
        cairo_set_source_rgba(cr, textCol.red, textCol.green, textCol.blue, 0.5);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    }
    return true;
}

inline void setContrastSwatch(GtkWidget* swatch, Color color, std::optional<Color> secondary = std::nullopt,
                              bool empty = false) {
    g_object_set_data(G_OBJECT(swatch), detail::SWATCH_COLOR_KEY, GUINT_TO_POINTER(static_cast<uint32_t>(color)));
    g_object_set_data(G_OBJECT(swatch), detail::SWATCH_HAS_SECONDARY_KEY, GINT_TO_POINTER(secondary.has_value() ? 1 : 0));
    if (secondary.has_value()) {
        g_object_set_data(G_OBJECT(swatch), detail::SWATCH_SECONDARY_KEY,
                          GUINT_TO_POINTER(static_cast<uint32_t>(*secondary)));
    }
    g_object_set_data(G_OBJECT(swatch), detail::SWATCH_EMPTY_KEY, GINT_TO_POINTER(empty ? 1 : 0));
    gtk_widget_queue_draw(swatch);
}

/// Circular colour swatch drawing area that draws a 1 px alpha(@mx_text, .5) ring when contrast with @mx_surface < 3:1
inline GtkWidget* newContrastSwatch(Color color, int size, std::optional<Color> secondary = std::nullopt,
                                    bool empty = false) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, size, size);
    gtk_widget_set_halign(area, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(area, GTK_ALIGN_CENTER);
    setContrastSwatch(area, color, secondary, empty);
    g_signal_connect(area, "draw", G_CALLBACK(drawContrastSwatch), nullptr);
    return area;
}

inline gboolean drawLineStyleSample(GtkWidget* w, cairo_t* cr, gpointer) {
    const int k = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), detail::LINE_STYLE_KIND_KEY));
    GtkStyleContext* ctx = gtk_widget_get_style_context(w);
    GdkRGBA color;
    gtk_style_context_get_color(ctx, gtk_style_context_get_state(ctx), &color);
    gdk_cairo_set_source_rgba(cr, &color);
    const double h = gtk_widget_get_allocated_height(w);
    const double width = gtk_widget_get_allocated_width(w);
    cairo_set_line_width(cr, 2.2);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    if (k == 1) {
        const double dashes[] = {5.0, 4.0};
        cairo_set_dash(cr, dashes, 2, 0);
    } else if (k == 2) {
        const double dashes[] = {5.0, 3.5, 0.1, 3.5};
        cairo_set_dash(cr, dashes, 4, 0);
    } else if (k == 3) {
        const double dashes[] = {0.1, 4.5};
        cairo_set_dash(cr, dashes, 2, 0);
    }
    cairo_move_to(cr, 3.0, h / 2.0);
    cairo_line_to(cr, width - 3.0, h / 2.0);
    cairo_stroke(cr);
    return true;
}

/// Shared horizontal line style preview (0=plain, 1=dash, 2=dashdot, 3=dot) used by ToolPill and ShapeToolbox
inline GtkWidget* newLineStyleSample(int kind) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 26, 12);
    gtk_widget_set_valign(area, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(area), detail::LINE_STYLE_KIND_KEY, GINT_TO_POINTER(kind));
    g_signal_connect(area, "draw", G_CALLBACK(drawLineStyleSample), nullptr);
    return area;
}

}  // namespace xoj::chrome
