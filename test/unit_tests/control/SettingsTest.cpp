/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <fstream>  // for ofstream

#include <gtest/gtest.h>

#include "control/XournalMain.h"
#include "control/settings/ButtonConfig.h"
#include "control/settings/Settings.h"

namespace {
struct ScopedTempSettingsFile {
    fs::path path;

    explicit ScopedTempSettingsFile(const char* name): path(fs::temp_directory_path() / name) {
        std::error_code ec;
        fs::remove(path, ec);
    }

    ~ScopedTempSettingsFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};
}  // namespace

TEST(SettingsTest, testLoadDoesNotThrowForNonExistingFilePath) {
    ScopedTempSettingsFile tmp{"xournalpp-test-units_Settings_nonExisting.xml"};
    Settings settings{tmp.path};
    EXPECT_NO_THROW(settings.load());
}

// Rudimentary test for Settings save/load - very crude
TEST(SettingsTest, testReadWrite) {
    auto saveReloadTest = [&](const fs::path& dir) {
        std::cout << "Test saving in " << dir << std::endl;
        const fs::path outPath = dir / "xournalpp-test-units_Settings_testReadWrite.xml";
        if (fs::exists(outPath)) {
            std::cout << "Removing file (already exist): " << dir << std::endl;
            fs::remove(outPath);
        };

        Settings settings(outPath);
        settings.transactionStart();
        settings.setAudioDisabled(true);               // bool
        settings.setDefaultSaveName(u8"foo/bar€_%H");  // u8string
        settings.setPreferredLocale("es");             // string
        PageTemplateSettings tp;
        tp.parse("xoj/"
                 "template\ncopyLastPageSettings=false\ncopyLastPageSize=true\nsize=5.123x8.764\nbackgroundType="
                 "cµßtom\nbackgroundTypeConfig=m1=3,®ændomString=↓↓↓\nbackgroundColor=#abcdef\n");
        settings.setPageTemplateSettings(tp);                          // string
        settings.setDisplayDpi(123);                                   // int
        settings.setStabilizerDrag(3.1415);                            // double
        settings.setBackgroundColor(Color(123, 45, 67));               // Color
        settings.setColorPaletteSetting("foo/bar€_palette");           // path
        settings.setEraserVisibility(ERASER_VISIBILITY_HOVER);         // enum
        settings.setFont(XojFont{"myfontname italic 34"});             // Font
        settings.latexSettings.editorFont = XojFont{"myfonttest 52"};  // Font
        settings.setPreloadPagesAfter(145);                            // unsigned int
        settings.transactionEnd();                                     // calls save()

        Settings loaded(outPath);
        loaded.load();

        // For each type, we test one that has been changed and one that should be default
        EXPECT_EQ(settings.isAudioDisabled(), loaded.isAudioDisabled());                                    // bool
        EXPECT_EQ(settings.isAutoloadPdfXoj(), loaded.isAutoloadPdfXoj());                                  // bool
        EXPECT_EQ(settings.getDefaultSaveName(), loaded.getDefaultSaveName());                              // u8string
        EXPECT_EQ(settings.getDefaultPdfExportName(), loaded.getDefaultPdfExportName());                    // u8string
        EXPECT_EQ(settings.getPreferredLocale(), loaded.getPreferredLocale());                              // string
        EXPECT_EQ(settings.getPageTemplateSettings(), loaded.getPageTemplateSettings());                    // string
        EXPECT_EQ(settings.getDisplayDpi(), loaded.getDisplayDpi());                                        // int
        EXPECT_EQ(settings.getAddHorizontalSpaceAmountLeft(), loaded.getAddHorizontalSpaceAmountLeft());    // int
        EXPECT_EQ(settings.getStabilizerDrag(), loaded.getStabilizerDrag());                                // double
        EXPECT_EQ(settings.getCursorHighlightBorderWidth(), loaded.getCursorHighlightBorderWidth());        // double
        EXPECT_EQ(settings.getBackgroundColor(), loaded.getBackgroundColor());                              // Color
        EXPECT_EQ(settings.getActiveSelectionColor(), loaded.getActiveSelectionColor());                    // Color
        EXPECT_EQ(settings.getColorPaletteSetting(), loaded.getColorPaletteSetting());                      // path
        EXPECT_EQ(settings.getLastOpenPath(), loaded.getLastOpenPath());                                    // path
        EXPECT_EQ(settings.getEraserVisibility(), loaded.getEraserVisibility());                            // enum
        EXPECT_EQ(settings.getActiveViewMode(), loaded.getActiveViewMode());                                // enum
        EXPECT_EQ(settings.getFont().getName(), loaded.getFont().getName());                                // Font
        EXPECT_EQ(settings.getFont().getSize(), loaded.getFont().getSize());                                // Font
        EXPECT_EQ(settings.latexSettings.editorFont.getName(), loaded.latexSettings.editorFont.getName());  // Font
        EXPECT_EQ(settings.latexSettings.editorFont.getSize(), loaded.latexSettings.editorFont.getSize());  // Font
        EXPECT_EQ(settings.getPreloadPagesAfter(), loaded.getPreloadPagesAfter());    // unsigned int
        EXPECT_EQ(settings.getPreloadPagesBefore(), loaded.getPreloadPagesBefore());  // unsigned int

        fs::remove(outPath);
    };
    saveReloadTest(fs::temp_directory_path());
}

