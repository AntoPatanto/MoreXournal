/*
 * MoreXournal
 *
 * Switching rules between compact (mouse/pen) and touch chrome layouts
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gdk/gdk.h>  // for GdkInputSource

#include "control/settings/SettingsEnums.h"  // for TouchMode

namespace xoj::chrome {

/// Coarse input class used to decide whether the chrome should switch density
enum class PointerKind {
    Mouse,  ///< Mouse, touchpad, trackpoint, cursor (fine pointer -> switches back to compact in Auto)
    Pen,    ///< Stylus pen or eraser (never changes the current layout in Auto)
    Touch,  ///< Touchscreen finger (switches to touch layout in Auto)
    Other,  ///< Keyboard or unknown device (never changes the current layout)
};

constexpr PointerKind classifyGdkSource(GdkInputSource source) {
    switch (source) {
        case GDK_SOURCE_TOUCHSCREEN:
            return PointerKind::Touch;
        case GDK_SOURCE_PEN:
        case GDK_SOURCE_ERASER:
            return PointerKind::Pen;
        case GDK_SOURCE_MOUSE:
        case GDK_SOURCE_TOUCHPAD:
        case GDK_SOURCE_TRACKPOINT:
        case GDK_SOURCE_CURSOR:
            return PointerKind::Mouse;
        default:
            return PointerKind::Other;
    }
}

/**
 * Resolves whether the window should use the touch chrome layout after a press event.
 * - TouchMode::ON always uses touch.
 * - TouchMode::OFF always uses compact.
 * - TouchMode::AUTO switches to touch on a touchscreen press, back to compact on a mouse press,
 *   and leaves the current layout untouched for pen/eraser or other inputs.
 */
constexpr bool resolveTouchLayout(TouchMode setting, PointerKind input, bool currentlyTouch) {
    switch (setting) {
        case TouchMode::ON:
            return true;
        case TouchMode::OFF:
            return false;
        case TouchMode::AUTO:
            if (input == PointerKind::Touch) {
                return true;
            }
            if (input == PointerKind::Mouse) {
                return false;
            }
            return currentlyTouch;
    }
    return false;
}

/// Whether a popup menu should use the taller 44 px touch rows
constexpr bool isTouchMenuLayout(TouchMode setting, bool openedFromTouch) {
    switch (setting) {
        case TouchMode::ON:
            return true;
        case TouchMode::OFF:
            return false;
        case TouchMode::AUTO:
            return openedFromTouch;
    }
    return false;
}

/// Standard icon size in the chrome: 18 px compact, 20 px touch
constexpr int chromeIconSize(bool touchLayout) { return touchLayout ? 20 : 18; }

/// Colour swatch dot diameter in the pill: 20 px compact, 24 px touch
constexpr int chromeSwatchDotSize(bool touchLayout) { return touchLayout ? 24 : 20; }

}  // namespace xoj::chrome
