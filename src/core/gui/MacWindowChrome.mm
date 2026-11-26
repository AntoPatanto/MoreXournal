#include "MacWindowChrome.h"

#include <algorithm>
#include <cmath>

#include <glib.h>

#ifdef GDK_WINDOWING_QUARTZ
#import <AppKit/AppKit.h>
#include <gdk/gdkquartz.h>
// The header has no G_BEGIN_DECLS; AppKit and gdk.h are already included above, so only its own declarations get C linkage
extern "C" {
#include <gdk/quartz/gdkquartz-cocoa-access.h>  // gdk_quartz_window_get_nswindow (Objective-C only)
}

namespace {
constexpr const char* kChromeObserverKey = "mx-mac-window-chrome-observer";
}  // namespace

@interface MxMacChromeObserver : NSObject {
@private
    NSWindow* _nsWindow;
    CGFloat _barHeight;
    CGFloat _buttonSpacing;
}

- (instancetype)initWithWindow:(NSWindow*)window barHeight:(CGFloat)barHeight;
- (void)setBarHeight:(CGFloat)barHeight;
- (void)layoutTrafficLights;
- (void)scheduleLayoutTrafficLights;
- (void)invalidate;

@end

@implementation MxMacChromeObserver

- (instancetype)initWithWindow:(NSWindow*)window barHeight:(CGFloat)barHeight {
    self = [super init];
    if (self != nil) {
        _nsWindow = window;
        _barHeight = barHeight;
        _buttonSpacing = 20.0;

        NSButton* closeBtn = [_nsWindow standardWindowButton:NSWindowCloseButton];
        NSButton* minBtn = [_nsWindow standardWindowButton:NSWindowMiniaturizeButton];
        if (closeBtn != nil && minBtn != nil) {
            const CGFloat spacing = minBtn.frame.origin.x - closeBtn.frame.origin.x;
            if (spacing > 0.0) {
                _buttonSpacing = spacing;
            }
        }

        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        [center addObserver:self
                   selector:@selector(onWindowNotification:)
                       name:NSWindowDidResizeNotification
                     object:_nsWindow];
        [center addObserver:self
                   selector:@selector(onWindowNotification:)
                       name:NSWindowDidExitFullScreenNotification
                     object:_nsWindow];
        [center addObserver:self
                   selector:@selector(onWindowNotification:)
                       name:NSWindowDidBecomeKeyNotification
                     object:_nsWindow];
        [center addObserver:self
                   selector:@selector(onWindowWillClose:)
                       name:NSWindowWillCloseNotification
                     object:_nsWindow];
    }
    return self;
}

- (void)setBarHeight:(CGFloat)barHeight {
    _barHeight = barHeight;
    [self layoutTrafficLights];
    [self scheduleLayoutTrafficLights];
}

- (void)onWindowNotification:(NSNotification*)notification {
    (void)notification;
    [self layoutTrafficLights];
    // AppKit can run another titlebar layout pass after fullscreen exit or resize; re-apply on the next turn.
    [self scheduleLayoutTrafficLights];
}

- (void)onWindowWillClose:(NSNotification*)notification {
    (void)notification;
    [self invalidate];
}

- (void)scheduleLayoutTrafficLights {
    if (_nsWindow == nil) {
        return;
    }
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(layoutTrafficLights) object:nil];
    [self performSelector:@selector(layoutTrafficLights) withObject:nil afterDelay:0.0];
}