TEST(SettingsTest, testHoldToShapeDefaults) {
    ScopedTempSettingsFile tmp{"xournalpp-test-units_Settings_holdDefaults.xml"};
    Settings settings{tmp.path};
    EXPECT_TRUE(settings.isHoldToShapeEnabled());
    EXPECT_EQ(settings.getHoldToShapeDelay(), 400);
    EXPECT_DOUBLE_EQ(settings.getHoldToShapeTolerancePx(), 8.0);
    EXPECT_DOUBLE_EQ(settings.getStrokeRecognizerMinSize(), 40.0);
    EXPECT_EQ(settings.getShapeSensitivity(), 50);
    EXPECT_EQ(settings.getShapeRecognizerParams(), ShapeRecognizerParams{});
}

TEST(SettingsTest, testHoldToShapeDelayIsClamped) {
    ScopedTempSettingsFile tmp{"xournalpp-test-units_Settings_holdDelayClamp.xml"};
    Settings settings{tmp.path};
    settings.transactionStart();  // do not write the settings file
    settings.setHoldToShapeDelay(10);
    EXPECT_EQ(settings.getHoldToShapeDelay(), HOLD_TO_SHAPE_DELAY_MIN);
    settings.setHoldToShapeDelay(100000);
    EXPECT_EQ(settings.getHoldToShapeDelay(), HOLD_TO_SHAPE_DELAY_MAX);
    settings.setHoldToShapeDelay(750);
    EXPECT_EQ(settings.getHoldToShapeDelay(), 750);
}

