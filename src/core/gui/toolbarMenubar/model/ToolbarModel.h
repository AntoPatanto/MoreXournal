/*
 * Xournal++
 *
 * Toolbar definitions model
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glib.h>

#include "filesystem.h"

class ToolbarData;
struct Palette;

class ToolbarModel {
public:
    ToolbarModel();
    virtual ~ToolbarModel();

private:
    ToolbarModel(const ToolbarModel& other);
    void operator=(const ToolbarModel& other);

public:
    const std::vector<std::unique_ptr<ToolbarData>>& getToolbars() const;
    bool parse(fs::path const& filepath, bool predefined, const Palette& colorPalette);

private:
    ToolbarData* add(std::unique_ptr<ToolbarData> data);
    void parseGroup(GKeyFile* config, const char* group, bool predefined, const Palette& colorPalette);

private:
    std::vector<std::unique_ptr<ToolbarData>> toolbars;
};
