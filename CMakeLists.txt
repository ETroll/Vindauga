cmake_minimum_required(VERSION 3.21)
project(Vindauga VERSION 1.2.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Packaging needs the standard install directories (bin/, share/applications/, ...).
include(GNUInstallDirs)

option(VINDAUGA_WITH_RDP "Link libfreerdp for RDP session support" ON)

# Qt 6.9 is the real minimum: QOAuth2DeviceAuthorizationFlow (used by AuthManager) first
# appeared in 6.9. Checking here gives a clear CMake error instead of a confusing
# missing-header compile error.
find_package(Qt6 6.9 REQUIRED COMPONENTS Core Network Quick Qml QuickControls2 NetworkAuth Test)
qt_standard_project_setup(REQUIRES 6.9)

# Secure storage of RDSTLS credentials per saved connection (CredentialStore).
# Package: qtkeychain-qt6.
find_package(Qt6Keychain REQUIRED)

if(VINDAUGA_WITH_RDP)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FREERDP REQUIRED IMPORTED_TARGET freerdp3 freerdp-client3 winpr3)
    # Embedded web view for capturing the AAD redirect automatically.
    find_package(Qt6 6.9 REQUIRED COMPONENTS WebEngineQuick)
    # RdpRenderNode uses QRhi (GuiPrivate must be requested as an explicit component for
    # CMake to generate the Qt6::GuiPrivate target) and compiled .qsb shaders
    # (qt6_add_shaders from ShaderTools) for partial GPU texture uploads.
    find_package(Qt6 6.9 REQUIRED COMPONENTS GuiPrivate ShaderTools)
endif()

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/cli)
add_subdirectory(src/app)
add_subdirectory(tests)