TEST(SettingsTest, testShapeSensitivityReadWriteAndClamp) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_Settings_testShapeSensitivity.xml";
    fs::remove(outPath);

    ShapeRecognizerParams custom;
    custom.lineMaxDet = 0.031;
    custom.segmentMaxDet = 0.072;
    custom.circleMinDet = 0.89;
    custom.circleMaxScore = 0.16;
    custom.rectangleAngleToleranceDeg = 22.0;
    custom.rectangleLinearTolerance = 0.31;
    custom.triangleLinearTolerance = 0.42;
    custom.slantToleranceDeg = 9.0;

    Settings settings(outPath);
    settings.transactionStart();
    settings.setHoldToShapeTolerancePx(14.0);
    settings.setStrokeRecognizerMinSize(75.0);
    settings.setShapeSensitivity(73);
    settings.setShapeRecognizerParams(custom);
    settings.transactionEnd();

    Settings loaded(outPath);
    ASSERT_TRUE(loaded.load());
    EXPECT_DOUBLE_EQ(loaded.getHoldToShapeTolerancePx(), 14.0);
    EXPECT_DOUBLE_EQ(loaded.getStrokeRecognizerMinSize(), 75.0);
    EXPECT_EQ(loaded.getShapeSensitivity(), 73);
    EXPECT_NEAR(loaded.getShapeRecognizerParams().lineMaxDet, custom.lineMaxDet, 1e-6);
    EXPECT_NEAR(loaded.getShapeRecognizerParams().segmentMaxDet, custom.segmentMaxDet, 1e-6);
    EXPECT_NEAR(loaded.getShapeRecognizerParams().circleMinDet, custom.circleMinDet, 1e-6);
    EXPECT_NEAR(loaded.getShapeRecognizerParams().circleMaxScore, custom.circleMaxScore, 1e-6);
    EXPECT_NEAR(loaded.getShapeRecognizerParams().rectangleAngleToleranceDeg, custom.rectangleAngleToleranceDeg, 1e-6);
    EXPECT_NEAR(loaded.getShapeRecognizerParams().rectangleLinearTolerance, custom.rectangleLinearTolerance, 1e-6);
    EXPECT_NEAR(loaded.getShapeRecognizerParams().triangleLinearTolerance, custom.triangleLinearTolerance, 1e-6);
    EXPECT_NEAR(loaded.getShapeRecognizerParams().slantToleranceDeg, custom.slantToleranceDeg, 1e-6);

    // Out-of-range values in XML are clamped on load
    {
        std::ofstream bad(outPath);
        bad << R"(<?xml version="1.0"?>
<settings>
  <property name="uiLayoutVersion" value="1"/>
  <property name="holdToShapeDelay" value="5"/>
  <property name="holdToShapeTolerancePx" value="99"/>
  <property name="strokeRecognizerMinSize" value="2"/>
  <property name="shapeSensitivity" value="250"/>
  <property name="shapeLineMaxDet" value="0.0001"/>
  <property name="shapeSegmentMaxDet" value="5.0"/>
  <property name="shapeCircleMinDet" value="0.5"/>
  <property name="shapeCircleMaxScore" value="0.9"/>
  <property name="shapeRectangleAngleTolerance" value="1"/>
  <property name="shapeRectangleLinearTolerance" value="2.0"/>
  <property name="shapeTriangleLinearTolerance" value="0.01"/>
  <property name="shapeSlantTolerance" value="45"/>
</settings>
)";
    }

    Settings clamped(outPath);
    ASSERT_TRUE(clamped.load());
    EXPECT_EQ(clamped.getHoldToShapeDelay(), 100);
    EXPECT_DOUBLE_EQ(clamped.getHoldToShapeTolerancePx(), 20.0);
    EXPECT_DOUBLE_EQ(clamped.getStrokeRecognizerMinSize(), 10.0);
    EXPECT_EQ(clamped.getShapeSensitivity(), 100);
    EXPECT_DOUBLE_EQ(clamped.getShapeRecognizerParams().lineMaxDet, 0.005);
    EXPECT_DOUBLE_EQ(clamped.getShapeRecognizerParams().segmentMaxDet, 0.10);
    EXPECT_DOUBLE_EQ(clamped.getShapeRecognizerParams().circleMinDet, 0.85);
    EXPECT_DOUBLE_EQ(clamped.getShapeRecognizerParams().circleMaxScore, 0.20);
    EXPECT_DOUBLE_EQ(clamped.getShapeRecognizerParams().rectangleAngleToleranceDeg, 6.0);
    EXPECT_DOUBLE_EQ(clamped.getShapeRecognizerParams().rectangleLinearTolerance, 0.40);
    EXPECT_DOUBLE_EQ(clamped.getShapeRecognizerParams().triangleLinearTolerance, 0.12);
    EXPECT_DOUBLE_EQ(clamped.getShapeRecognizerParams().slantToleranceDeg, 15.0);

    // Older settings file without the new keys gets defaults
    {
        std::ofstream older(outPath);
        older << R"(<?xml version="1.0"?>
<settings>
  <property name="uiLayoutVersion" value="1"/>
  <property name="holdToShapeDelay" value="600"/>
</settings>
)";
    }

    Settings fromOlder(outPath);
    ASSERT_TRUE(fromOlder.load());
    EXPECT_EQ(fromOlder.getHoldToShapeDelay(), 600);
    EXPECT_DOUBLE_EQ(fromOlder.getHoldToShapeTolerancePx(), 8.0);
    EXPECT_DOUBLE_EQ(fromOlder.getStrokeRecognizerMinSize(), 40.0);
    EXPECT_EQ(fromOlder.getShapeSensitivity(), 50);
    EXPECT_EQ(fromOlder.getShapeRecognizerParams(), ShapeRecognizerParams{});

    fs::remove(outPath);
}

TEST(SettingsTest, testHoldToShapeReadWrite) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_Settings_testHoldToShape.xml";
    fs::remove(outPath);

    Settings settings(outPath);
    settings.transactionStart();
    settings.setHoldToShapeEnabled(false);
    settings.setHoldToShapeDelay(1250);
    settings.transactionEnd();  // calls save()

    Settings loaded(outPath);
    loaded.load();
    EXPECT_FALSE(loaded.isHoldToShapeEnabled());
    EXPECT_EQ(loaded.getHoldToShapeDelay(), 1250);

    fs::remove(outPath);
}

