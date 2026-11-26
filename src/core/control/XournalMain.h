/*
 * Xournal++
 *
 * Xournal main entry, commandline parser
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "filesystem.h"  // for path

namespace XournalMain {
auto run(int argc, char** argv) -> int;
void initLocalisation();
/// Copies an upstream configuration directory into MoreXournal's config folder, skipping crash artifacts.
void copyMigratedConfigDirectory(const fs::path& sourceDir, const fs::path& targetDir);
/// Returns true when a stored LaTeX template path is empty, missing, or points into a Xournal++ data directory.
auto shouldResetLatexTemplatePath(const fs::path& templatePath) -> bool;
}  // namespace XournalMain