- (void)layoutTrafficLights {
    if (_nsWindow == nil) {
        return;
    }
    // In macOS fullscreen the traffic lights move to the auto-hiding menu bar overlay.
    if ((_nsWindow.styleMask & NSWindowStyleMaskFullScreen) != 0) {
        return;
    }

    NSButton* closeBtn = [_nsWindow standardWindowButton:NSWindowCloseButton];
    NSButton* minBtn = [_nsWindow standardWindowButton:NSWindowMiniaturizeButton];
    NSButton* zoomBtn = [_nsWindow standardWindowButton:NSWindowZoomButton];
    if (closeBtn == nil || minBtn == nil || zoomBtn == nil) {
        return;
    }

    NSView* titlebarView = closeBtn.superview;
    NSView* titlebarContainer = titlebarView != nil ? titlebarView.superview : nil;
    NSView* themeFrame = titlebarContainer != nil ? titlebarContainer.superview : nil;
    if (titlebarView == nil || titlebarContainer == nil || themeFrame == nil) {
        return;
    }

    constexpr CGFloat kLeftInset = 14.0;
    const CGFloat barHeight = _barHeight;

    // Expand the container vertically so traffic lights centred in a 48/56 pt bar are not clipped below AppKit's
    // default 28 pt title bar, and limit its width to the traffic-light cluster so clicks on GTK top-bar widgets
    // hit the content view directly.
    const CGFloat zoomWidth = NSWidth(zoomBtn.frame) > 0.0 ? NSWidth(zoomBtn.frame) : 14.0;
    const CGFloat clusterWidth =
            std::min(NSWidth(themeFrame.bounds), kLeftInset + 2.0 * _buttonSpacing + zoomWidth + 8.0);

    NSRect containerFrame = titlebarContainer.frame;
    containerFrame.origin.x = 0.0;
    containerFrame.size.width = clusterWidth;
    containerFrame.size.height = barHeight;
    containerFrame.origin.y = themeFrame.isFlipped ? 0.0 : (NSHeight(themeFrame.bounds) - barHeight);
    titlebarContainer.frame = containerFrame;

    NSRect titlebarFrame = titlebarView.frame;
    titlebarFrame.origin.x = 0.0;
    titlebarFrame.origin.y = 0.0;
    titlebarFrame.size.width = clusterWidth;
    titlebarFrame.size.height = barHeight;
    titlebarView.frame = titlebarFrame;

    NSButton* buttons[3] = {closeBtn, minBtn, zoomBtn};
    for (int i = 0; i < 3; ++i) {
        NSButton* btn = buttons[i];
        NSRect f = btn.frame;
        f.origin.x = kLeftInset + static_cast<CGFloat>(i) * _buttonSpacing;
        f.origin.y = std::floor((barHeight - NSHeight(f)) / 2.0);
        btn.frame = f;
    }
}

- (void)invalidate {
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _nsWindow = nil;
}

- (void)dealloc {
    [self invalidate];
    [super dealloc];
}

@end

namespace {
void destroyObserver(gpointer data) {
    auto* observer = static_cast<MxMacChromeObserver*>(data);
    if (observer != nil) {
        [observer invalidate];
        [observer release];
    }
}

void onWindowDestroy(GtkWidget* window, gpointer) {
    g_object_set_data(G_OBJECT(window), kChromeObserverKey, nullptr);
}
}  // namespace
#endif  // GDK_WINDOWING_QUARTZ

namespace xoj::gui::mac {

bool useUnifiedTitlebar() {
    // Testing escape hatch: set MOREXOURNAL_MACOS_TITLEBAR=native to keep the plain native title bar.
    static const bool unified = g_strcmp0(g_getenv("MOREXOURNAL_MACOS_TITLEBAR"), "native") != 0;
    return unified;
}

void configureUnifiedTitlebar(GtkWidget* window, int topBarHeight) {
    if (!useUnifiedTitlebar() || window == nullptr) {
        return;
    }
#ifdef GDK_WINDOWING_QUARTZ
    GdkWindow* gdkWindow = gtk_widget_get_window(window);
    if (gdkWindow == nullptr) {
        return;
    }
    NSWindow* nsWindow = gdk_quartz_window_get_nswindow(gdkWindow);
    if (nsWindow == nil) {
        return;
    }

    nsWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
    nsWindow.titlebarAppearsTransparent = YES;
    nsWindow.titleVisibility = NSWindowTitleHidden;
    if (@available(macOS 11.0, *)) {
        nsWindow.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
    }

    MxMacChromeObserver* observer =
            [[MxMacChromeObserver alloc] initWithWindow:nsWindow barHeight:static_cast<CGFloat>(topBarHeight)];
    g_object_set_data_full(G_OBJECT(window), kChromeObserverKey, observer, destroyObserver);
    g_signal_connect(window, "destroy", G_CALLBACK(onWindowDestroy), nullptr);

    [observer layoutTrafficLights];
    [observer scheduleLayoutTrafficLights];
#else
    (void)topBarHeight;
#endif
}

void updateTopBarHeight(GtkWidget* window, int topBarHeight) {
    if (!useUnifiedTitlebar() || window == nullptr) {
        return;
    }
#ifdef GDK_WINDOWING_QUARTZ
    auto* observer = static_cast<MxMacChromeObserver*>(g_object_get_data(G_OBJECT(window), kChromeObserverKey));
    if (observer != nil) {
        [observer setBarHeight:static_cast<CGFloat>(topBarHeight)];
    }
#else
    (void)topBarHeight;
#endif
}

void zoomWindow(GtkWidget* window) {
    if (window == nullptr) {
        return;
    }
#ifdef GDK_WINDOWING_QUARTZ
    if (GdkWindow* gdkWindow = gtk_widget_get_window(window)) {
        if (NSWindow* nsWindow = gdk_quartz_window_get_nswindow(gdkWindow)) {
            [nsWindow performZoom:nil];
            return;
        }
    }
#endif
    if (gtk_window_is_maximized(GTK_WINDOW(window))) {
        gtk_window_unmaximize(GTK_WINDOW(window));
    } else {
        gtk_window_maximize(GTK_WINDOW(window));
    }
}

}  // namespace xoj::gui::mac
