/*
 * Xournal++
 *
 * Handles input of strokes
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>  // for unique_ptr

#include <gdk/gdk.h>  // for GdkEventKey

#include "control/shaperecognizer/ShapeRecognizerParams.h"  // for ShapeRecognizerParams
#include "model/PageRef.h"                                  // for PageRef
#include "model/Point.h"            // for Point
#include "util/raii/GSourceURef.h"  // for GSourceURef

#include "HoldDetector.h"            // for HoldDetector
#include "InputHandler.h"            // for InputHandler
#include "SnapToGridInputHandler.h"  // for SnapToGridInputHandler

class Control;
class Layer;
class PositionInputData;
class Stroke;

namespace xoj::util {
template <class T>
class DispatchPool;
};

namespace xoj::view {
class OverlayView;
class Repaintable;
class StrokeToolView;
};  // namespace xoj::view

namespace StrokeStabilizer {
class Base;
class Active;
}  // namespace StrokeStabilizer

/**
 * @brief The stroke handler draws a stroke on a XojPageView
 *
 * The stroke is drawn using a cairo_surface_t* as a mask:
 * As the pointer moves on the canvas single segments are
 * drawn opaquely on the initially transparent masking
 * surface. The surface is used to mask the stroke
 * when drawing it to the XojPageView
 */
class StrokeHandler: public InputHandler {
public:
    StrokeHandler(Control* control, const PageRef& page);
    ~StrokeHandler() override;

    void onSequenceCancelEvent() override;
    bool onMotionNotifyEvent(const PositionInputData& pos, double zoom) override;
    void onButtonReleaseEvent(const PositionInputData& pos, double zoom) override;
    void onButtonPressEvent(const PositionInputData& pos, double zoom) override;
    void onButtonDoublePressEvent(const PositionInputData& pos, double zoom) override;
    bool onKeyPressEvent(const KeyEvent& event) override;
    bool onKeyReleaseEvent(const KeyEvent& event) override;

    /**
     * @brief Add a straight line to the stroke (if the movement is valid).
     * The line may be subdivided into smaller segments if the pressure variation is too big.
     * @param point The endpoint of the added line
     */
    void paintTo(Point point);

    auto createView(xoj::view::Repaintable* parent) const -> std::unique_ptr<xoj::view::OverlayView> override;

    const std::shared_ptr<xoj::util::DispatchPool<xoj::view::StrokeToolView>>& getViewPool() const;

protected:
    /**
     * @brief Unconditionally add a segment to the stroke.
     * Warning: it does not set the width properly nor test if the motion is valid. Use paintTo instead.
     * @param point The endpoint of the added segment
     */
    void drawSegmentTo(const Point& point);

    void strokeRecognizerDetected(std::unique_ptr<Stroke> recognized, Layer* layer);

    /// Width of a shape recognized from the current stroke
    double getRecognizedWidth() const;

    /// Snap the bounding box of a recognized shape to the grid
    void snapRecognizedBoundsToGrid(Stroke& recognized) const;

    /**
     * @brief Snap a recognized line (2 points) to the rotation snapping angles, and its end points to the grid if
     * snapping of recognized shapes is enabled. Snapping the bounding box would change the line's angle.
     */
    void snapRecognizedLine(Stroke& recognized) const;

    /// (Re)start the timer that calls tryConvertToShape() once the pen has been held still long enough
    void restartHoldTimer();

    static void holdTimerCallback(StrokeHandler* self);

    /**
     * @brief Replace the stroke in progress by the recognized shape, if any.
     * If no shape is recognized, the stroke is kept and the hold detector is disarmed until the pen moves again.
     */
    void tryConvertToShape();

    /// Finalizes the stroke using the provided pressure as last point
    void finalizeStroke(double pressure);

protected:
    Point buttonDownPoint;  // used for tapSelect and filtering - never snapped to grid.
    SnapToGridInputHandler snappingHandler;

    bool hasPressure;

    /// Whether a stroke started by this handler moves its colour to the front of the recent colours
    bool recordsUsedColor = true;

private:
    /**
     * @brief Pointer to the Stabilizer instance
     */
    std::unique_ptr<StrokeStabilizer::Base> stabilizer;

    std::shared_ptr<xoj::util::DispatchPool<xoj::view::StrokeToolView>> viewPool;

    /**
     * Hold to shape: convert the stroke to a shape when the pen stays still for holdToShapeDelay ms
     */
    bool holdToShapeEnabled = false;
    unsigned int holdToShapeDelay = 0;  ///< in ms
    ShapeRecognizerParams shapeRecognizerParams;
    HoldDetector holdDetector;
    xoj::util::GSourceURef holdTimer;
    /// Last position of the input device in page coordinates, before stabilization
    Point lastRawPos;
    /// Once converted, the shape is final: further motion is ignored and the stroke is not finalized
    bool convertedToShape = false;

    friend class StrokeStabilizer::Active;

    static constexpr double MAX_WIDTH_VARIATION = 0.3;
};
