/*
 * MoreXournal
 *
 * State machine that keeps the overlay vertical scrollbar disabled until
 * sustained page scrolling occurs, then hides it again after inactivity.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cmath>
#include <cstdint>

namespace xoj::scroll {

enum class VerticalScrollbarMode {
    Hidden,         ///< Explicitly hidden in Preferences (SCROLLBAR_HIDE_VERTICAL)
    AlwaysVisible,  ///< Non-overlay mode (disableScrollbarFadeout == true)
    ScrollOnly,     ///< Overlay mode: disabled until real page scrolling, auto-hides after 1 s
};

struct VerticalScrollbarRevealAction {
    bool visible = false;
    bool armHideTimer = false;
    bool cancelHideTimer = false;
};

class VerticalScrollbarRevealState {
public:
    /// Minimum net vertical scroll distance (in pixels) required to enable the scrollbar
    static constexpr double ACTIVATION_SCROLL_PX = 120.0;
    /// Maximum pause between consecutive scroll steps while accumulating toward activation
    static constexpr int64_t ACCUMULATION_WINDOW_MS = 450;
    /// Inactivity timeout after scrolling stops before disabling the scrollbar again
    static constexpr int64_t HIDE_DELAY_MS = 1000;
    /// Cooldown after adjustment range/page-size changes (zoom, document load, window resize)
    static constexpr int64_t RANGE_CHANGE_COOLDOWN_MS = 200;
    /// Opposite-direction step size that resets the directional accumulator
    static constexpr double DIRECTION_RESET_THRESHOLD_PX = 4.0;
    /// Sub-pixel noise floor ignored when checking movement
    static constexpr double MIN_STEP_PX = 0.5;

    static constexpr bool isVerticallyScrollable(double upper, double pageSize) {
        return (upper - pageSize) > MIN_STEP_PX;
    }

    auto configure(bool hiddenByPreference, bool fadeoutDisabled) -> VerticalScrollbarRevealAction {
        if (hiddenByPreference) {
            mode = VerticalScrollbarMode::Hidden;
        } else if (fadeoutDisabled) {
            mode = VerticalScrollbarMode::AlwaysVisible;
        } else {
            mode = VerticalScrollbarMode::ScrollOnly;
        }

        visible = (mode == VerticalScrollbarMode::AlwaysVisible);
        interacting = false;
        accumulatedSignedPx = 0.0;
        lastScrollMs = -1;
        return {visible, false, true};
    }

    auto onRangeChanged(double currentValue, double upper, double pageSize, int64_t nowMs)
            -> VerticalScrollbarRevealAction {
        lastValue = currentValue;
        hasBaseline = true;

        const bool rangeChanged = !hasRangeBaseline || std::abs(upper - lastUpper) > MIN_STEP_PX ||
                                  std::abs(pageSize - lastPageSize) > MIN_STEP_PX;
        if (rangeChanged) {
            lastUpper = upper;
            lastPageSize = pageSize;
            hasRangeBaseline = true;
            accumulatedSignedPx = 0.0;
            suppressUntilMs = nowMs + RANGE_CHANGE_COOLDOWN_MS;
        }

        if (mode != VerticalScrollbarMode::ScrollOnly) {
            return {mode == VerticalScrollbarMode::AlwaysVisible, false, false};
        }

        if (!isVerticallyScrollable(upper, pageSize)) {
            visible = false;
            interacting = false;
            accumulatedSignedPx = 0.0;
            return {false, false, true};
        }

        return {visible, false, false};
    }

    auto onValueChanged(double newValue, double upper, double pageSize, bool zoomInProgress, int64_t nowMs)
            -> VerticalScrollbarRevealAction {
        if (mode != VerticalScrollbarMode::ScrollOnly) {
            lastValue = newValue;
            hasBaseline = true;
            return {mode == VerticalScrollbarMode::AlwaysVisible, false, false};
        }

        if (!hasBaseline) {
            lastValue = newValue;
            hasBaseline = true;
            return {visible, false, false};
        }

        const double delta = newValue - lastValue;
        lastValue = newValue;

        if (!isVerticallyScrollable(upper, pageSize)) {
            visible = false;
            interacting = false;
            accumulatedSignedPx = 0.0;
            return {false, false, true};
        }

        if (interacting) {
            visible = true;
            accumulatedSignedPx = 0.0;
            return {true, false, true};
        }

        if (zoomInProgress || nowMs < suppressUntilMs) {
            accumulatedSignedPx = 0.0;
            return {visible, false, false};
        }

        if (visible) {
            if (std::abs(delta) >= MIN_STEP_PX) {
                lastScrollMs = nowMs;
                return {true, true, false};
            }
            return {true, false, false};
        }

        if (std::abs(delta) < MIN_STEP_PX) {
            return {false, false, false};
        }

        if (lastScrollMs >= 0 && (nowMs - lastScrollMs) > ACCUMULATION_WINDOW_MS) {
            accumulatedSignedPx = 0.0;
        }

        if (accumulatedSignedPx != 0.0 && ((delta > 0.0) != (accumulatedSignedPx > 0.0))) {
            if (std::abs(delta) >= DIRECTION_RESET_THRESHOLD_PX) {
                accumulatedSignedPx = delta;
            } else {
                accumulatedSignedPx += delta;
            }
        } else {
            accumulatedSignedPx += delta;
        }

        lastScrollMs = nowMs;

        if (std::abs(accumulatedSignedPx) >= ACTIVATION_SCROLL_PX) {
            visible = true;
            accumulatedSignedPx = 0.0;
            return {true, true, false};
        }

        return {false, false, false};
    }

    auto setScrollbarInteracting(bool active) -> VerticalScrollbarRevealAction {
        if (mode != VerticalScrollbarMode::ScrollOnly) {
            interacting = false;
            return {mode == VerticalScrollbarMode::AlwaysVisible, false, false};
        }

        interacting = active && visible;
        if (interacting) {
            return {true, false, true};
        }

        if (visible) {
            return {true, true, false};
        }
        return {false, false, false};
    }

    auto onHideTimeout() -> VerticalScrollbarRevealAction {
        if (mode != VerticalScrollbarMode::ScrollOnly || interacting) {
            return {visible, false, false};
        }

        visible = false;
        accumulatedSignedPx = 0.0;
        return {false, false, false};
    }

    [[nodiscard]] auto isVisible() const -> bool { return visible; }
    [[nodiscard]] auto getMode() const -> VerticalScrollbarMode { return mode; }
    [[nodiscard]] auto isInteracting() const -> bool { return interacting; }

private:
    VerticalScrollbarMode mode = VerticalScrollbarMode::ScrollOnly;
    bool visible = false;
    bool interacting = false;
    bool hasBaseline = false;
    bool hasRangeBaseline = false;
    double lastValue = 0.0;
    double lastUpper = 0.0;
    double lastPageSize = 0.0;
    double accumulatedSignedPx = 0.0;
    int64_t lastScrollMs = -1;
    int64_t suppressUntilMs = -1;
};

}  // namespace xoj::scroll
