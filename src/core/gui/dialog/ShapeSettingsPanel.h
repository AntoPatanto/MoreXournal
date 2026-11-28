/*
 * MoreXournal
 *
 * Preferences panel for automatic shapes: sensitivity, thresholds and interactive test pad
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <gtk/gtk.h>

#include "control/shaperecognizer/ShapeRecognizerParams.h"
#include "control/tools/HoldDetector.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "util/raii/GSourceURef.h"

class Settings;

class ShapeSettingsPanel {
public:
    explicit ShapeSettingsPanel(std::function<double()> getZoom100);
    ~ShapeSettingsPanel();

    ShapeSettingsPanel(const ShapeSettingsPanel&) = delete;
    ShapeSettingsPanel& operator=(const ShapeSettingsPanel&) = delete;

    GtkWidget* getPanel() const { return rootBox; }
    GtkWidget* getAdvancedCard() const { return advancedCard; }
    GtkExpander* getAdvancedExpander() const { return GTK_EXPANDER(advancedExpander); }
    GtkWidget* getRowWidget(const char* id) const;

    void load(const Settings& settings);
    void save(Settings& settings) const;

    /// Enable or disable hold-specific controls (Delay, Jitter allowed)
    void setHoldControlsSensitive(bool sensitive);

    ShapeRecognizerParams currentParams() const;
    int currentDelayMs() const;
    double currentHoldTolerancePx() const;
    double currentMinSizePt() const;
    int currentSensitivity() const;

private:
    struct AdvRow {
        GtkScale* scale = nullptr;
        GtkLabel* valueLabel = nullptr;
        int decimals = 3;
        const char* suffix = "";
        double ShapeRecognizerParams::* member = nullptr;
    };

    struct PadStroke {
        std::vector<Point> raw;
        std::unique_ptr<Stroke> recognized;
    };

    GtkWidget* buildMainControls();
    GtkWidget* buildAdvancedExpander();
    GtkWidget* buildTestPad();

    void onSensitivityChanged();
    void onAdvancedParamChanged();
    void onRestoreDefaults();

    void applyParamsToAdvancedSliders(const ShapeRecognizerParams& params);
    void updateMainValueLabels();
    void updateAdvancedValueLabels();
    void updateCustomBadge();

    // Test pad handlers
    static gboolean onPadDraw(GtkWidget* widget, cairo_t* cr, gpointer data);
    static gboolean onPadButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data);
    static gboolean onPadMotion(GtkWidget* widget, GdkEventMotion* event, gpointer data);
    static gboolean onPadButtonRelease(GtkWidget* widget, GdkEventButton* event, gpointer data);
    static gboolean onPadGrabBroken(GtkWidget* widget, GdkEventGrabBroken* event, gpointer data);
    static void onPadRealize(GtkWidget* widget, gpointer data);
    static void onPadHoldTimeout(ShapeSettingsPanel* self);

    void drawPad(cairo_t* cr, int width, int height);
    void restartPadHoldTimer();
    void tryConvertPadStroke();
    void finishPadStroke();
    void clearPad();
    void updateDiagnostics(const ShapeRecognizerDiagnostics& diag);
    void resetDiagnostics(const char* message);

    std::function<double()> getZoom100;

    GtkWidget* rootBox = nullptr;
    GtkWidget* advancedCard = nullptr;

    // Hold-specific widgets (disabled when hold-to-shape is turned off)
    GtkWidget* delayRow = nullptr;
    GtkWidget* delayDepHint = nullptr;
    GtkScale* delayScale = nullptr;
    GtkLabel* delayValueLabel = nullptr;

    GtkWidget* jitterRow = nullptr;
    GtkWidget* jitterDepHint = nullptr;
    GtkScale* jitterScale = nullptr;
    GtkLabel* jitterValueLabel = nullptr;

    // Sensitivity & min size
    GtkWidget* sensitivityRow = nullptr;
    GtkScale* sensitivityScale = nullptr;
    GtkLabel* sensitivityValueLabel = nullptr;
    GtkLabel* customBadgeLabel = nullptr;

    GtkWidget* minSizeRow = nullptr;
    GtkScale* minSizeScale = nullptr;
    GtkLabel* minSizeValueLabel = nullptr;

    GtkWidget* padRow = nullptr;
    GtkWidget* resetRow = nullptr;

    GtkWidget* advancedExpander = nullptr;
    std::array<AdvRow, 8> advRows{};
    std::array<GtkWidget*, 8> advRowWidgets{};

    bool updatingSliders = false;
    bool customThresholds = false;

    // Test pad widgets and state
    GtkWidget* padArea = nullptr;
    GtkLabel* diagResultLabel = nullptr;
    GtkLabel* diagReasonsLabel = nullptr;

    std::vector<PadStroke> completedStrokes;
    PadStroke activeStroke;
    bool drawing = false;
    double activeZoom = 1.0;
    Point lastRawPos;
    HoldDetector holdDetector;
    xoj::util::GSourceURef holdTimer;
    xoj::util::GSourceURef animTimer;
    gint64 holdStartUs = 0;
};
