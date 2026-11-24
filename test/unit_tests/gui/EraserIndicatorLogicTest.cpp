/*
 * MoreXournal
 *
 * Unit tests for eraser indicator geometry, palette, visibility and pen dot sizing.
 *
 * @license GNU GPLv2 or later
 */

#include <array>

#include <gtest/gtest.h>

#include "control/ToolHandler.h"
#include "gui/EraserIndicatorLogic.h"
#include "model/Stroke.h"
#include "model/eraser/PaddedBox.h"
#include "util/Recolor.h"

using namespace xoj::cursor;

TEST(EraserIndicatorLogicTest, testStandardAndDeleteStrokeMatchPaddedBoxEraseArea) {
    ToolHandler th(nullptr, nullptr);
    const double* eraserThicknesses = th.getToolThickness(TOOL_ERASER);

    constexpr std::array<ToolSize, 5> sizes = {TOOL_SIZE_VERY_FINE, TOOL_SIZE_FINE, TOOL_SIZE_MEDIUM, TOOL_SIZE_THICK,
                                               TOOL_SIZE_VERY_THICK};
    constexpr std::array<double, 5> zooms = {0.5, 1.0, 4.0 / 3.0, 2.0, 4.0};

    for (ToolSize sz: sizes) {
        const double thickness = eraserThicknesses[sz];
        // EraseHandler::erase uses halfEraserSize = handler->getThickness() and PaddedBox{{x, y}, halfEraserSize, ...}
        const PaddedBox box{{100.0, 100.0}, thickness, thickness};
        const auto innerRect = box.getInnerRectangle();

        for (double zoom: zooms) {
            const double expectedSidePx = innerRect.width * zoom;
            ASSERT_DOUBLE_EQ(innerRect.width, innerRect.height);

            for (EraserType mode: {ERASER_TYPE_DEFAULT, ERASER_TYPE_DELETE_STROKE}) {
                const auto hoverGeom = computeEraserIndicatorGeometry(mode, thickness, zoom, false);
                EXPECT_EQ(hoverGeom.shape, EraserIndicatorShape::RoundedSquare);
                EXPECT_DOUBLE_EQ(hoverGeom.trueSizePx, expectedSidePx);
                EXPECT_DOUBLE_EQ(hoverGeom.strokeWidthPx, STROKE_WIDTH_HOVER_PX);

                const auto pressedGeom = computeEraserIndicatorGeometry(mode, thickness, zoom, true);
                EXPECT_EQ(pressedGeom.shape, EraserIndicatorShape::RoundedSquare);
                EXPECT_DOUBLE_EQ(pressedGeom.trueSizePx, expectedSidePx);
                EXPECT_DOUBLE_EQ(pressedGeom.strokeWidthPx, STROKE_WIDTH_PRESSED_PX);

                if (expectedSidePx >= 8.0) {
                    EXPECT_DOUBLE_EQ(hoverGeom.cornerRadiusPx, ROUNDED_SQUARE_RADIUS_PX);
                } else {
                    EXPECT_LE(hoverGeom.cornerRadiusPx, expectedSidePx * 0.25 + 1e-12);
                }
            }
        }
    }
}

TEST(EraserIndicatorLogicTest, testWhiteoutMatchesStrokeWidthAndIsCircle) {
    ToolHandler th(nullptr, nullptr);
    const double* eraserThicknesses = th.getToolThickness(TOOL_ERASER);

    constexpr std::array<ToolSize, 5> sizes = {TOOL_SIZE_VERY_FINE, TOOL_SIZE_FINE, TOOL_SIZE_MEDIUM, TOOL_SIZE_THICK,
                                               TOOL_SIZE_VERY_THICK};
    constexpr std::array<double, 4> zooms = {0.5, 1.0, 4.0 / 3.0, 2.5};

    for (ToolSize sz: sizes) {
        const double thickness = eraserThicknesses[sz];
        // InputHandler::createStroke sets s->setWidth(h->getThickness()) with StrokeCapStyle::ROUND
        Stroke whiteoutStroke;
        whiteoutStroke.setToolType(StrokeTool::ERASER);
        whiteoutStroke.setWidth(thickness);
        ASSERT_EQ(whiteoutStroke.getStrokeCapStyle(), StrokeCapStyle::ROUND);

        for (double zoom: zooms) {
            const auto geom = computeEraserIndicatorGeometry(ERASER_TYPE_WHITEOUT, thickness, zoom, false);
            const auto stdGeom = computeEraserIndicatorGeometry(ERASER_TYPE_DEFAULT, thickness, zoom, false);

            EXPECT_EQ(geom.shape, EraserIndicatorShape::Circle);
            EXPECT_DOUBLE_EQ(geom.trueSizePx, whiteoutStroke.getWidth() * zoom);
            EXPECT_DOUBLE_EQ(geom.trueSizePx * 2.0, stdGeom.trueSizePx);
            EXPECT_DOUBLE_EQ(geom.cornerRadiusPx, 0.0);
        }
    }
}