TEST(SettingsTest, testTopBarSettingsReadWrite) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_Settings_testTopBar.xml";
    fs::remove(outPath);

    Settings settings(outPath);
    settings.transactionStart();
    settings.setUseClientSideDecorations(false);
    settings.setLastShapeType(DRAWING_TYPE_ELLIPSE);
    settings.setMenubarVisible(true);
    settings.transactionEnd();  // calls save()

    Settings loaded(outPath);
    ASSERT_TRUE(loaded.load());
    EXPECT_EQ(loaded.getUiLayoutVersion(), UI_LAYOUT_VERSION);
    EXPECT_FALSE(loaded.isUseClientSideDecorations());
    EXPECT_EQ(loaded.getLastShapeType(), DRAWING_TYPE_ELLIPSE);
    // A current file is not migrated: the user's choice to always show the menu bar stays
    EXPECT_TRUE(loaded.isMenubarVisible());
    EXPECT_FALSE(loaded.consumePendingToolbarMigration());

    fs::remove(outPath);
}

TEST(SettingsTest, testTouchModeReadWrite) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_Settings_testTouchMode.xml";
    fs::remove(outPath);

    Settings settings(outPath);
    EXPECT_EQ(settings.getTouchMode(), TouchMode::AUTO);
    settings.setTouchMode(TouchMode::ON);

    Settings loaded(outPath);
    ASSERT_TRUE(loaded.load());
    EXPECT_EQ(loaded.getTouchMode(), TouchMode::ON);

    loaded.setTouchMode(TouchMode::OFF);
    Settings reloaded(outPath);
    ASSERT_TRUE(reloaded.load());
    EXPECT_EQ(reloaded.getTouchMode(), TouchMode::OFF);

    fs::remove(outPath);
}

TEST(SettingsTest, testLastShapeTypeOnlyAcceptsShapes) {
    ScopedTempSettingsFile tmp{"xournalpp-test-units_Settings_lastShapeType.xml"};
    Settings settings{tmp.path};
    settings.transactionStart();  // do not write the settings file
    EXPECT_EQ(settings.getLastShapeType(), DRAWING_TYPE_RECTANGLE);
    settings.setLastShapeType(DRAWING_TYPE_SHAPE_RECOGNIZER);
    EXPECT_EQ(settings.getLastShapeType(), DRAWING_TYPE_RECTANGLE);
    settings.setLastShapeType(DRAWING_TYPE_SPLINE);
    EXPECT_EQ(settings.getLastShapeType(), DRAWING_TYPE_SPLINE);
}

TEST(SettingsTest, testNewSettingsFileUsesTopBarDefaults) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_Settings_testFresh.xml";
    fs::remove(outPath);

    Settings settings(outPath);
    ASSERT_TRUE(settings.load());  // writes the defaults, then reads them back
    EXPECT_EQ(settings.getUiLayoutVersion(), UI_LAYOUT_VERSION);
    EXPECT_FALSE(settings.isMenubarVisible());
    EXPECT_FALSE(settings.isSidebarVisible());
    EXPECT_TRUE(settings.isUseClientSideDecorations());
    EXPECT_EQ(settings.getSelectedToolbar(), "MoreXournal");
    const ViewMode& defaultMode = settings.getViewModes().at(PresetViewModeIds::VIEW_MODE_DEFAULT);
    EXPECT_FALSE(defaultMode.showMenubar);
    EXPECT_FALSE(defaultMode.showSidebar);
    EXPECT_TRUE(defaultMode.showToolbar);
    EXPECT_FALSE(settings.consumePendingToolbarMigration());

    fs::remove(outPath);
}

