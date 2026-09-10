#!/usr/bin/env bash
# Builds a self-contained .deb for Ubuntu 24.04.
#
# Ubuntu's apt Qt is 6.4, which is older than the minimum this project requires, so the
# package bundles its own Qt under /opt/vindauga rather than depending on whatever Qt (if
# any) the target system has installed. The selection of Qt libs/plugins/QML modules below
# is based on ldd/qmlimportscanner analysis of the built binary, not on guesswork.
#
# Usage: build-deb.sh <qt-gcc_64-dir> <pkgver>
#   e.g.: build-deb.sh /opt/qt/6.11.2/gcc_64 1.0.2
#
# Expects the following to be installed on the build machine (apt, see release.yml):
#   cmake ninja-build pkg-config freerdp3-dev git dpkg-dev
# qtkeychain is built from SOURCE against the BUNDLED Qt, not taken from apt's
# qtkeychain-qt6-dev: that package is linked against the system Qt, and mixing two Qt builds
# in one process risks private-API ABI crashes.
set -euo pipefail

QT_DIR="${1:?Usage: build-deb.sh <qt-gcc_64-dir> <pkgver>}"
PKGVER="${2:?Usage: build-deb.sh <qt-gcc_64-dir> <pkgver>}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== [1/7] Building qtkeychain from source against $QT_DIR =="
git clone --branch v0.14.0 --depth 1 https://github.com/frankosterfeld/qtkeychain.git "$WORK/qtkeychain-src"
cmake -B "$WORK/qtkeychain-build" -S "$WORK/qtkeychain-src" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -DCMAKE_INSTALL_PREFIX="$WORK/qtkeychain-install" \
  -DBUILD_WITH_QT6=ON -DBUILD_TRANSLATIONS=OFF -DLIBSECRET_SUPPORT=ON
cmake --build "$WORK/qtkeychain-build"
cmake --install "$WORK/qtkeychain-build"

echo "== [2/7] Building vindauga (Release) against bundled Qt + qtkeychain =="
cmake -B "$WORK/vindauga-build" -S "$REPO_ROOT" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_DIR;$WORK/qtkeychain-install" \
  -DVINDAUGA_WITH_RDP=ON
cmake --build "$WORK/vindauga-build"

echo "== [3/7] Running ctest (only bundled libs on LD_LIBRARY_PATH, as in the installed package) =="
env QT_QPA_PLATFORM=offscreen QTWEBENGINE_DISABLE_SANDBOX=1 \
  LD_LIBRARY_PATH="$QT_DIR/lib:$WORK/qtkeychain-install/lib" \
  ctest --test-dir "$WORK/vindauga-build" --output-on-failure

echo "== [4/7] Assembling package tree under /opt/vindauga =="
PKGROOT="$WORK/pkgroot"
OPT="$PKGROOT/opt/vindauga"
mkdir -p "$OPT/bin" "$OPT/lib" "$OPT/plugins" "$OPT/qml" "$OPT/libexec" "$OPT/resources" \
  "$OPT/translations/qtwebengine_locales" \
  "$PKGROOT/usr/bin" "$PKGROOT/usr/share/applications" "$PKGROOT/DEBIAN"

install -m755 "$WORK/vindauga-build/src/app/vindauga" "$OPT/bin/vindauga"

# --- Qt plugins: only the categories the application can actually load (QPA platform,
# TLS backend for HTTPS/OAuth, image formats, Wayland/X11 integration). Not included:
# designer/help/printsupport/sqldrivers/qmllint/qmlls/qmltooling/egldeviceintegrations,
# none of which are used.
PLUGIN_DIRS=(platforms tls imageformats iconengines generic networkinformation
  platforminputcontexts platformthemes position
  wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration
  xcbglintegrations)
for d in "${PLUGIN_DIRS[@]}"; do
  if [ -d "$QT_DIR/plugins/$d" ]; then
    cp -a "$QT_DIR/plugins/$d" "$OPT/plugins/"
  fi
done
# position/ is bundled only because QtWebEngineQuick links libQt6Positioning. The NMEA
# plugin (GPS over serial port) needs libQt6SerialPort.so.6, which is not part of the Qt
# installation and thus not bundled; dpkg-shlibdeps in step [7/7] fails hard on such an
# unresolved soname. Vindauga uses no positioning, so the plugin is dropped rather than
# pulling in another Qt module.
rm -f "$OPT/plugins/position/libqtposition_nmea.so"

