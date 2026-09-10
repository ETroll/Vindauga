#!/usr/bin/env bash
# Install development dependencies.
# Run from anywhere:  ./scripts/setup-deps.sh
set -euo pipefail

os_id=""
[[ -r /etc/os-release ]] && os_id="$(. /etc/os-release && echo "${ID} ${ID_LIKE:-}")"

case "$os_id" in
  *arch*)
    echo ">> Arch Linux detected. Installing via pacman ..."
    sudo pacman -S --needed qt6-base qt6-declarative qt6-networkauth qt6-shadertools \
                            freerdp cmake ninja pkgconf gcc
    echo ">> Done. (qtkeychain for Qt6 is also required — check: pacman -Ss keychain)"
    ;;
  *debian*|*ubuntu*)
    echo ">> Debian/Ubuntu detected. Installing build tools, FreeRDP and Qt system libraries via apt ..."
    sudo apt update
    # Build tools, FreeRDP and the system libraries an aqt-installed Qt depends on
    # (OpenGL/xcb/xkb; otherwise find_package(Qt6 ... Gui/Quick) fails).
    sudo apt install -y build-essential cmake ninja-build pkg-config \
                        freerdp3-dev freerdp3-x11 \
                        libgl1-mesa-dev libglvnd-dev libegl1-mesa-dev libglu1-mesa-dev \
                        libxkbcommon-dev libxcb-cursor0
    echo
    echo "!! IMPORTANT: the apt Qt6 is too old (6.4) and lacks QtNetworkAuth."
    echo "!! Install Qt 6.9 alongside it (if you have not already):"
    echo "     pipx install aqtinstall"
    echo "     aqt install-qt linux desktop 6.9.1 linux_gcc_64 -m qtnetworkauth qtshadertools -O \$HOME/Qt"
    echo
    echo "   Let CMake find it without passing flags every time by adding to ~/.bashrc:"
    echo "     export CMAKE_PREFIX_PATH=\"\$HOME/Qt/6.9.1/gcc_64\""
    echo "   Then:  cmake --preset debug   (or plain cmake -B build -G Ninja)"
    ;;
  *)
    echo "Unknown distro ($os_id). Install the dependencies manually."
    exit 1
    ;;
esac
