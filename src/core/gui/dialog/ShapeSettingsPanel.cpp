#include "ShapeSettingsPanel.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "control/settings/Settings.h"
#include "control/shaperecognizer/ShapeRecognizer.h"
#include "control/shaperecognizer/ShapeSensitivityLogic.h"
#include "util/glib_casts.h"
#include "util/i18n.h"
#include "util/safe_casts.h"

namespace {

void addCssClass(GtkWidget* widget, const char* cssClass) {
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), cssClass);
}

void removeCssClass(GtkWidget* widget, const char* cssClass) {
    gtk_style_context_remove_class(gtk_widget_get_style_context(widget), cssClass);
}

GtkWidget* makeRowLabel(const char* title, const char* hint) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

    GtkWidget* lblTitle = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(lblTitle), 0.0f);
    gtk_box_pack_start(GTK_BOX(box), lblTitle, false, false, 0);

    if (hint && *hint) {
        GtkWidget* lblHint = gtk_label_new(hint);
        gtk_label_set_xalign(GTK_LABEL(lblHint), 0.0f);
        addCssClass(lblHint, "mx-shape-hint");
        gtk_box_pack_start(GTK_BOX(box), lblHint, false, false, 0);
    }

    return box;
}

GtkLabel* makeValueLabel(int widthChars = 8) {
    GtkWidget* lbl = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(lbl), 1.0f);
    gtk_label_set_width_chars(GTK_LABEL(lbl), widthChars);
    gtk_widget_set_valign(lbl, GTK_ALIGN_CENTER);
    addCssClass(lbl, "mx-numeric");
    return GTK_LABEL(lbl);
}

GtkScale* makeScale(double minVal, double maxVal, double step, double page, double markVal, int roundDigits) {
    GtkAdjustment* adj = gtk_adjustment_new(markVal, minVal, maxVal, step, page, 0.0);
    GtkWidget* scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
    gtk_scale_set_draw_value(GTK_SCALE(scale), false);
    gtk_range_set_round_digits(GTK_RANGE(scale), roundDigits);
    gtk_scale_add_mark(GTK_SCALE(scale), markVal, GTK_POS_BOTTOM, nullptr);
    gtk_widget_set_hexpand(scale, true);
    gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
    // Let mouse-wheel scrolling scroll the Preferences pane instead of accidentally moving sliders under the pointer.
    g_signal_connect(scale, "scroll-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventScroll*, gpointer) -> gboolean {
                         g_signal_stop_emission_by_name(widget, "scroll-event");
                         return false;
                     }),
                     nullptr);
    return GTK_SCALE(scale);
}

GtkWidget* makeLegendItem(const char* dotClass, const char* text) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);

    GtkWidget* dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(dot, 8, 8);
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    addCssClass(dot, "mx-legend-dot");
    addCssClass(dot, dotClass);
    gtk_box_pack_start(GTK_BOX(box), dot, false, false, 0);

    GtkWidget* lbl = gtk_label_new(text);
    addCssClass(lbl, "mx-shape-hint");
    gtk_box_pack_start(GTK_BOX(box), lbl, false, false, 0);

    return box;
}

const char* shapeKindLabel(RecognizedShapeKind kind) {
    switch (kind) {
        case RecognizedShapeKind::Line:
            return _("Line");
        case RecognizedShapeKind::Triangle:
            return _("Triangle");
        case RecognizedShapeKind::Rectangle:
            return _("Rectangle");
        case RecognizedShapeKind::Circle:
            return _("Circle");
        case RecognizedShapeKind::None:
        default:
            return "";
    }
}

void drawStrokePath(cairo_t* cr, const std::vector<Point>& pts, double zoom, double r, double g, double b, double a,
                    double lineWidth) {
    if (pts.size() < 2) {
        return;
    }
    cairo_save(cr);
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_set_line_width(cr, lineWidth);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_move_to(cr, pts[0].x * zoom, pts[0].y * zoom);
    for (size_t i = 1; i < pts.size(); i++) {
        cairo_line_to(cr, pts[i].x * zoom, pts[i].y * zoom);
    }
    cairo_stroke(cr);
    cairo_restore(cr);
}

/// Disconnect the handlers connected with `data` on `w` and on every widget inside it
void disconnectTree(GtkWidget* w, gpointer data) {
    g_signal_handlers_disconnect_by_data(w, data);
    if (GTK_IS_CONTAINER(w)) {
        gtk_container_forall(
                GTK_CONTAINER(w), +[](GtkWidget* child, gpointer d) { disconnectTree(child, d); }, data);
    }
}

}  // namespace

