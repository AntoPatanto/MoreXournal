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

#include "control/settings/Settings.h"

TEST(SettingsTest, testLoadDoesNotThrowForNonExistingFilePath) {
    Settings settings{"non-existing-file-path"};
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
    Settings settings{"non-existing-file-path"};
    EXPECT_TRUE(settings.isHoldToShapeEnabled());
    EXPECT_EQ(settings.getHoldToShapeDelay(), 500);
}

TEST(SettingsTest, testHoldToShapeDelayIsClamped) {
    Settings settings{"non-existing-file-path"};
    settings.transactionStart();  // do not write the settings file
    settings.setHoldToShapeDelay(10);
    EXPECT_EQ(settings.getHoldToShapeDelay(), HOLD_TO_SHAPE_DELAY_MIN);
    settings.setHoldToShapeDelay(100000);
    EXPECT_EQ(settings.getHoldToShapeDelay(), HOLD_TO_SHAPE_DELAY_MAX);
    settings.setHoldToShapeDelay(750);
    EXPECT_EQ(settings.getHoldToShapeDelay(), 750);
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

TEST(SettingsTest, testLastShapeTypeOnlyAcceptsShapes) {
    Settings settings{"non-existing-file-path"};
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

    // The migration was saved: loading again does not migrate a second time
    Settings reloaded(outPath);
    ASSERT_TRUE(reloaded.load());
    EXPECT_EQ(reloaded.getUiLayoutVersion(), UI_LAYOUT_VERSION);
    EXPECT_FALSE(reloaded.isMenubarVisible());
    EXPECT_FALSE(reloaded.consumePendingToolbarMigration());

    fs::remove(outPath);
}