TEST(SettingsTest, testLegacySettingsAreMigratedOnce) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_Settings_testLegacy.xml";
    {
        // Settings written before the top bar existed: no uiLayoutVersion
        std::ofstream legacy(outPath);
        legacy << R"(<?xml version="1.0"?>
<settings>
  <property name="menubarVisible" value="true"/>
  <property name="showSidebar" value="true"/>
  <property name="selectedToolbar" value="Portrait"/>
  <property name="defaultViewModeAttributes" value="showMenubar,showToolbar,showSidebar"/>
  <property name="sidebarWidth" value="222"/>
  <data name="buttonConfig">
    <data name="right">
      <attribute name="tool" type="string" value="hand"/>
    </data>
  </data>
  <data name="deviceClasses">
    <data name="Test Tablet">
      <attribute name="deviceClass" type="int" value="2"/>
      <attribute name="deviceSource" type="int" value="2"/>
    </data>
  </data>
</settings>
)";
    }

    Settings settings(outPath);
    ASSERT_TRUE(settings.load());
    EXPECT_EQ(settings.getUiLayoutVersion(), UI_LAYOUT_VERSION);
    EXPECT_FALSE(settings.isMenubarVisible());
    EXPECT_FALSE(settings.isSidebarVisible());
    const ViewMode& defaultMode = settings.getViewModes().at(PresetViewModeIds::VIEW_MODE_DEFAULT);
    EXPECT_FALSE(defaultMode.showMenubar);
    EXPECT_FALSE(defaultMode.showSidebar);
    EXPECT_TRUE(defaultMode.showToolbar);
    // Leaving fullscreen loads the default view mode: the menu bar must stay hidden
    ASSERT_TRUE(settings.loadViewMode(PresetViewModeIds::VIEW_MODE_DEFAULT));
    EXPECT_FALSE(settings.isMenubarVisible());
    // Other settings and the toolbar choice are kept; the window switches the toolbar once
    EXPECT_EQ(settings.getSidebarWidth(), 222);
    EXPECT_EQ(settings.getSelectedToolbar(), "Portrait");
    EXPECT_TRUE(settings.consumePendingToolbarMigration());
    EXPECT_FALSE(settings.consumePendingToolbarMigration());
    // The migration saves the file: user data loaded from it must not be replaced by defaults
    EXPECT_EQ(settings.getButtonConfig(BUTTON_MOUSE_RIGHT)->getAction(), TOOL_HAND);
    EXPECT_EQ(settings.getDeviceClassForDevice("Test Tablet", GDK_SOURCE_PEN), static_cast<InputDeviceTypeOption>(2));

    // The migration was saved: loading again does not migrate a second time
    Settings reloaded(outPath);
    ASSERT_TRUE(reloaded.load());
    EXPECT_EQ(reloaded.getUiLayoutVersion(), UI_LAYOUT_VERSION);
    EXPECT_FALSE(reloaded.isMenubarVisible());
    EXPECT_FALSE(reloaded.consumePendingToolbarMigration());
    EXPECT_EQ(reloaded.getButtonConfig(BUTTON_MOUSE_RIGHT)->getAction(), TOOL_HAND);
    EXPECT_EQ(reloaded.getDeviceClassForDevice("Test Tablet", GDK_SOURCE_PEN), static_cast<InputDeviceTypeOption>(2));

    fs::remove(outPath);
}

TEST(SettingsTest, testRecentColorsReadWrite) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_Settings_testRecentColors.xml";
    fs::remove(outPath);

    Settings settings(outPath);
    settings.transactionStart();
    EXPECT_TRUE(settings.getRecentColors().get().empty());
    EXPECT_TRUE(settings.pushRecentColor(Color(0xffff0000U)));
    EXPECT_TRUE(settings.pushRecentColor(Color(0xffffff00U)));
    EXPECT_FALSE(settings.pushRecentColor(Colors::black));
    settings.transactionEnd();  // calls save()

    Settings loaded(outPath);
    ASSERT_TRUE(loaded.load());
    EXPECT_EQ(loaded.getRecentColors().get(), (std::vector<Color>{Color(0xffffff00U), Color(0xffff0000U)}));

    fs::remove(outPath);
}

TEST(SettingsTest, testPushRecentColorDebouncesSaveAndFlushes) {
    ScopedTempSettingsFile tmp{"xournalpp-test-units_Settings_recentColorDebounce.xml"};
    {
        Settings settings(tmp.path);
        EXPECT_FALSE(settings.hasPendingSave());
        settings.flushPendingSave();
        EXPECT_FALSE(fs::exists(tmp.path));

        ASSERT_TRUE(settings.pushRecentColor(Color(0xffff0000U)));
        // Pen-down must not write settings.xml synchronously
        EXPECT_TRUE(settings.hasPendingSave());
        EXPECT_FALSE(fs::exists(tmp.path));

        ASSERT_TRUE(settings.pushRecentColor(Color(0xff00ff00U)));
        EXPECT_TRUE(settings.hasPendingSave());
        EXPECT_FALSE(fs::exists(tmp.path));

        settings.flushPendingSave();
        EXPECT_FALSE(settings.hasPendingSave());
        ASSERT_TRUE(fs::exists(tmp.path));

        Settings midReload(tmp.path);
        ASSERT_TRUE(midReload.load());
        EXPECT_EQ(midReload.getRecentColors().get(), (std::vector<Color>{Color(0xff00ff00U), Color(0xffff0000U)}));

        // Another pending change is flushed on destruction/shutdown
        ASSERT_TRUE(settings.pushRecentColor(Color(0xff0000ffU)));
        EXPECT_TRUE(settings.hasPendingSave());
    }
    Settings reloaded(tmp.path);
    ASSERT_TRUE(reloaded.load());
    EXPECT_EQ(reloaded.getRecentColors().get(),
              (std::vector<Color>{Color(0xff0000ffU), Color(0xff00ff00U), Color(0xffff0000U)}));
}