ShapeSettingsPanel::ShapeSettingsPanel(std::function<double()> getZoom100): getZoom100(std::move(getZoom100)) {
    rootBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    // Released in the destructor, which needs the widget tree whichever of the panel and the window goes first
    g_object_ref_sink(rootBox);
    gtk_widget_set_name(rootBox, "shapeSettingsPanel");

    gtk_box_pack_start(GTK_BOX(rootBox), buildMainControls(), false, false, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), buildAdvancedExpander(), false, false, 0);

    // Restore defaults row
    GtkWidget* resetRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* resetBtn = gtk_button_new_with_label(_("Restore defaults"));
    gtk_widget_set_name(resetBtn, "btnRestoreShapeDefaults");
    g_signal_connect_swapped(resetBtn, "clicked",
                             G_CALLBACK(+[](ShapeSettingsPanel* self) { self->onRestoreDefaults(); }), this);
    gtk_box_pack_end(GTK_BOX(resetRow), resetBtn, false, false, 0);
    gtk_box_pack_start(GTK_BOX(rootBox), resetRow, false, false, 0);

    gtk_box_pack_start(GTK_BOX(rootBox), buildTestPad(), false, false, 0);

    gtk_widget_show_all(rootBox);
    updateCustomBadge();
}

ShapeSettingsPanel::~ShapeSettingsPanel() {
    holdTimer.cancel();
    animTimer.cancel();
    // SettingsDialog destroys the panel before its window, whose destruction can still emit signals (e.g. the pad's
    // grab-broken-event when it is unmapped mid-stroke)
    disconnectTree(rootBox, this);
    g_object_unref(rootBox);
}

GtkWidget* ShapeSettingsPanel::buildMainControls() {
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    // Row 0: Delay
    delayLabelBox = makeRowLabel(_("Delay"), _("How long to hold the pen still"));
    delayScale = makeScale(HOLD_TO_SHAPE_DELAY_MIN, HOLD_TO_SHAPE_DELAY_MAX, 50.0, 250.0, 400.0, 0);
    gtk_widget_set_name(GTK_WIDGET(delayScale), "scaleHoldToShapeDelay");
    delayValueLabel = makeValueLabel();
    g_signal_connect_swapped(delayScale, "value-changed",
                             G_CALLBACK(+[](ShapeSettingsPanel* self) { self->updateMainValueLabels(); }), this);
    gtk_grid_attach(GTK_GRID(grid), delayLabelBox, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(delayScale), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(delayValueLabel), 2, 0, 1, 1);

    // Row 1: Jitter allowed
    jitterLabelBox = makeRowLabel(_("Jitter allowed"), _("Movement that does not reset the delay"));
    jitterScale = makeScale(xoj::shapes::HOLD_TOLERANCE_PX_RANGE.min(), xoj::shapes::HOLD_TOLERANCE_PX_RANGE.max(), 1.0,
                            5.0, xoj::shapes::HOLD_TOLERANCE_PX_RANGE.def, 0);
    gtk_widget_set_name(GTK_WIDGET(jitterScale), "scaleHoldToShapeTolerance");
    jitterValueLabel = makeValueLabel();
    g_signal_connect_swapped(jitterScale, "value-changed",
                             G_CALLBACK(+[](ShapeSettingsPanel* self) { self->updateMainValueLabels(); }), this);
    gtk_grid_attach(GTK_GRID(grid), jitterLabelBox, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(jitterScale), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(jitterValueLabel), 2, 1, 1, 1);

    // Row 2: Sensitivity (with end labels Strict / Tolerant, mark at default 50, and Custom badge)
    GtkWidget* sensLabelBox = makeRowLabel(_("Sensitivity"), _("All shapes together"));

    GtkWidget* sensSliderBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(sensSliderBox, true);
    sensitivityScale = makeScale(xoj::shapes::SHAPE_SENSITIVITY_MIN, xoj::shapes::SHAPE_SENSITIVITY_MAX, 1.0, 10.0,
                                 xoj::shapes::SHAPE_SENSITIVITY_DEFAULT, 0);
    gtk_widget_set_name(GTK_WIDGET(sensitivityScale), "scaleShapeSensitivity");
    g_signal_connect_swapped(sensitivityScale, "value-changed",
                             G_CALLBACK(+[](ShapeSettingsPanel* self) { self->onSensitivityChanged(); }), this);
    gtk_box_pack_start(GTK_BOX(sensSliderBox), GTK_WIDGET(sensitivityScale), false, false, 0);

    GtkWidget* endsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* strictLbl = gtk_label_new(_("Strict"));
    gtk_label_set_xalign(GTK_LABEL(strictLbl), 0.0f);
    addCssClass(strictLbl, "mx-shape-end-label");
    GtkWidget* tolerantLbl = gtk_label_new(_("Tolerant"));
    gtk_label_set_xalign(GTK_LABEL(tolerantLbl), 1.0f);
    addCssClass(tolerantLbl, "mx-shape-end-label");
    gtk_box_pack_start(GTK_BOX(endsBox), strictLbl, true, true, 0);
    gtk_box_pack_end(GTK_BOX(endsBox), tolerantLbl, true, true, 0);
    gtk_box_pack_start(GTK_BOX(sensSliderBox), endsBox, false, false, 0);

    GtkWidget* sensValBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(sensValBox, GTK_ALIGN_CENTER);
    sensitivityValueLabel = makeValueLabel();
    gtk_box_pack_start(GTK_BOX(sensValBox), GTK_WIDGET(sensitivityValueLabel), false, false, 0);

    GtkWidget* customLbl = gtk_label_new(C_("shape sensitivity", "Custom"));
    gtk_widget_set_name(customLbl, "lblShapeSensitivityCustom");
    gtk_widget_set_halign(customLbl, GTK_ALIGN_END);
    gtk_widget_set_no_show_all(customLbl, true);
    addCssClass(customLbl, "mx-shape-custom");
    customBadgeLabel = GTK_LABEL(customLbl);
    gtk_box_pack_start(GTK_BOX(sensValBox), customLbl, false, false, 0);

    gtk_grid_attach(GTK_GRID(grid), sensLabelBox, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sensSliderBox, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sensValBox, 2, 2, 1, 1);

    // Row 3: Minimum size
    GtkWidget* minSizeLabelBox = makeRowLabel(_("Minimum size"), _("Smaller strokes stay freehand"));
    minSizeScale = makeScale(xoj::shapes::STROKE_MIN_SIZE_RANGE.min(), xoj::shapes::STROKE_MIN_SIZE_RANGE.max(), 5.0,
                             15.0, xoj::shapes::STROKE_MIN_SIZE_RANGE.def, 0);
    gtk_widget_set_name(GTK_WIDGET(minSizeScale), "scaleStrokeRecognizerMinSize");
    minSizeValueLabel = makeValueLabel();
    g_signal_connect_swapped(minSizeScale, "value-changed",
                             G_CALLBACK(+[](ShapeSettingsPanel* self) { self->updateMainValueLabels(); }), this);
    gtk_grid_attach(GTK_GRID(grid), minSizeLabelBox, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(minSizeScale), 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(minSizeValueLabel), 2, 3, 1, 1);

    return grid;
}

