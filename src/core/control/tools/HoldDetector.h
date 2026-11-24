/*
 * Xournal++
 *
 * Detects when the input device is held still
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "util/Point.h"  // for Point

/// Movements up to this distance (in screen pixels) do not count as moving the pen
constexpr double HOLD_TO_SHAPE_TOLERANCE_PX = 8.0;

/**
 * @brief Tells whether the input device moved away from an anchor point.
 *
 * The caller owns the timer: it starts it with start() and restarts it whenever update() returns true.
 * The detector only decides which movements are large enough to count, so that jitter and pressure-only events do not
 * restart the timer.
 */
class HoldDetector {
public:
    explicit HoldDetector(double tolerancePx = HOLD_TO_SHAPE_TOLERANCE_PX);

    /**
     * @brief Anchor the detector at the given position and arm it.
     * @param pagePos Position in page coordinates
     */
    void start(const xoj::util::Point<double>& pagePos);

    /**
     * @brief Process a new position of the input device.
     * @param pagePos Position in page coordinates
     * @param zoom Current zoom, used to convert the distance to screen pixels
     * @return true if the device moved further than the tolerance from the anchor. The detector is then re-anchored at
     * pagePos and armed, and the caller should restart its timer.
     */
    bool update(const xoj::util::Point<double>& pagePos, double zoom);

    /// Disarm the detector, e.g. after a hold that did not lead to anything. The next movement re-arms it.
    void disarm();

    bool isArmed() const;

    const xoj::util::Point<double>& getAnchor() const;

    double getTolerancePx() const;
    void setTolerancePx(double tolerancePx);

private:
    double tolerancePx;
    xoj::util::Point<double> anchor;
    bool armed = false;
};
