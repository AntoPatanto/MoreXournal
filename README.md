<p align="center">
  <img src="ui/pixmaps/io.github.antopatanto.morexournal.svg" width="80" height="80" alt="">
</p>

<h1 align="center">MoreXournal</h1>

<p align="center">
  A fork of <a href="https://github.com/xournalpp/xournalpp">Xournal++</a> for handwritten notes and PDF annotation, with editable shapes and a single top bar.<br>
  Runs locally on your computer with no account, server or telemetry, and installs side by side with Xournal++.
</p>

<p align="center">
  <a href="LICENSE"><img alt="GPL-2.0 license" src="https://img.shields.io/badge/license-GPL--2.0-3563e9"></a>
</p>

<p align="center">
  <a href="#build-from-source">Build from source</a> ·
  <a href="#documentation">Documentation</a> ·
  <a href="CONTRIBUTING.md">Contributing</a>
</p>

> [!NOTE]
> TODO: Add light and dark screenshots of MoreXournal's current UI (top bar, floating tool pill and editable shape handles) in `docs/screenshots/`. The screenshots under `readme/` are from upstream Xournal++ and do not reflect MoreXournal's interface.

## Why MoreXournal

MoreXournal is a public fork of [Xournal++](https://github.com/xournalpp/xournalpp) that adds hold-to-shape drawing, editable shape objects, and a single-bar GTK3 interface while keeping compatibility with `.xopp` files.

It is free software under GPL-2.0 and runs locally on your machine with no account, server or telemetry. Because it uses its own application ID (`io.github.antopatanto.morexournal`), binaries (`morexournal`), and configuration folder, you can install it alongside Xournal++ without sharing settings.

## What you can do

**Convert strokes into shapes by holding the pen still.** While drawing with the pen or highlighter, hold the tip still (500 ms by default, configurable from 100 to 3000 ms in Preferences, or toggleable as Automatic shapes in the tool pill) to replace the stroke with a recognized line, triangle, rectangle or circle before lifting the pen. Recognized lines snap to 15-degree steps when rotation snapping is enabled, and one undo removes the shape.

**Edit shapes after you draw them.** Shapes created with hold-to-shape or with the line, rectangle and ellipse tools store their type and control points. Selecting a shape displays geometry handles (endpoints for lines, free vertices for polygons, corners and rotation for rectangles, side stretch and rotation for ellipses) and a floating shape menu to change stroke colour, thickness, fill colour and opacity, solid, dashed or dotted line style, duplicate, delete, or convert the shape back to a plain stroke.

**Work in a single top bar.** One header bar replaces the classic menu bar and toolbar rows: the main menu and document title with save status sit on the left, the eight main tools in the centre, and undo/redo, page sidebar toggle, page navigation and new page on the right. Tap <kbd>Alt</kbd> (or press <kbd>F10</kbd>) to show the menu bar temporarily, or enable "Always show the menu bar" in Preferences.

**Adjust tools from floating controls.** A rounded floating pill under the top bar shows options for the active tool, including black plus up to five recently used colours (shared across pen, highlighter, shapes and text), thickness, line style and fill. Zoom controls and the layer selector float in the bottom corners and follow the page area when the sidebar or search bar opens.

**Switch between indigo light and dark themes.** Built-in themes (`ui/morexournal-light.css` and `ui/morexournal-dark.css`) follow your system or application theme setting while keeping the page paper light in dark mode.

**Keep upstream Xournal++ features.** Pressure-sensitive styluses, PDF annotation with text selection and link following, paper backgrounds, LaTeX formulas, audio recording, Lua plugins, and export to PDF, PNG and SVG remain available.

## Build from source

Prebuilt releases are not published yet, so build MoreXournal from source with CMake and Ninja.

You need a C++20 compiler (GCC 11+ or Clang 15+), CMake 3.18+, Ninja, GTK 3.18.9+, `poppler-glib` 0.41.0+, libxml2, libzip, librsvg and zlib. Optional features use PortAudio and libsndfile for audio, qpdf for PDF export, Lua for plugins, GtkSourceView 4 for LaTeX syntax highlighting, and GoogleTest for tests. Dependency install commands for Arch, Fedora, Ubuntu/Debian, openSUSE and Solus are in [readme/LinuxBuild.md](readme/LinuxBuild.md).

Clone the repository, configure, build, install to a local prefix, and run:

```bash
git clone https://github.com/AntoPatanto/MoreXournal.git
cd MoreXournal
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=build/install
cmake --build build
cmake --build build --target install
./build/install/bin/morexournal
```

Running the `install` target is required because MoreXournal loads its UI, icons and themes from `share/morexournal` in the install prefix. To install into `~/.local` instead, pass `-DCMAKE_INSTALL_PREFIX="$HOME/.local"` and run `./build/cmake/postinst configure` after installing so desktop, MIME and icon caches update.

To build and run the unit and GTK integration tests:

```bash
cmake -S . -B build -G Ninja -DENABLE_GTEST=ON
cmake --build build --target test-units test-gtk-integration
ctest --test-dir build --output-on-failure
```

If GoogleTest is not installed on your system, add `-DDOWNLOAD_GTEST=ON` to the configure command.

## Your data

MoreXournal stores all notes and settings locally and makes no network requests of its own, with no account, cloud sync or telemetry.

On Linux, configuration, cache, data and state live under `morexournal`: `~/.config/morexournal`, `~/.cache/morexournal`, `~/.local/share/morexournal` and `~/.local/state/morexournal`. On first launch, if `~/.config/xournalpp` exists and `~/.config/morexournal` does not, MoreXournal copies your Xournal++ settings and leaves the original folder untouched.

Notes are saved in the gzipped XML `.xopp` format shared with Xournal++. Attached PDFs are linked by path rather than embedded. Editable shapes are saved as extra attributes (`mxShape`, `mxShapeParams`, `mxFillColor`) on `<stroke>` elements, which upstream Xournal++ ignores so the same file opens there as regular strokes. Exporting to legacy `.xoj` drops Xournal++ and MoreXournal extensions.

## Documentation

| Read this                                                           | To learn                                           |
| ------------------------------------------------------------------- | -------------------------------------------------- |
| [Compilation and testing](readme/Compile.md)                        | Platform-independent CMake build and test options  |
| [Linux build guide](readme/LinuxBuild.md)                           | Distro dependency packages and Linux packaging     |
| [macOS build guide](readme/MacBuild.md)                             | Building on macOS (from upstream Xournal++)        |
| [Windows build guide](readme/WindowsBuild.md)                       | Building on Windows (from upstream Xournal++)      |
| [Contributing](CONTRIBUTING.md)                                     | Coding conventions and pull request guidelines     |
| [Xournal++ user guide](https://xournalpp.github.io/guide/overview/) | Upstream documentation for core Xournal++ features |

## License

MoreXournal is a fork of [Xournal++](https://github.com/xournalpp/xournalpp), which builds on [Xournal](http://sourceforge.net/projects/xournal/). It is licensed under the [GNU General Public License v2.0](LICENSE). Upstream contributors are listed in [AUTHORS](AUTHORS), and packaging copyright notices are in [debian/copyright](debian/copyright). Upstream translations are managed on [Crowdin](https://crowdin.com/project/xournalpp/).