GtkWidget* ShapeSettingsPanel::buildAdvancedExpander() {
    advancedExpander = gtk_expander_new(_("Advanced"));
    gtk_widget_set_name(advancedExpander, "expShapeAdvanced");
    gtk_expander_set_expanded(GTK_EXPANDER(advancedExpander), false);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top(grid, 6);
    gtk_widget_set_margin_start(grid, 12);

    struct Spec {
        const char* name;
        const char* label;
        xoj::shapes::ParamRange range;
        double step;
        double page;
        int decimals;
        const char* suffix;
        double ShapeRecognizerParams::* member;
    };

    const std::array<Spec, 8> specs = {{
            {"scaleShapeLineMaxDet", _("Line: straightness"), xoj::shapes::LINE_MAX_DET_RANGE, 0.001, 0.005, 3, "",
             &ShapeRecognizerParams::lineMaxDet},
            {"scaleShapeSegmentMaxDet", _("Polygon sides: straightness"), xoj::shapes::SEGMENT_MAX_DET_RANGE, 0.001,
             0.01, 3, "", &ShapeRecognizerParams::segmentMaxDet},
            {"scaleShapeCircleMinDet", _("Circle: roundness"), xoj::shapes::CIRCLE_MIN_DET_RANGE, 0.001, 0.01, 3, "",
             &ShapeRecognizerParams::circleMinDet},
            {"scaleShapeCircleMaxScore", _("Circle: radius error"), xoj::shapes::CIRCLE_MAX_SCORE_RANGE, 0.001, 0.01, 3,
             "", &ShapeRecognizerParams::circleMaxScore},
            {"scaleShapeRectAngleTol", _("Rectangle: corner angle"), xoj::shapes::RECTANGLE_ANGLE_TOLERANCE_DEG_RANGE,
             1.0, 3.0, 0, "°", &ShapeRecognizerParams::rectangleAngleToleranceDeg},
            {"scaleShapeRectLinearTol", _("Rectangle: open corners"), xoj::shapes::RECTANGLE_LINEAR_TOLERANCE_RANGE,
             0.01, 0.05, 2, "", &ShapeRecognizerParams::rectangleLinearTolerance},
            {"scaleShapeTriLinearTol", _("Triangle: open corners"), xoj::shapes::TRIANGLE_LINEAR_TOLERANCE_RANGE, 0.01,
             0.05, 2, "", &ShapeRecognizerParams::triangleLinearTolerance},
            {"scaleShapeSlantTol", _("Straighten near-axis angle"), xoj::shapes::SLANT_TOLERANCE_DEG_RANGE, 1.0, 3.0, 0,
             "°", &ShapeRecognizerParams::slantToleranceDeg},
    }};

    for (size_t i = 0; i < specs.size(); i++) {
        const auto& s = specs[i];
        GtkWidget* lbl = gtk_label_new(s.label);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_widget_set_valign(lbl, GTK_ALIGN_CENTER);

        // Keep round-digits = -1 so values written by paramsForSensitivity() keep full precision.
        GtkScale* scale = makeScale(s.range.min(), s.range.max(), s.step, s.page, s.range.def, -1);
        gtk_widget_set_name(GTK_WIDGET(scale), s.name);
        GtkLabel* valLbl = makeValueLabel();

        g_signal_connect_swapped(scale, "value-changed",
                                 G_CALLBACK(+[](ShapeSettingsPanel* self) { self->onAdvancedParamChanged(); }), this);

        gtk_grid_attach(GTK_GRID(grid), lbl, 0, static_cast<gint>(i), 1, 1);
        gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(scale), 1, static_cast<gint>(i), 1, 1);
        gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(valLbl), 2, static_cast<gint>(i), 1, 1);

        advRows[i] = AdvRow{scale, valLbl, s.decimals, s.suffix, s.member};
    }

    gtk_container_add(GTK_CONTAINER(advancedExpander), grid);
    return advancedExpander;
}