TEST(EraserIndicatorLogicTest, testCornerBracketsBelow12PxThreshold) {
    // At 96 DPI (zoom = 4/3):
    // Standard: Very Fine (2.67 px) and Fine (7.55 px) are < 12 px; Medium (22.67 px) is >= 12 px.
    constexpr double zoom100 = 4.0 / 3.0;
    const auto vfStd = computeEraserIndicatorGeometry(ERASER_TYPE_DEFAULT, 1.0, zoom100, false);
    EXPECT_TRUE(vfStd.showCornerBrackets);
    EXPECT_DOUBLE_EQ(vfStd.bracketBoxSizePx, 12.0);
    EXPECT_DOUBLE_EQ(vfStd.bracketLegPx, 4.0);
    EXPECT_GE(vfStd.paintExtentHalfPx, 6.0 + vfStd.strokeWidthPx * 0.5 + HALO_OUTER_PX);

    const auto fineStd = computeEraserIndicatorGeometry(ERASER_TYPE_DEFAULT, 2.83, zoom100, false);
    EXPECT_TRUE(fineStd.showCornerBrackets);
    EXPECT_DOUBLE_EQ(fineStd.bracketBoxSizePx, 12.0);
    EXPECT_DOUBLE_EQ(fineStd.bracketLegPx, 4.0);

    const auto medStd = computeEraserIndicatorGeometry(ERASER_TYPE_DEFAULT, 8.50, zoom100, false);
    EXPECT_FALSE(medStd.showCornerBrackets);
    EXPECT_DOUBLE_EQ(medStd.bracketBoxSizePx, 0.0);
    EXPECT_DOUBLE_EQ(medStd.bracketLegPx, 0.0);

    // Exact boundary at 12.0 px
    const auto justUnder = computeEraserIndicatorGeometry(ERASER_TYPE_DEFAULT, 5.995, 1.0, false);
    EXPECT_TRUE(justUnder.showCornerBrackets);
    const auto exact12 = computeEraserIndicatorGeometry(ERASER_TYPE_DEFAULT, 6.0, 1.0, false);
    EXPECT_FALSE(exact12.showCornerBrackets);

    // Whiteout: Medium (8.5 * 4/3 = 11.33 px) is < 12 px; Thick (12 * 4/3 = 16 px) is >= 12 px.
    const auto medWo = computeEraserIndicatorGeometry(ERASER_TYPE_WHITEOUT, 8.50, zoom100, false);
    EXPECT_TRUE(medWo.showCornerBrackets);
    const auto thickWo = computeEraserIndicatorGeometry(ERASER_TYPE_WHITEOUT, 12.0, zoom100, false);
    EXPECT_FALSE(thickWo.showCornerBrackets);
}

TEST(EraserIndicatorLogicTest, testNoUpperSizeCapOnHighZoom) {
    // Very thick (18 pt) at 400% zoom -> 18 * 2 * 4 * (4/3) = 192 px logical, no cap
    const auto geom = computeEraserIndicatorGeometry(ERASER_TYPE_DEFAULT, 18.0, 4.0 * 4.0 / 3.0, false);
    EXPECT_DOUBLE_EQ(geom.trueSizePx, 192.0);
    EXPECT_GE(geom.paintExtentHalfPx, 96.0 + geom.strokeWidthPx * 0.5 + HALO_OUTER_PX);
}

