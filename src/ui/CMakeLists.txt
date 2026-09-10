# UI layer: Qt Quick / QML. QML module URI "Vindauga"; C++ types are exposed to QML
# via QML_ELEMENT.
qt_add_library(vindauga_ui STATIC)

# Qt6::NetworkAuth comes transitively via vindauga_core.
target_link_libraries(vindauga_ui PUBLIC
    vindauga_core
    Qt6::Quick Qt6::Qml Qt6::QuickControls2
)
target_include_directories(vindauga_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backend)
target_compile_definitions(vindauga_ui PUBLIC VINDAUGA_VERSION="${PROJECT_VERSION}")

set(VINDAUGA_UI_SOURCES
    backend/AppController.h
    backend/AppController.cpp
    backend/ConnectionListModel.h
    backend/ConnectionListModel.cpp
)

# AadWebAuthenticator needs Qt6::WebEngineQuick; these sources are only built when
# VINDAUGA_WITH_RDP is ON.
if(VINDAUGA_WITH_RDP)
    list(APPEND VINDAUGA_UI_SOURCES
        backend/AadWebAuthenticator.h
        backend/AadWebAuthenticator.cpp
        backend/RdstlsCredentialDialog.h
        backend/RdstlsCredentialDialog.cpp
        backend/RdpItem.h
        backend/RdpItem.cpp
        backend/RdpRenderNode.h
        backend/RdpRenderNode.cpp
        backend/X11KeymapGuard.h
        backend/X11KeymapGuard.cpp
    )
    target_link_libraries(vindauga_ui PUBLIC Qt6::WebEngineQuick)
    # X11KeymapGuard: libxkbcommon (the same library Chromium compiles keymaps with) and
    # libxcb (to read/write _XKB_RULES_NAMES on the root window). Both are already hard
    # runtime dependencies of Qt Gui / the xcb plugin; only the build-time headers are
    # new. PkgConfig is found in the top-level CMakeLists.
    pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
    target_link_libraries(vindauga_ui PRIVATE PkgConfig::XKBCOMMON PkgConfig::XCB)
    # RdpRenderNode uses QRhi/QSGRenderNode directly for partial GPU texture uploads;
    # <rhi/qrhi.h> requires Qt6::GuiPrivate.
    target_link_libraries(vindauga_ui PUBLIC Qt6::GuiPrivate)
endif()

# SessionPage.qml uses RdpItem, which only exists with VINDAUGA_WITH_RDP. That is the
# top-level default, and the rest of the GUI makes no sense without RDP support, so
# there is no conditional branch here.
qt_add_qml_module(vindauga_ui
    URI Vindauga
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/ConnectionsPage.qml
        qml/SessionPage.qml
        qml/SettingsPage.qml
    SOURCES
        ${VINDAUGA_UI_SOURCES}
)

# Compiles rdprender.vert/.frag to portable .qsb bytecode with the same GLSL target
# versions ("300 es,130") as Qt's "Scene Graph - Custom QSGRenderNode" example. BASE is
# set so the resource path becomes ":/vindauga/shaders/rdprender.vert.qsb" rather than
# repeating the "backend/shaders/" prefix.
if(VINDAUGA_WITH_RDP)
    qt6_add_shaders(vindauga_ui "rdp_shaders"
        GLSL "300 es,130"
        PREFIX "/vindauga/shaders"
        BASE "backend/shaders"
        FILES
            "backend/shaders/rdprender.vert"
            "backend/shaders/rdprender.frag"
    )
endif()