GtkWidget* ShapeSettingsPanel::buildTestPad() {
    GtkWidget* wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addCssClass(wrap, "mx-shape-pad-wrap");

    // Header bar
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    addCssClass(bar, "mx-shape-pad-bar");

    GtkWidget* title = gtk_label_new(nullptr);
    g_autofree gchar* escapedTitle = g_markup_escape_text(_("Try it here"), -1);
    g_autofree gchar* markup = g_strdup_printf("<b>%s</b>", escapedTitle);
    gtk_label_set_markup(GTK_LABEL(title), markup);
    gtk_box_pack_start(GTK_BOX(bar), title, false, false, 0);

    gtk_box_pack_start(GTK_BOX(bar), makeLegendItem("mx-dot-raw", _("stroke")), false, false, 0);
    gtk_box_pack_start(GTK_BOX(bar), makeLegendItem("mx-dot-shape", _("recognized shape")), false, false, 0);
    gtk_box_pack_start(GTK_BOX(bar), makeLegendItem("mx-dot-hold", _("tolerance and delay")), false, false, 0);

    GtkWidget* clearBtn = gtk_button_new_with_label(_("Clear"));
    gtk_widget_set_name(clearBtn, "btnClearShapePad");
    g_signal_connect_swapped(clearBtn, "clicked", G_CALLBACK(+[](ShapeSettingsPanel* self) { self->clearPad(); }),
                             this);
    gtk_box_pack_end(GTK_BOX(bar), clearBtn, false, false, 0);

    gtk_box_pack_start(GTK_BOX(wrap), bar, false, false, 0);

    // Drawing area (300 px tall, white)
    padArea = gtk_drawing_area_new();
    gtk_widget_set_name(padArea, "shapeTestPad");
    gtk_widget_set_size_request(padArea, -1, 300);
    gtk_widget_set_hexpand(padArea, true);
    gtk_widget_add_events(padArea, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(padArea, "realize", G_CALLBACK(onPadRealize), this);
    g_signal_connect(padArea, "draw", G_CALLBACK(onPadDraw), this);
    g_signal_connect(padArea, "button-press-event", G_CALLBACK(onPadButtonPress), this);
    g_signal_connect(padArea, "motion-notify-event", G_CALLBACK(onPadMotion), this);
    g_signal_connect(padArea, "button-release-event", G_CALLBACK(onPadButtonRelease), this);
    g_signal_connect(padArea, "grab-broken-event", G_CALLBACK(onPadGrabBroken), this);

    gtk_box_pack_start(GTK_BOX(wrap), padArea, false, false, 0);

    // Diagnostics output below the pad
    GtkWidget* diagBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    addCssClass(diagBox, "mx-shape-diag");

    GtkWidget* resLbl = gtk_label_new(_("Draw a line, triangle, rectangle or circle and hold still at the end without lifting."));
    gtk_widget_set_name(resLbl, "lblShapeDiagResult");
    gtk_label_set_xalign(GTK_LABEL(resLbl), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(resLbl), true);
    gtk_label_set_max_width_chars(GTK_LABEL(resLbl), 75);
    addCssClass(resLbl, "mx-shape-hint");
    diagResultLabel = GTK_LABEL(resLbl);
    gtk_box_pack_start(GTK_BOX(diagBox), resLbl, false, false, 0);

    GtkWidget* reasonsLbl = gtk_label_new("");
    gtk_widget_set_name(reasonsLbl, "lblShapeDiagReasons");
    gtk_label_set_xalign(GTK_LABEL(reasonsLbl), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(reasonsLbl), true);
    gtk_label_set_max_width_chars(GTK_LABEL(reasonsLbl), 75);
    gtk_widget_set_no_show_all(reasonsLbl, true);
    addCssClass(reasonsLbl, "mx-diag-reasons");
    diagReasonsLabel = GTK_LABEL(reasonsLbl);
    gtk_box_pack_start(GTK_BOX(diagBox), reasonsLbl, false, false, 0);

    gtk_box_pack_start(GTK_BOX(wrap), diagBox, false, false, 0);

    return wrap;
}

void ShapeSettingsPanel::load(const Settings& settings) {
    updatingSliders = true;

    gtk_range_set_value(GTK_RANGE(delayScale), settings.getHoldToShapeDelay());
    gtk_range_set_value(GTK_RANGE(jitterScale), settings.getHoldToShapeTolerancePx());
    gtk_range_set_value(GTK_RANGE(sensitivityScale), settings.getShapeSensitivity());
    gtk_range_set_value(GTK_RANGE(minSizeScale), settings.getStrokeRecognizerMinSize());

    applyParamsToAdvancedSliders(settings.getShapeRecognizerParams());
    customThresholds = !xoj::shapes::matchesSensitivity(settings.getShapeRecognizerParams(),
                                                        settings.getShapeSensitivity());

    updatingSliders = false;

    updateMainValueLabels();
    updateAdvancedValueLabels();
    updateCustomBadge();
}

void ShapeSettingsPanel::save(Settings& settings) const {
    settings.setHoldToShapeDelay(currentDelayMs());
    settings.setHoldToShapeTolerancePx(currentHoldTolerancePx());
    settings.setShapeSensitivity(currentSensitivity());
    settings.setStrokeRecognizerMinSize(currentMinSizePt());
    settings.setShapeRecognizerParams(currentParams());
}

void ShapeSettingsPanel::setHoldControlsSensitive(bool sensitive) {
    gtk_widget_set_sensitive(delayLabelBox, sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(delayScale), sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(delayValueLabel), sensitive);
    gtk_widget_set_sensitive(jitterLabelBox, sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(jitterScale), sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(jitterValueLabel), sensitive);
}

ShapeRecognizerParams ShapeSettingsPanel::currentParams() const {
    ShapeRecognizerParams p;
    for (const auto& row: advRows) {
        p.*(row.member) = gtk_range_get_value(GTK_RANGE(row.scale));
    }
    return xoj::shapes::clampShapeRecognizerParams(p);
}

int ShapeSettingsPanel::currentDelayMs() const {
    return std::clamp(round_cast<int>(gtk_range_get_value(GTK_RANGE(delayScale))), HOLD_TO_SHAPE_DELAY_MIN,
                      HOLD_TO_SHAPE_DELAY_MAX);
}

double ShapeSettingsPanel::currentHoldTolerancePx() const {
    return xoj::shapes::clampParam(gtk_range_get_value(GTK_RANGE(jitterScale)), xoj::shapes::HOLD_TOLERANCE_PX_RANGE);
}

double ShapeSettingsPanel::currentMinSizePt() const {
    return xoj::shapes::clampParam(gtk_range_get_value(GTK_RANGE(minSizeScale)), xoj::shapes::STROKE_MIN_SIZE_RANGE);
}

int ShapeSettingsPanel::currentSensitivity() const {
    return std::clamp(round_cast<int>(gtk_range_get_value(GTK_RANGE(sensitivityScale))),
                      xoj::shapes::SHAPE_SENSITIVITY_MIN, xoj::shapes::SHAPE_SENSITIVITY_MAX);
}

void ShapeSettingsPanel::onSensitivityChanged() {
    if (updatingSliders) {
        return;
    }
    updatingSliders = true;
    const ShapeRecognizerParams mapped = xoj::shapes::paramsForSensitivity(currentSensitivity());
    applyParamsToAdvancedSliders(mapped);
    customThresholds = false;
    updatingSliders = false;

    updateMainValueLabels();
    updateAdvancedValueLabels();
    updateCustomBadge();
}

void ShapeSettingsPanel::onAdvancedParamChanged() {
    if (updatingSliders) {
        return;
    }
    customThresholds = true;
    updateAdvancedValueLabels();
    updateCustomBadge();
}

void ShapeSettingsPanel::onRestoreDefaults() {
    updatingSliders = true;

    gtk_range_set_value(GTK_RANGE(delayScale), 400.0);
    gtk_range_set_value(GTK_RANGE(jitterScale), xoj::shapes::HOLD_TOLERANCE_PX_RANGE.def);
    gtk_range_set_value(GTK_RANGE(sensitivityScale), xoj::shapes::SHAPE_SENSITIVITY_DEFAULT);
    gtk_range_set_value(GTK_RANGE(minSizeScale), xoj::shapes::STROKE_MIN_SIZE_RANGE.def);
    applyParamsToAdvancedSliders(ShapeRecognizerParams{});
    customThresholds = false;

    updatingSliders = false;

    updateMainValueLabels();
    updateAdvancedValueLabels();
    updateCustomBadge();
}

void ShapeSettingsPanel::applyParamsToAdvancedSliders(const ShapeRecognizerParams& params) {
    for (const auto& row: advRows) {
        gtk_range_set_value(GTK_RANGE(row.scale), params.*(row.member));
    }
}

void ShapeSettingsPanel::updateMainValueLabels() {
    gtk_label_set_text(delayValueLabel, (std::to_string(currentDelayMs()) + " ms").c_str());
    gtk_label_set_text(jitterValueLabel,
                       (xoj::shapes::formatFixed(currentHoldTolerancePx(), 0) + " px").c_str());
    gtk_label_set_text(minSizeValueLabel, (xoj::shapes::formatFixed(currentMinSizePt(), 0) + " pt").c_str());

    const int sens = currentSensitivity();
    if (sens == xoj::shapes::SHAPE_SENSITIVITY_DEFAULT) {
        gtk_label_set_text(sensitivityValueLabel, C_("shape sensitivity", "Default"));
    } else {
        const int delta = (sens - xoj::shapes::SHAPE_SENSITIVITY_DEFAULT) * 2;
        const std::string text = (delta < 0 ? "−" : "+") + std::to_string(std::abs(delta)) + "%";
        gtk_label_set_text(sensitivityValueLabel, text.c_str());
    }
}

void ShapeSettingsPanel::updateAdvancedValueLabels() {
    for (const auto& row: advRows) {
        const double val = gtk_range_get_value(GTK_RANGE(row.scale));
        const std::string text = xoj::shapes::formatFixed(val, row.decimals) + row.suffix;
        gtk_label_set_text(row.valueLabel, text.c_str());
    }
}

void ShapeSettingsPanel::updateCustomBadge() {
    if (!customBadgeLabel) {
        return;
    }
    if (customThresholds) {
        gtk_widget_show(GTK_WIDGET(customBadgeLabel));
    } else {
        gtk_widget_hide(GTK_WIDGET(customBadgeLabel));
    }
}

void ShapeSettingsPanel::onPadRealize(GtkWidget* widget, gpointer) {
    GdkWindow* win = gtk_widget_get_window(widget);
    if (!win) {
        return;
    }
    GdkCursor* cursor = gdk_cursor_new_for_display(gdk_window_get_display(win), GDK_CROSSHAIR);
    if (cursor) {  // null when the cursor theme has no crosshair
        gdk_window_set_cursor(win, cursor);
        g_object_unref(cursor);
    }
}

gboolean ShapeSettingsPanel::onPadDraw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    auto* self = static_cast<ShapeSettingsPanel*>(data);
    self->drawPad(cr, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget));
    return false;
}