TEST(EraserIndicatorLogicTest, testLightAndDarkRecoloredPalettes) {
    EXPECT_FALSE(isDarkColor(Colors::white));
    EXPECT_TRUE(isDarkColor(Colors::black));

    // Inverted dark recolor: white page maps to dark background (#1e2027)
    const Recolor darkRecolor(ColorU8{30, 32, 39}, ColorU8{230, 232, 240});
    const Color recoloredPaper = Color(darkRecolor.convertColor(Colors::white));
    EXPECT_TRUE(isDarkColor(recoloredPaper));

    const auto lightPal = computeEraserIndicatorPalette(false, Colors::white);
    EXPECT_NEAR(lightPal.stroke.r, 28.0 / 255.0, 1e-6);
    EXPECT_NEAR(lightPal.stroke.g, 31.0 / 255.0, 1e-6);
    EXPECT_NEAR(lightPal.stroke.b, 38.0 / 255.0, 1e-6);
    EXPECT_DOUBLE_EQ(lightPal.stroke.a, 0.85);
    EXPECT_DOUBLE_EQ(lightPal.halo.r, 1.0);
    EXPECT_DOUBLE_EQ(lightPal.halo.a, 0.90);
    EXPECT_DOUBLE_EQ(lightPal.pressedFill.a, 0.08);
    EXPECT_DOUBLE_EQ(lightPal.whiteoutFill.a, 0.90);

    const auto darkPal = computeEraserIndicatorPalette(true, recoloredPaper);
    EXPECT_NEAR(darkPal.stroke.r, 236.0 / 255.0, 1e-6);
    EXPECT_NEAR(darkPal.stroke.g, 238.0 / 255.0, 1e-6);
    EXPECT_NEAR(darkPal.stroke.b, 244.0 / 255.0, 1e-6);
    EXPECT_DOUBLE_EQ(darkPal.halo.r, 0.0);
    EXPECT_DOUBLE_EQ(darkPal.halo.a, 0.60);
    EXPECT_NEAR(darkPal.whiteoutFill.r, 30.0 / 255.0, 1e-2);
    EXPECT_DOUBLE_EQ(darkPal.whiteoutFill.a, 0.90);
}

TEST(EraserIndicatorLogicTest, testPdfPageAndColoredPaperPalette) {
    RecolorParameters noRecolor{};
    noRecolor.recolorizeMainView = false;

    // PDF page with dark underlying backgroundColor is rendered white: indicator must use light-paper palette
    const Color pdfPaper = displayedPaperColor(/*isPdfPage=*/true, Colors::black, noRecolor);
    EXPECT_EQ(pdfPaper, Colors::white);
    const auto pdfPal = computeEraserIndicatorPaletteForPage(/*isPdfPage=*/true, Colors::black, noRecolor);
    EXPECT_FALSE(pdfPal.darkPage);
    EXPECT_DOUBLE_EQ(pdfPal.whiteoutFill.r, 1.0);
    EXPECT_DOUBLE_EQ(pdfPal.whiteoutFill.g, 1.0);
    EXPECT_DOUBLE_EQ(pdfPal.whiteoutFill.b, 1.0);

    // PDF page with dark main-view recolor maps white paper to dark recolored paper
    RecolorParameters darkRecolor{};
    darkRecolor.recolorizeMainView = true;
    darkRecolor.recolor = Recolor(ColorU8{30, 32, 39}, ColorU8{230, 232, 240});
    const auto darkPdfPal = computeEraserIndicatorPaletteForPage(/*isPdfPage=*/true, Colors::black, darkRecolor);
    EXPECT_TRUE(darkPdfPal.darkPage);
    EXPECT_NEAR(darkPdfPal.whiteoutFill.r, 30.0 / 255.0, 1e-2);

    // Non-PDF colored paper picks the palette from the paper, but whiteout strokes are painted white,
    // so the whiteout preview stays white
    const Color charcoal{0xff20242cU};
    const auto charcoalPal = computeEraserIndicatorPaletteForPage(/*isPdfPage=*/false, charcoal, noRecolor);
    EXPECT_TRUE(charcoalPal.darkPage);
    EXPECT_DOUBLE_EQ(charcoalPal.whiteoutFill.r, 1.0);
    EXPECT_DOUBLE_EQ(charcoalPal.whiteoutFill.g, 1.0);
    EXPECT_DOUBLE_EQ(charcoalPal.whiteoutFill.b, 1.0);

    const Color cream = Colors::xopp_antiquewhite;
    const auto creamPal = computeEraserIndicatorPaletteForPage(/*isPdfPage=*/false, cream, noRecolor);
    EXPECT_FALSE(creamPal.darkPage);
    EXPECT_DOUBLE_EQ(creamPal.whiteoutFill.r, 1.0);
    EXPECT_DOUBLE_EQ(creamPal.whiteoutFill.g, 1.0);
    EXPECT_DOUBLE_EQ(creamPal.whiteoutFill.b, 1.0);
}