TEST(SettingsTest, testPushRecentColorTimerSavesAfterDebounce) {
    ScopedTempSettingsFile tmp{"xournalpp-test-units_Settings_recentColorTimer.xml"};
    Settings settings(tmp.path);
    ASSERT_TRUE(settings.pushRecentColor(Color(0xffff0000U)));
    EXPECT_TRUE(settings.hasPendingSave());
    EXPECT_FALSE(fs::exists(tmp.path));

    const gint64 deadline = g_get_monotonic_time() + 4 * G_TIME_SPAN_SECOND;
    while (settings.hasPendingSave() && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(10000);
    }

    EXPECT_FALSE(settings.hasPendingSave());
    ASSERT_TRUE(fs::exists(tmp.path));
    Settings reloaded(tmp.path);
    ASSERT_TRUE(reloaded.load());
    EXPECT_EQ(reloaded.getRecentColors().get(), (std::vector<Color>{Color(0xffff0000U)}));
}

TEST(SettingsTest, testCanvasAidColorsDefaultAndMigration) {
    // Fresh settings use the new defaults
    ScopedTempSettingsFile freshTmp{"xournalpp-test-units_Settings_aidColorsFresh.xml"};
    Settings fresh{freshTmp.path};
    EXPECT_EQ(fresh.getBorderColor(), Colors::mx_accent);
    EXPECT_EQ(fresh.getActiveSelectionColor(), Colors::mx_search_current);

    ScopedTempSettingsFile outTmp{"xournalpp-test-units_Settings_testAidColorsMigration.xml"};
    const fs::path& outPath = outTmp.path;

    // Older settings file with old defaults gets migrated to the new accent/search defaults
    {
        std::ofstream v1Defaults(outPath);
        v1Defaults << R"(<?xml version="1.0"?>
<settings>
  <property name="uiLayoutVersion" value="1"/>
  <property name="selectionBorderColor" value="4294901760"/>
  <property name="selectionMarkerColor" value="4285702095"/>
  <property name="activeSelectionColor" value="4286381056"/>
</settings>
)";
    }

    Settings migrated(outPath);
    ASSERT_TRUE(migrated.load());
    EXPECT_EQ(migrated.getUiLayoutVersion(), UI_LAYOUT_VERSION);
    EXPECT_EQ(migrated.getBorderColor(), Colors::mx_accent);
    EXPECT_EQ(migrated.getActiveSelectionColor(), Colors::mx_search_current);

    // Older settings file with custom colours keeps the user's choices
    const Color customBorder{0xff123456U};
    const Color customSearch{0xff654321U};
    {
        std::ofstream v1Custom(outPath);
        v1Custom << "<?xml version=\"1.0\"?>\n<settings>\n"
                 << "  <property name=\"uiLayoutVersion\" value=\"1\"/>\n"
                 << "  <property name=\"selectionBorderColor\" value=\"" << std::to_string(uint32_t(customBorder))
                 << "\"/>\n"
                 << "  <property name=\"activeSelectionColor\" value=\"" << std::to_string(uint32_t(customSearch))
                 << "\"/>\n"
                 << "</settings>\n";
    }

    Settings keptCustom(outPath);
    ASSERT_TRUE(keptCustom.load());
    EXPECT_EQ(keptCustom.getUiLayoutVersion(), UI_LAYOUT_VERSION);
    EXPECT_EQ(keptCustom.getBorderColor(), customBorder);
    EXPECT_EQ(keptCustom.getActiveSelectionColor(), customSearch);

    // A current settings file where the user explicitly chose the old colours is not overwritten
    {
        std::ofstream v2Explicit(outPath);
        v2Explicit << "<?xml version=\"1.0\"?>\n<settings>\n"
                   << "  <property name=\"uiLayoutVersion\" value=\"" << std::to_string(UI_LAYOUT_VERSION) << "\"/>\n"
                   << "  <property name=\"selectionBorderColor\" value=\"" << std::to_string(uint32_t(Colors::red))
                   << "\"/>\n"
                   << "  <property name=\"activeSelectionColor\" value=\""
                   << std::to_string(uint32_t(Colors::lawngreen)) << "\"/>\n"
                   << "</settings>\n";
    }

    Settings keptExplicit(outPath);
    ASSERT_TRUE(keptExplicit.load());
    EXPECT_EQ(keptExplicit.getBorderColor(), Colors::red);
    EXPECT_EQ(keptExplicit.getActiveSelectionColor(), Colors::lawngreen);
}