void ShapeSettingsPanel::drawPad(cairo_t* cr, int width, int height) {
    // White drawing surface
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, 0.0, 0.0, width, height);
    cairo_fill(cr);

    const double zoom = drawing ? activeZoom : (getZoom100 ? getZoom100() : 1.0);

    for (const auto& s: completedStrokes) {
        if (s.recognized) {
            drawStrokePath(cr, s.raw, zoom, 0.208, 0.388, 0.914, 0.18, 2.0);
            drawStrokePath(cr, s.recognized->getPointVector(), zoom, 0.086, 0.639, 0.290, 1.0, 2.4);
        } else {
            drawStrokePath(cr, s.raw, zoom, 0.208, 0.388, 0.914, 1.0, 2.0);
        }
    }

    if (drawing) {
        if (activeStroke.recognized) {
            drawStrokePath(cr, activeStroke.raw, zoom, 0.208, 0.388, 0.914, 0.18, 2.0);
            drawStrokePath(cr, activeStroke.recognized->getPointVector(), zoom, 0.086, 0.639, 0.290, 1.0, 2.4);
        } else {
            drawStrokePath(cr, activeStroke.raw, zoom, 0.208, 0.388, 0.914, 1.0, 2.0);

            const auto& anchor = holdDetector.getAnchor();
            const double ax = anchor.x * zoom;
            const double ay = anchor.y * zoom;
            const double tolPx = holdDetector.getTolerancePx();

            // Tolerance circle around anchor
            cairo_save(cr);
            cairo_set_source_rgba(cr, 0.961, 0.620, 0.043, 0.6);
            cairo_set_line_width(cr, 1.0);
            cairo_arc(cr, ax, ay, tolPx, 0.0, 2.0 * M_PI);
            cairo_stroke(cr);

            // Progress arc while holding
            if (holdDetector.isArmed()) {
                const double elapsedMs = static_cast<double>(g_get_monotonic_time() - holdStartUs) / 1000.0;
                const double delayMs = static_cast<double>(std::max(1, currentDelayMs()));
                const double frac = std::clamp(elapsedMs / delayMs, 0.0, 1.0);
                cairo_set_source_rgb(cr, 0.961, 0.620, 0.043);
                cairo_set_line_width(cr, 3.0);
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                cairo_arc(cr, ax, ay, tolPx + 5.0, -M_PI / 2.0, -M_PI / 2.0 + frac * 2.0 * M_PI);
                cairo_stroke(cr);
            }
            cairo_restore(cr);
        }
    }
}

