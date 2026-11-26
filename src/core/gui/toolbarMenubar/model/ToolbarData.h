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

#include <string>  // for string
#include <vector>  // for vector

#include <glib.h>  // for GKeyFile

class ToolbarEntry;
struct Palette;

class ToolbarData {
public:
    ToolbarData(bool predefined);
    virtual ~ToolbarData();

public:
    const std::string& getName() const;
    const std::string& getId() const;
    bool isPredefined() const;

    void load(GKeyFile* config, const char* group, const Palette& colorPalette);

private:
    std::string id;
    std::string name;
    std::vector<ToolbarEntry> contents;

    bool predefined = false;

    friend class ToolbarModel;
    friend class ToolMenuHandler;
};