TEST(SettingsTest, testSelectionColorDefaultAndMigration) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_Settings_testSelectionColor.xml";
    fs::remove(outPath);

    // Fresh settings use the app accent #3563e9
    {
        ScopedTempSettingsFile freshTmp{"xournalpp-test-units_Settings_selectionColorFresh.xml"};
        Settings fresh{freshTmp.path};
        EXPECT_EQ(fresh.getSelectionColor(), DEFAULT_SELECTION_COLOR);
        EXPECT_EQ(fresh.getSelectionColor(), Color(0xff3563e9U));
    }

    // Stored v1 settings with the old default (#729fcf) are migrated once to #3563e9
    {
        std::ofstream v1(outPath);
        v1 << R"(<?xml version="1.0"?>
<settings>
  <property name="uiLayoutVersion" value="1"/>
  <property name="selectionMarkerColor" value="4285702095"/>
</settings>
)";
    }
    {
        Settings migrated(outPath);
        ASSERT_TRUE(migrated.load());
        EXPECT_EQ(migrated.getUiLayoutVersion(), UI_LAYOUT_VERSION);
        EXPECT_EQ(migrated.getSelectionColor(), Color(0xff3563e9U));
    }

    // Stored v1 settings with legacy RGB-only old default (0x00729fcf = 7512015) are also migrated
    {
        std::ofstream v1LegacyRgb(outPath);
        v1LegacyRgb << R"(<?xml version="1.0"?>
<settings>
  <property name="uiLayoutVersion" value="1"/>
  <property name="selectionMarkerColor" value="7512015"/>
</settings>
)";
    }
    {
        Settings migrated(outPath);
        ASSERT_TRUE(migrated.load());
        EXPECT_EQ(migrated.getSelectionColor(), Color(0xff3563e9U));
    }

    // Stored v1 settings with a custom selection colour are preserved
    {
        std::ofstream custom(outPath);
        custom << R"(<?xml version="1.0"?>
<settings>
  <property name="uiLayoutVersion" value="1"/>
  <property name="selectionMarkerColor" value="4294901930"/>
</settings>
)";
    }
    {
        Settings kept(outPath);
        ASSERT_TRUE(kept.load());
        EXPECT_EQ(kept.getUiLayoutVersion(), UI_LAYOUT_VERSION);
        EXPECT_EQ(kept.getSelectionColor(), Color(0xffff00aaU));
    }

    // Current-version settings explicitly set back to #729fcf are not overwritten
    {
        std::ofstream explicitOld(outPath);
        explicitOld << R"(<?xml version="1.0"?>
<settings>
  <property name="uiLayoutVersion" value="3"/>
  <property name="selectionMarkerColor" value="4285702095"/>
</settings>
)";
    }
    {
        Settings keptOld(outPath);
        ASSERT_TRUE(keptOld.load());
        EXPECT_EQ(keptOld.getSelectionColor(), Colors::xopp_cornflowerblue);
    }

    fs::remove(outPath);
}

TEST(ConfigMigrationTest, skipsEmergencySaveAndErrorLogs) {
    const fs::path baseDir = fs::temp_directory_path() / "morexournal-test-config-migration";
    std::error_code ec;
    fs::remove_all(baseDir, ec);
    const fs::path srcDir = baseDir / "xournalpp";
    const fs::path dstDir = baseDir / "morexournal";

    fs::create_directories(srcDir / "palettes");
    fs::create_directories(srcDir / "errorlogs");
    {
        std::ofstream(srcDir / "settings.xml") << "<settings/>";
        std::ofstream(srcDir / "toolbar.ini") << "[MoreXournal]\n";
        std::ofstream(srcDir / "palettes" / "custom.gpl") << "GIMP Palette\n";
        std::ofstream(srcDir / "emergencysave.xopp") << "stale crash save";
        std::ofstream(srcDir / "errorlogs" / "errorlog.20260101.log") << "crash trace";
    }

    XournalMain::copyMigratedConfigDirectory(srcDir, dstDir);

    EXPECT_TRUE(fs::exists(dstDir / "settings.xml"));
    EXPECT_TRUE(fs::exists(dstDir / "toolbar.ini"));
    EXPECT_TRUE(fs::exists(dstDir / "palettes" / "custom.gpl"));
    EXPECT_FALSE(fs::exists(dstDir / "emergencysave.xopp"));
    EXPECT_FALSE(fs::exists(dstDir / "errorlogs"));

    fs::remove_all(baseDir, ec);
}