gboolean ShapeSettingsPanel::onPadButtonPress(GtkWidget*, GdkEventButton* event, gpointer data) {
    if (event->button != 1 || event->type != GDK_BUTTON_PRESS) {
        return false;
    }
    auto* self = static_cast<ShapeSettingsPanel*>(data);
    self->activeZoom = self->getZoom100 ? self->getZoom100() : 1.0;

    const Point p(event->x / self->activeZoom, event->y / self->activeZoom);
    self->activeStroke = PadStroke{{p}, nullptr};
    self->lastRawPos = p;
    self->drawing = true;

    self->holdDetector.setTolerancePx(self->currentHoldTolerancePx());
    self->holdDetector.start({p.x, p.y});
    self->restartPadHoldTimer();

    gtk_widget_queue_draw(self->padArea);
    return true;
}

gboolean ShapeSettingsPanel::onPadMotion(GtkWidget*, GdkEventMotion* event, gpointer data) {
    auto* self = static_cast<ShapeSettingsPanel*>(data);
    if (!self->drawing) {
        return false;
    }

    // Ignore stylus lift motion events with zero pressure (same guard as StrokeHandler)
    gdouble pressure = 1.0;
    if (gdk_event_get_axis(reinterpret_cast<GdkEvent*>(event), GDK_AXIS_PRESSURE, &pressure) && pressure == 0.0) {
        return true;
    }

    if (self->activeStroke.recognized) {
        return true;
    }

    const Point p(event->x / self->activeZoom, event->y / self->activeZoom);
    self->lastRawPos = p;
    self->activeStroke.raw.push_back(p);

    if (self->holdDetector.update({p.x, p.y}, self->activeZoom)) {
        self->restartPadHoldTimer();
    }

    gtk_widget_queue_draw(self->padArea);
    return true;
}

