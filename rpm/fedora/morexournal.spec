# Request an out-of-source build even where RPM macros default to in-source builds.
# This macro is checked for existence, so defining it as 0 still enables it.
%undefine __cmake_in_source_build

Name:           morexournal
Version:        0.1.0
Release:        1%{?dist}
Summary:        Handwriting note-taking software with PDF annotation support

License:        GPL-2.0-or-later
URL:            https://github.com/AntoPatanto/MoreXournal
Packager:       MoreXournal maintainers <https://github.com/AntoPatanto/MoreXournal/issues>
Source0:        https://github.com/AntoPatanto/MoreXournal/releases/download/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.18
BuildRequires:  desktop-file-utils
BuildRequires:  fdupes
BuildRequires:  gcc-c++
BuildRequires:  gettext
BuildRequires:  help2man
BuildRequires:  libappstream-glib
BuildRequires:  pkgconfig(glib-2.0) >= 2.32.0
BuildRequires:  pkgconfig(gtk+-3.0) >= 3.18.9
BuildRequires:  pkgconfig(gtksourceview-4) >= 4.0
BuildRequires:  pkgconfig(libqpdf) >= 11.0.0
BuildRequires:  pkgconfig(librsvg-2.0) >= 2.46
BuildRequires:  pkgconfig(libxml-2.0) >= 2.0.0
BuildRequires:  pkgconfig(libzip) >= 1.0.1
BuildRequires:  pkgconfig(lua) >= 5.3
BuildRequires:  pkgconfig(poppler-glib) >= 0.41.0
BuildRequires:  pkgconfig(portaudiocpp) >= 12
BuildRequires:  pkgconfig(sndfile) >= 1.0.25
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(xi)
BuildRequires:  pkgconfig(zlib)

Requires:       hicolor-icon-theme
Recommends:     texlive-scheme-basic
Recommends:     texlive-standalone

%description
MoreXournal is a C++20/GTK3 handwriting note-taking application with PDF
annotation support, forked from Xournal++. It supports stylus input devices
such as Wacom tablets, hold-to-shape drawing, editable geometric shapes, and
Lua plugins.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake \
    -DDISTRO_CODENAME="Fedora Linux" \
    -DENABLE_CPPTRACE=OFF \
    -DENABLE_GTEST=OFF \
    %{?git_shortcommit:-DGIT_VERSION=%{git_shortcommit}}

%cmake_build

%install
%cmake_install

%find_lang %{name}

# Drop MIME-type icons that share the exact paths installed by Fedora's
# xournalpp package (/usr/share/icons/hicolor/scalable/mimetypes/*.svg) so
# MoreXournal can be installed side by side with xournalpp without conflicts.
rm -rf %{buildroot}%{_datadir}/icons/hicolor/scalable/mimetypes

# Remove helper script not meant for system-wide packaged installs
find %{buildroot}%{_datadir}/%{name} -name update-icon-cache.sh -delete -print

%fdupes %{buildroot}%{_datadir}/%{name}

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/io.github.antopatanto.morexournal.desktop
appstream-util validate-relax --nonet %{buildroot}%{_metainfodir}/io.github.antopatanto.morexournal.appdata.xml

%files -f %{name}.lang
%license LICENSE
%doc README.md AUTHORS
%{_bindir}/%{name}
%{_bindir}/%{name}-thumbnailer
%{_bindir}/%{name}-wrapper
%{_datadir}/applications/io.github.antopatanto.morexournal.desktop
%{_datadir}/icons/hicolor/scalable/apps/io.github.antopatanto.morexournal.svg
%{_datadir}/mime/packages/io.github.antopatanto.morexournal.xml
%{_datadir}/thumbnailers/io.github.antopatanto.morexournal.thumbnailer
%{_datadir}/%{name}/
%{_mandir}/man1/%{name}*.1*
%{_metainfodir}/io.github.antopatanto.morexournal.appdata.xml

%changelog
* Tue Nov 24 2026 MoreXournal maintainers <https://github.com/AntoPatanto/MoreXournal/issues> - 0.1.0-1
- Initial Fedora package for MoreXournal 0.1.0
