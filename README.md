<p align="center">
  <img src="ui/pixmaps/io.github.antopatanto.morexournal.svg" width="80" height="80" alt="">
</p>

<h1 align="center">MoreXournal</h1>

<p align="center">
  A fork of <a href="https://github.com/xournalpp/xournalpp">Xournal++</a> for handwritten notes and PDF annotation, with editable shapes and a single top bar.<br>
  Runs locally on your computer with no account, server or telemetry, and installs side by side with Xournal++.
</p>

<p align="center">
  <a href="https://github.com/AntoPatanto/MoreXournal/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/AntoPatanto/MoreXournal?label=release&color=3563e9"></a>
  <a href="LICENSE"><img alt="GPL-2.0 license" src="https://img.shields.io/badge/license-GPL--2.0-3563e9"></a>
</p>

<p align="center">
  <a href="#install">Download</a> ·
  <a href="#documentation">Documentation</a> ·
  <a href="CHANGELOG.md">Changelog</a> ·
  <a href="CONTRIBUTING.md">Contributing</a>
</p>

## Why MoreXournal

MoreXournal is a public fork of [Xournal++](https://github.com/xournalpp/xournalpp) (based on Xournal++ 1.3.8) that adds hold-to-shape drawing, editable shape objects, and a single-bar GTK3 interface while keeping `.xopp` file compatibility.

It is free software under GPL-2.0 and runs locally on your machine with no account, server or telemetry. Because it uses its own application ID, executables and configuration folder, you can install it alongside Xournal++ without sharing settings.

## What you can do

**Convert strokes into shapes by holding the pen still.** While drawing with the pen or highlighter, hold the tip still (400 ms by default) to replace the stroke with a recognized line, triangle, rectangle or circle before lifting the pen. You can toggle Automatic shapes from the Tools menu or tune its delay, jitter tolerance, sensitivity, minimum size and per-threshold values in Preferences, with a built-in test pad. Recognized lines snap to 15-degree steps when rotation snapping is enabled, and one undo removes the shape.

**Edit shapes after you draw them.** Shapes drawn with the line, rectangle and ellipse tools store their type and control points, while hold-to-shape and the shape recognizer produce plain strokes. Selecting an editable shape displays geometry handles (endpoints for lines, corners and rotation for rectangles, side stretch and rotation for ellipses) with device-aware hit areas, plus a floating shape menu to change stroke color, thickness, fill color and opacity, solid, dashed, dash-dot or dotted line style, duplicate, delete, or convert the shape back to a plain stroke.

**Work in a single top bar.** One header bar replaces the classic menu bar and toolbar rows: the main menu and document title with save status sit on the left, the eight main tools in the center, and undo/redo, page sidebar toggle, page navigation and new page on the right. Tap <kbd>Alt</kbd> to show the menu bar temporarily, press <kbd>F10</kbd> to keep it shown or hidden, or press <kbd>F6</kbd> to move keyboard focus across the bar, tool pill and canvas. The bar switches to an overflow menu on narrow windows (down to 600 px) and adjusts spacing when Touch mode is enabled or a touchscreen is used.

**Adjust tools from floating controls.** A rounded floating pill under the top bar shows options for the active tool, including recently used colors (shared across pen, highlighter, shapes and text), thickness, line style, fill, selection actions and fullscreen toggle. Zoom controls and the layer selector float in the bottom corners and follow the page area when the sidebar or search bar opens, while the eraser indicator renders at true size on the canvas and the pen uses a halo dot cursor.

**Switch between indigo light and dark themes.** Built-in themes (`ui/morexournal-light.css` and `ui/morexournal-dark.css`) follow your system or application theme setting while keeping the page paper light in dark mode.

**Keep upstream Xournal++ features.** Pressure-sensitive styluses, PDF annotation with text selection and link following, paper backgrounds, LaTeX formulas, audio recording, Lua plugins, and export to PDF, PNG and SVG remain available.

## Install

Download the latest release from the [GitHub Releases page](https://github.com/AntoPatanto/MoreXournal/releases/latest).

| System                  | File                                  |
| ----------------------- | ------------------------------------- |
| Linux (x86_64, general) | `morexournal-0.1.0-x86_64.AppImage`   |
| Fedora Linux (x86_64)   | `morexournal-0.1.0-1.fc45.x86_64.rpm` |
| Source archive          | `morexournal-0.1.0.tar.gz`            |

Prebuilt `.deb` packages, Linux ARM64 builds, macOS bundles and Windows installers are not published in `v0.1.0`, so users on those platforms should [build from source](#build-from-source) (Debian and Ubuntu x86_64 users can run the AppImage directly). Release artifacts are unsigned, and no separate checksum file is included with `v0.1.0`.

- **Linux AppImage.** Make the downloaded file executable and run it:

  ```bash
  chmod +x morexournal-0.1.0-x86_64.AppImage
  ./morexournal-0.1.0-x86_64.AppImage
  ```

  Running an AppImage directly requires FUSE 2. If FUSE 2 is not installed on your system, run `./morexournal-0.1.0-x86_64.AppImage --appimage-extract-and-run` instead. Each release includes a `.AppImage.zsync` file so tools like [AppImageUpdate](https://github.com/AppImageCommunity/AppImageUpdate) can fetch binary deltas for future releases.

- **Fedora RPM.** Install the package with `dnf`, which pulls the required GTK3 and library dependencies and installs alongside `xournalpp` without file conflicts:

  ```bash
  sudo dnf install ./morexournal-0.1.0-1.fc45.x86_64.rpm
  ```

  The RPM is built against `fedora:latest` in CI, so if your Fedora version ships different shared library versions, use the AppImage or [build from source](#build-from-source).

### First launch

MoreXournal registers its own `morexournal` binary and `io.github.antopatanto.morexournal` desktop entry, so it does not overwrite an existing Xournal++ installation. On first launch, if `~/.config/xournalpp` exists and `~/.config/morexournal` does not, it copies your Xournal++ settings (skipping `emergencysave.xopp` and `errorlogs`) and leaves `~/.config/xournalpp` untouched.

MoreXournal does not check for updates automatically. Download new versions from the [Releases page](https://github.com/AntoPatanto/MoreXournal/releases), or update the AppImage with `AppImageUpdate`.

## Your data

MoreXournal stores all notes and settings locally and makes no network requests of its own, with no account, cloud sync or telemetry.

On Linux, configuration, cache, data and state live under `morexournal`: `~/.config/morexournal`, `~/.cache/morexournal`, `~/.local/share/morexournal` and `~/.local/state/morexournal`.

Notes are saved in the gzipped XML `.xopp` format shared with Xournal++. A PDF you annotate is linked by its path, or, if you choose to attach it, copied next to the note as `<name>.xopp.bg.pdf`, because it is never embedded in the `.xopp` file. Editable shapes are saved as extra attributes (`mxShape`, `mxShapeParams`, `mxFillColor`) on `<stroke>` elements, which upstream Xournal++ ignores so the same file opens there as regular strokes. Exporting to legacy `.xoj` drops Xournal++ and MoreXournal extensions.

## Build from source

Build MoreXournal from source with CMake and Ninja if you are on a platform without a prebuilt package or want to develop locally.

You need a C++20 compiler (GCC 11+ or Clang 15+), CMake 3.18+, Ninja, GTK 3.18.9+, `poppler-glib` 0.41.0+, libxml2, libzip, librsvg, zlib and gettext. Audio via PortAudio and libsndfile, qpdf export, Lua plugins and GtkSourceView 4 LaTeX highlighting are enabled by default and can be turned off with `-DENABLE_AUDIO=OFF`, `-DENABLE_QPDF=OFF`, `-DENABLE_PLUGINS=OFF` and `-DENABLE_GTK_SOURCEVIEW=OFF`. Except in `Release` builds, configure also downloads cpptrace from GitHub, which requires zstd unless you pass `-DENABLE_CPPTRACE=OFF`. GoogleTest is only needed for the tests. Dependency install commands for Arch, Fedora, Ubuntu/Debian, openSUSE and Solus are in [readme/LinuxBuild.md](readme/LinuxBuild.md).

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

If GoogleTest is not installed on your system, add `-DDOWNLOAD_GTEST=ON` to the configure command. See [CONTRIBUTING.md](CONTRIBUTING.md) for coding conventions and pull request guidelines.

## Documentation

| Read this                                                           | To learn                                           |
| ------------------------------------------------------------------- | -------------------------------------------------- |
| [Changelog](CHANGELOG.md)                                           | Release notes for MoreXournal and upstream history |
| [Compilation and testing](readme/Compile.md)                        | Platform-independent CMake build and test options  |
| [Linux build guide](readme/LinuxBuild.md)                           | Distro dependency packages and Linux packaging     |
| [macOS build guide](readme/MacBuild.md)                             | Building on macOS (from upstream Xournal++)        |
| [Windows build guide](readme/WindowsBuild.md)                       | Building on Windows (from upstream Xournal++)      |
| [Contributing](CONTRIBUTING.md)                                     | Coding conventions and pull request guidelines     |
| [Xournal++ user guide](https://xournalpp.github.io/guide/overview/) | Upstream documentation for core Xournal++ features |

## License

MoreXournal is a fork of [Xournal++](https://github.com/xournalpp/xournalpp), which builds on [Xournal](http://sourceforge.net/projects/xournal/). It is licensed under the [GNU General Public License v2.0](LICENSE). Upstream contributors are listed in [AUTHORS](AUTHORS), and packaging copyright notices are in [debian/copyright](debian/copyright). Upstream translations are managed on [Crowdin](https://crowdin.com/project/xournalpp/).
