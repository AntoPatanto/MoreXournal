/*
 * MoreXournal
 *
 * macOS native window chrome helpers (unified transparent title bar and traffic light placement)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gtk/gtk.h>

namespace xoj::gui::mac {

/// Testing escape hatch: returns false when MOREXOURNAL_MACOS_TITLEBAR=native is set so the plain native title bar is
/// kept.
bool useUnifiedTitlebar();

/// Configures the realized GtkWindow's NSWindow for the unified transparent title bar and keeps traffic lights centred
/// in the top bar.
void configureUnifiedTitlebar(GtkWidget* window, int topBarHeight);

/// Updates the top bar height (48 px compact, 56 px touch mode) and re-centres the traffic lights.
void updateTopBarHeight(GtkWidget* window, int topBarHeight);

/// Zooms the window natively like double-clicking a macOS title bar.
void zoomWindow(GtkWidget* window);

}  // namespace xoj::gui::mac