# --- QML modules: determined with qmlimportscanner over the QML sources plus a manual
# check for "import QtWebEngine" in inline QML embedded in C++ strings (which the scanner
# cannot see). The WHOLE QtQuick tree is bundled, including ALL QtQuick.Controls styles:
# the FluentWinUI3 style has a runtime dependency on the Fusion style plugin (it borrows
# control delegates it does not reimplement), and trimming to a subset of styles fails at
# startup with "module QtQuick.Controls.Fusion ... plugin ... not found".
QML_DIRS=(QtQuick QtQml QML Qt QtWebEngine QtWebChannel QtPositioning)
for d in "${QML_DIRS[@]}"; do
  if [ -d "$QT_DIR/qml/$d" ]; then
    cp -a "$QT_DIR/qml/$d" "$OPT/qml/"
  fi
done

install -m755 "$QT_DIR/libexec/QtWebEngineProcess" "$OPT/libexec/QtWebEngineProcess"
cp -a "$QT_DIR/resources/." "$OPT/resources/"
cp -a "$QT_DIR/translations/qtwebengine_locales/." "$OPT/translations/qtwebengine_locales/"

echo "== [5/7] Collecting bundled Qt/qtkeychain libs (recursive ldd over what is actually in the package) =="
LIBSRC_DIRS=("$QT_DIR/lib" "$WORK/qtkeychain-install/lib")
NEEDED="$WORK/needed-libs.txt"
: > "$NEEDED"
collect() {
  local elf="$1"
  LD_LIBRARY_PATH="$QT_DIR/lib:$WORK/qtkeychain-install/lib" ldd "$elf" 2>/dev/null \
    | awk '{print $3}' | while read -r p; do
      for d in "${LIBSRC_DIRS[@]}"; do
        case "$p" in "$d"/*) echo "$p" ;; esac
      done
    done
}
collect "$OPT/bin/vindauga" >> "$NEEDED"
collect "$OPT/libexec/QtWebEngineProcess" >> "$NEEDED"
while IFS= read -r -d '' f; do collect "$f" >> "$NEEDED"; done < <(find "$OPT/plugins" "$OPT/qml" -name '*.so' -print0)
sort -u "$NEEDED" -o "$NEEDED"
while IFS= read -r lib; do
  [ -n "$lib" ] || continue
  # -L: dereference the symlink chain. Many Qt libs are libX.so.N -> libX.so.N.N.N, a
  # RELATIVE symlink that would dangle if only the link were copied into a new directory
  # without its target.
  cp -aL "$lib" "$OPT/lib/" 2>/dev/null || true
done < "$NEEDED"
echo "  -> $(find "$OPT/lib" \( -type f -o -type l \) | wc -l) bundled libraries ($(du -sh "$OPT/lib" | cut -f1))"

echo "== [6/7] Wrapper script + .desktop file =="
cat > "$PKGROOT/usr/bin/vindauga" <<'WRAPPER'
#!/bin/sh
# A wrapper script is used instead of patching RPATH with patchelf: it is easier to read
# and debug than binary RPATH manipulation, and avoids a build-time dependency on patchelf.
HERE=/opt/vindauga
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/plugins"
export QML2_IMPORT_PATH="$HERE/qml"
export QML_IMPORT_PATH="$HERE/qml"
export QTWEBENGINEPROCESS_PATH="$HERE/libexec/QtWebEngineProcess"
export QTWEBENGINE_RESOURCES_PATH="$HERE/resources"
export QTWEBENGINE_LOCALES_PATH="$HERE/translations/qtwebengine_locales"
exec "$HERE/bin/vindauga" "$@"
WRAPPER
chmod 755 "$PKGROOT/usr/bin/vindauga"
install -m644 "$REPO_ROOT/packaging/vindauga.desktop" "$PKGROOT/usr/share/applications/vindauga.desktop"
# Application icon in the hicolor theme (same set as the CMake install() rules in src/app).
for size in 16 22 24 32 48 64 128 256 512; do
  install -Dm644 "$REPO_ROOT/packaging/icons/hicolor/${size}x${size}/apps/vindauga.png" \
    "$PKGROOT/usr/share/icons/hicolor/${size}x${size}/apps/vindauga.png"
done

echo "== [7/7] DEBIAN/control + dpkg-deb --build =="
INSTALLED_SIZE_KB=$(du -sk "$PKGROOT" | cut -f1)
# Only the base libs are hardcoded; the FreeRDP runtime packages (libfreerdp3-3,
# libfreerdp-client3-3, libwinpr3-3) and everything else are resolved by dpkg-shlibdeps
# below with proper version constraints. libsecret-1-0 must be listed explicitly:
# qtkeychain (LIBSECRET_SUPPORT=ON above) dlopen()s libsecret-1.so.0 at runtime instead of
# linking it, so there is no NEEDED entry for dpkg-shlibdeps to discover, and without the
# package installed the secret store fails silently.
DEPENDS="libc6, libstdc++6, libsecret-1-0"
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
  # dpkg-shlibdeps analyses the actual .so requirements on the NON-bundled system libs
  # (X11, fontconfig, and the GTK/audio/codec chain QtWebEngine pulls in) and resolves the
  # correct apt package names for this build machine. It only works on a real Debian/Ubuntu
  # system, which is why the deb-package job in release.yml runs directly on an
  # ubuntu-24.04 runner rather than in a container.
  #
  # -lopt/vindauga/lib tells dpkg-shlibdeps where the BUNDLED Qt/qtkeychain libs live.
  # Without it, resolution fails with "cannot find library libQt6NetworkAuth.so.6 needed by
  # opt/vindauga/bin/vindauga". The step fails loudly on an empty result: an empty Depends
  # list on a Debian machine is always a bug.
  mkdir -p "$PKGROOT/debian"
  touch "$PKGROOT/debian/control"
  # debian/shlibs.local declares every bundled lib (libQt6Core 6, libicuuc 73, ...) as
  # provided by this package itself. Without it, dpkg-shlibdeps falls through to the next
  # match for the same soname, i.e. the distro's own Qt packages (libqt6core6t64,
  # qt6-base-abi ...) if the build machine happens to have them installed, producing a
  # large, version-locked and unnecessary dependency. The resulting self-dependency on
  # "vindauga" is removed with -xvindauga (safer than a sed strip, which could also hit
  # other "vindauga-*" package names).
  for f in "$OPT"/lib/*.so.*; do
    so=$(objdump -p "$f" 2>/dev/null | awk '/SONAME/{print $2}')
    [ -n "$so" ] || continue
    echo "${so%%.so*} ${so#*.so.} vindauga"
  done | sort -u > "$PKGROOT/debian/shlibs.local"
  # opt/vindauga/lib is ALSO passed as analysis objects: dpkg-shlibdeps does not follow
  # NEEDED recursively, so without them everything the bundled libs themselves need from
  # the system (libnss3/libasound2/libdbus-1/libfontconfig1 for WebEngineCore etc.) would
  # be missing from Depends.
  # stderr: --ignore-missing-info emits one "no dependency information found for
  # opt/vindauga/lib/..." warning per bundled lib per binary; these are filtered so that
  # real errors ("cannot find library ...") are not drowned out in the CI log.
  ( cd "$PKGROOT" && dpkg-shlibdeps --ignore-missing-info -lopt/vindauga/lib -xvindauga -O \
      opt/vindauga/bin/vindauga opt/vindauga/libexec/QtWebEngineProcess \
      $(find opt/vindauga/plugins opt/vindauga/qml opt/vindauga/lib -name '*.so*' -type f) \
      2> >(grep -v 'no dependency information found for opt/vindauga/lib/\|useless dependency\|binaries to analyze should already be installed\|\$ORIGIN is used in RPATH' >&2) \
      | sed -n 's/^shlibs:Depends=//p' > "$WORK/shlibdeps.txt" )
  rm -rf "$PKGROOT/debian"
  if [ ! -s "$WORK/shlibdeps.txt" ]; then
    echo "ERROR: dpkg-shlibdeps produced no Depends list; the package would be missing all system dependencies (see the errors above)." >&2
    exit 1
  fi
  DEPENDS="$DEPENDS, $(cat "$WORK/shlibdeps.txt")"
  echo "  -> Depends: $DEPENDS"
else
  echo "  WARNING: dpkg-shlibdeps not found (expected on non-Debian systems). Do not use this step locally for a distributable result; see release.yml for the CI variant." >&2
fi

cat > "$PKGROOT/DEBIAN/control" <<CONTROL
Package: vindauga
Version: $PKGVER
Section: net
Priority: optional
Architecture: amd64
Installed-Size: $INSTALLED_SIZE_KB
Maintainer: Karl Løland <karl@folia.no>
Depends: $DEPENDS
Recommends: gnome-keyring | kwalletmanager
Description: Native Linux client for Azure Virtual Desktop / Windows 365
 Vindauga is a native RDP client for Azure Virtual Desktop / Windows 365,
 built against a bundled Qt 6.11.2 (the distribution's own Qt is too old).
CONTROL

OUT="$REPO_ROOT/packaging/deb/vindauga_${PKGVER}_amd64.deb"
if command -v dpkg-deb >/dev/null 2>&1; then
  dpkg-deb --build --root-owner-group "$PKGROOT" "$OUT"
  echo "== Done: $OUT =="
else
  echo "  WARNING: dpkg-deb not found; the package tree is left in $PKGROOT (removed on script exit, copy it out to inspect)." >&2
  echo "  Package tree ready for inspection: $PKGROOT"
  read -r -p "  Press Enter to clean up (or Ctrl-C to keep $WORK) ..." _ || true
fi