gboolean ShapeSettingsPanel::onPadButtonRelease(GtkWidget*, GdkEventButton* event, gpointer data) {
    if (event->button != 1) {
        return false;
    }
    auto* self = static_cast<ShapeSettingsPanel*>(data);
    if (!self->drawing) {
        return false;
    }
    self->finishPadStroke();
    return true;
}

gboolean ShapeSettingsPanel::onPadGrabBroken(GtkWidget*, GdkEventGrabBroken*, gpointer data) {
    auto* self = static_cast<ShapeSettingsPanel*>(data);
    if (self->drawing) {
        self->finishPadStroke();
    }
    return false;
}

void ShapeSettingsPanel::restartPadHoldTimer() {
    holdStartUs = g_get_monotonic_time();
    holdTimer = g_timeout_add(
            static_cast<guint>(currentDelayMs()),
            xoj::util::wrap_for_once_v<+[](ShapeSettingsPanel* self) {
                self->holdTimer.consume();
                self->tryConvertPadStroke();
            }>,
            this);

    if (!animTimer) {
        animTimer = g_timeout_add(
                16,
                +[](gpointer data) -> gboolean {
                    auto* self = static_cast<ShapeSettingsPanel*>(data);
                    if (!self->drawing || self->activeStroke.recognized || !self->holdDetector.isArmed()) {
                        self->animTimer.consume();
                        return G_SOURCE_REMOVE;
                    }
                    gtk_widget_queue_draw(self->padArea);
                    return G_SOURCE_CONTINUE;
                },
                this);
    }
}

