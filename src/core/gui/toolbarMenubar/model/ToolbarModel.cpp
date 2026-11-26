#include "ToolbarModel.h"

#include "util/StringUtils.h"

#include "ToolbarData.h"  // for ToolbarData
#include "filesystem.h"   // for path

ToolbarModel::ToolbarModel() = default;
ToolbarModel::~ToolbarModel() = default;

auto ToolbarModel::getToolbars() const -> const std::vector<std::unique_ptr<ToolbarData>>& { return this->toolbars; }

void ToolbarModel::parseGroup(GKeyFile* config, const char* group, bool predefined, const Palette& colorPalette) {
    auto data = std::make_unique<ToolbarData>(predefined);

    data->name = (predefined ? "predef_" : "custom_");
    data->id = group;

    data->load(config, group, colorPalette);

    add(std::move(data));
}

auto ToolbarModel::add(std::unique_ptr<ToolbarData> data) -> ToolbarData* {
    return this->toolbars.emplace_back(std::move(data)).get();
}

auto ToolbarModel::parse(fs::path const& filepath, bool predefined, const Palette& colorPalette) -> bool {
    GKeyFile* config = g_key_file_new();
    g_key_file_set_list_separator(config, ',');
    if (!g_key_file_load_from_file(config, char_cast(filepath.u8string().c_str()), G_KEY_FILE_NONE, nullptr)) {
        g_key_file_free(config);
        return false;
    }

    gsize length = 0;
    gchar** groups = g_key_file_get_groups(config, &length);

    for (gsize i = 0; i < length; i++) {
        parseGroup(config, groups[i], predefined, colorPalette);
    }

    g_strfreev(groups);
    g_key_file_free(config);
    return true;
}
