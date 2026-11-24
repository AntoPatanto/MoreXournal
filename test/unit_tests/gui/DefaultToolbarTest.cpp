/*
 * MoreXournal
 *
 * The default toolbar layout must exist in the shipped toolbar.ini
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>  // for find_if

#include <config-test.h>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "gui/toolbarMenubar/model/ColorPalette.h"
#include "gui/toolbarMenubar/model/ToolbarData.h"
#include "gui/toolbarMenubar/model/ToolbarModel.h"

#include "filesystem.h"

TEST(DefaultToolbarTest, testDefaultLayoutIsPredefined) {
    Palette palette{fs::path{}};
    palette.load_default();

    ToolbarModel model;
    const fs::path ini = fs::path(std::u8string(PROJECT_SOURCE_DIR)) / "resources-templates" / "toolbar.ini.in";
    ASSERT_TRUE(model.parse(ini, true, palette));

    const fs::path tmpPath = fs::temp_directory_path() / "xournalpp-test-units_DefaultToolbarTest.xml";
    std::error_code ec;
    fs::remove(tmpPath, ec);
    {
        Settings settings{tmpPath};
        const std::string& defaultId = settings.getSelectedToolbar();
        EXPECT_EQ(defaultId, "MoreXournal");

        const auto& toolbars = model.getToolbars();
        auto it =
                std::find_if(toolbars.begin(), toolbars.end(), [&](const auto& d) { return d->getId() == defaultId; });
        ASSERT_NE(it, toolbars.end()) << "the default toolbar layout is missing from toolbar.ini";
        EXPECT_TRUE((*it)->isPredefined());
        EXPECT_EQ((*it)->getName(), "MoreXournal");
    }
    fs::remove(tmpPath, ec);
}