void ShapeSettingsPanel::tryConvertPadStroke() {
    if (!drawing || activeStroke.recognized || !holdDetector.isArmed()) {
        return;
    }

    Stroke probe;
    for (const auto& pt: activeStroke.raw) {
        probe.addPoint(pt);
    }
    probe.addPoint(lastRawPos);

    ShapeRecognizer reco(currentParams());
    ShapeRecognizerDiagnostics diag;
    auto recognized = reco.recognizePatterns(&probe, currentMinSizePt(), &diag);

    updateDiagnostics(diag);

    if (!recognized) {
        holdDetector.disarm();
        animTimer.cancel();
        gtk_widget_queue_draw(padArea);
        return;
    }

    activeStroke.recognized = std::move(recognized);
    animTimer.cancel();
    gtk_widget_queue_draw(padArea);
}

void ShapeSettingsPanel::finishPadStroke() {
    holdTimer.cancel();
    animTimer.cancel();
    drawing = false;
    if (!activeStroke.raw.empty()) {
        completedStrokes.push_back(std::move(activeStroke));
    }
    activeStroke = {};
    gtk_widget_queue_draw(padArea);
}

void ShapeSettingsPanel::clearPad() {
    holdTimer.cancel();
    animTimer.cancel();
    drawing = false;
    activeStroke = {};
    completedStrokes.clear();
    resetDiagnostics(_("Pad cleared."));
    gtk_widget_queue_draw(padArea);
}

void ShapeSettingsPanel::updateDiagnostics(const ShapeRecognizerDiagnostics& diag) {
    removeCssClass(GTK_WIDGET(diagResultLabel), "mx-shape-hint");
    removeCssClass(GTK_WIDGET(diagResultLabel), "mx-diag-ok");
    removeCssClass(GTK_WIDGET(diagResultLabel), "mx-diag-no");

    if (diag.shape != RecognizedShapeKind::None) {
        addCssClass(GTK_WIDGET(diagResultLabel), "mx-diag-ok");
        gtk_label_set_text(diagResultLabel, FC(_F("Recognized: {1}") % shapeKindLabel(diag.shape)));
    } else {
        addCssClass(GTK_WIDGET(diagResultLabel), "mx-diag-no");
        gtk_label_set_text(diagResultLabel, _("No shape"));
    }

    if (diag.reasons.empty()) {
        gtk_label_set_text(diagReasonsLabel, "");
        gtk_widget_hide(GTK_WIDGET(diagReasonsLabel));
    } else {
        std::string text;
        for (size_t i = 0; i < diag.reasons.size(); i++) {
            if (i > 0) {
                text += "\n";
            }
            text += "• ";
            text += diag.reasons[i];
        }
        gtk_label_set_text(diagReasonsLabel, text.c_str());
        gtk_widget_show(GTK_WIDGET(diagReasonsLabel));
    }
}

void ShapeSettingsPanel::resetDiagnostics(const char* message) {
    removeCssClass(GTK_WIDGET(diagResultLabel), "mx-diag-ok");
    removeCssClass(GTK_WIDGET(diagResultLabel), "mx-diag-no");
    addCssClass(GTK_WIDGET(diagResultLabel), "mx-shape-hint");
    gtk_label_set_text(diagResultLabel, message);
    gtk_label_set_text(diagReasonsLabel, "");
    gtk_widget_hide(GTK_WIDGET(diagReasonsLabel));
}