TEST(EraserIndicatorLogicTest, testVisibilityAcrossDevicesAndStates) {
    EraserOverlayInput base{};
    base.insidePage = true;
    base.pointerInWidget = true;
    base.toolType = TOOL_ERASER;

    // Mouse: visible on hover and press
    base.inputDevice = INPUT_DEVICE_MOUSE;
    base.pressed = false;
    EXPECT_TRUE(shouldShowEraserOverlay(base));
    base.pressed = true;
    EXPECT_TRUE(shouldShowEraserOverlay(base));

    // Touchscreen (finger): visible ONLY while in contact
    base.inputDevice = INPUT_DEVICE_TOUCHSCREEN;
    base.pressed = false;
    EXPECT_FALSE(shouldShowEraserOverlay(base));
    base.pressed = true;
    EXPECT_TRUE(shouldShowEraserOverlay(base));

    // Stylus eraser tip (GDK_SOURCE_ERASER) and stylus pen tip follow EraserVisibility
    for (InputDeviceClass dev: {INPUT_DEVICE_PEN, INPUT_DEVICE_ERASER}) {
        base.inputDevice = dev;
        base.eraserVisibility = ERASER_VISIBILITY_ALWAYS;
        base.pressed = false;
        EXPECT_TRUE(shouldShowEraserOverlay(base));
        base.pressed = true;
        EXPECT_TRUE(shouldShowEraserOverlay(base));

        base.eraserVisibility = ERASER_VISIBILITY_HOVER;
        base.pressed = false;
        EXPECT_TRUE(shouldShowEraserOverlay(base));
        base.pressed = true;
        EXPECT_FALSE(shouldShowEraserOverlay(base));

        base.eraserVisibility = ERASER_VISIBILITY_TOUCH;
        base.pressed = false;
        EXPECT_FALSE(shouldShowEraserOverlay(base));
        base.pressed = true;
        EXPECT_TRUE(shouldShowEraserOverlay(base));

        base.eraserVisibility = ERASER_VISIBILITY_NEVER;
        base.pressed = false;
        EXPECT_FALSE(shouldShowEraserOverlay(base));
        base.pressed = true;
        EXPECT_FALSE(shouldShowEraserOverlay(base));
    }

    // Hidden when busy, outside page, outside widget, hovering selection, or using another tool
    base.inputDevice = INPUT_DEVICE_MOUSE;
    base.pressed = false;
    base.eraserVisibility = ERASER_VISIBILITY_ALWAYS;
    {
        auto x = base;
        x.busy = true;
        EXPECT_FALSE(shouldShowEraserOverlay(x));
    }
    {
        auto x = base;
        x.insidePage = false;
        EXPECT_FALSE(shouldShowEraserOverlay(x));
    }
    {
        auto x = base;
        x.pointerInWidget = false;
        EXPECT_FALSE(shouldShowEraserOverlay(x));
    }
    {
        auto x = base;
        x.selectionType = CURSOR_SELECTION_MOVE;
        EXPECT_FALSE(shouldShowEraserOverlay(x));
    }
    {
        auto x = base;
        x.toolType = TOOL_PEN;
        EXPECT_FALSE(shouldShowEraserOverlay(x));
    }
}

TEST(EraserIndicatorLogicTest, testPenDotMinimum4PxAndMax90Px) {
    // Medium pen 1.41 pt at 100% (zoom = 4/3) = 1.88 px -> clamped up to 4.0 px
    EXPECT_DOUBLE_EQ(computePenDotDiameterPx(1.41, 4.0 / 3.0), 4.0);
    EXPECT_DOUBLE_EQ(computePenDotDiameterPx(0.42, 0.5), 4.0);

    // Thick highlighter 19.84 pt at 100% (zoom = 4/3) = 26.453... px -> unchanged
    EXPECT_DOUBLE_EQ(computePenDotDiameterPx(19.84, 4.0 / 3.0), 19.84 * 4.0 / 3.0);

    // Huge highlighter at high zoom -> capped at 90 px
    EXPECT_DOUBLE_EQ(computePenDotDiameterPx(30.0, 5.0), 90.0);
}