TEST(ConfigMigrationTest, keepsBrokenSymlinksAndCleansUpTempDir) {
    const fs::path baseDir = fs::temp_directory_path() / "morexournal-test-config-migration-symlink";
    std::error_code ec;
    fs::remove_all(baseDir, ec);
    const fs::path srcDir = baseDir / "xournalpp";
    const fs::path dstDir = baseDir / "morexournal";

    fs::create_directories(srcDir / "plugins");
    {
        std::ofstream(srcDir / "settings.xml") << "<settings/>";
        std::ofstream(srcDir / "plugins" / "real.lua") << "-- plugin\n";
    }

    std::error_code symlinkEc;
    fs::create_symlink(srcDir / "missing-target", srcDir / "plugins" / "broken.lua", symlinkEc);
    if (symlinkEc) {
        fs::remove_all(baseDir, ec);
        GTEST_SKIP() << "Symlinks are not supported in this environment: " << symlinkEc.message();
    }
    fs::create_symlink(srcDir / "missing-top", srcDir / "broken-top");
    fs::create_symlink(srcDir / "plugins" / "real.lua", srcDir / "plugins" / "valid-link.lua");

    EXPECT_NO_THROW(XournalMain::copyMigratedConfigDirectory(srcDir, dstDir));

    EXPECT_TRUE(fs::exists(dstDir / "settings.xml"));
    EXPECT_TRUE(fs::exists(dstDir / "plugins" / "real.lua"));
    EXPECT_TRUE(fs::exists(dstDir / "plugins" / "valid-link.lua"));
    // Broken links are copied as links instead of aborting the migration
    EXPECT_TRUE(fs::is_symlink(fs::symlink_status(dstDir / "plugins" / "broken.lua")));
    EXPECT_TRUE(fs::is_symlink(fs::symlink_status(dstDir / "broken-top")));

    // No temporary sibling directory should remain
    for (const auto& entry: fs::directory_iterator(baseDir)) {
        const auto name = entry.path().filename().u8string();
        EXPECT_EQ(name.find(u8".migrating-"), std::u8string::npos);
    }

    fs::remove_all(baseDir, ec);
}

TEST(ConfigMigrationTest, removesTempDirAndLeavesTargetAbsentOnFailure) {
    const fs::path baseDir = fs::temp_directory_path() / "morexournal-test-config-migration-failure";
    std::error_code ec;
    fs::remove_all(baseDir, ec);
    fs::create_directories(baseDir);

    const fs::path missingSrc = baseDir / "does-not-exist";
    const fs::path dstDir = baseDir / "morexournal";

    EXPECT_THROW(XournalMain::copyMigratedConfigDirectory(missingSrc, dstDir), fs::filesystem_error);
    EXPECT_FALSE(fs::exists(dstDir));

    for (const auto& entry: fs::directory_iterator(baseDir)) {
        const auto name = entry.path().filename().u8string();
        EXPECT_EQ(name.find(u8".migrating-"), std::u8string::npos);
    }

    fs::remove_all(baseDir, ec);
}

TEST(ConfigMigrationTest, resetsMissingOrXournalppLatexTemplatePath) {
    const fs::path baseDir = fs::temp_directory_path() / "morexournal-test-latex-template-migration";
    std::error_code ec;
    fs::remove_all(baseDir, ec);

    const fs::path customTemplate = baseDir / "custom" / "my_template.tex";
    const fs::path mxTemplate = baseDir / "share" / "morexournal" / "resources" / "default_template.tex";
    const fs::path xoppShareTemplate = baseDir / "share" / "xournalpp" / "resources" / "default_template.tex";
    const fs::path xoppMacTemplate =
            baseDir / "Xournal++.app" / "Contents" / "Resources" / "resources" / "default_template.tex";

    for (const fs::path& p: {customTemplate, mxTemplate, xoppShareTemplate, xoppMacTemplate}) {
        fs::create_directories(p.parent_path());
        std::ofstream(p) << "\\documentclass{standalone}\n";
    }

    EXPECT_TRUE(XournalMain::shouldResetLatexTemplatePath(fs::path{}));
    EXPECT_TRUE(XournalMain::shouldResetLatexTemplatePath(baseDir / "nonexistent" / "default_template.tex"));
    EXPECT_TRUE(XournalMain::shouldResetLatexTemplatePath(xoppShareTemplate));
    EXPECT_TRUE(XournalMain::shouldResetLatexTemplatePath(xoppMacTemplate));
    EXPECT_FALSE(XournalMain::shouldResetLatexTemplatePath(customTemplate));
    EXPECT_FALSE(XournalMain::shouldResetLatexTemplatePath(mxTemplate));

    fs::remove_all(baseDir, ec);
}
