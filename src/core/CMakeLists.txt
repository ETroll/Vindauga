# Core logic: no Qt Quick/GUI dependency.
set(VINDAUGA_CORE_SOURCES
    Logging.h
    Logging.cpp
    Result.h
    Models.h
    Feed/FeedParser.h
    Feed/FeedParser.cpp
    Auth/AuthManager.h
    Auth/AuthManager.cpp
    Auth/CredentialStore.h
    Auth/CredentialStore.cpp
    Connections/ConnectionStore.h
    Connections/ConnectionStore.cpp
    Settings/AppSettings.h
    Settings/AppSettings.cpp
)

# RdpSession needs libfreerdp and is only built when VINDAUGA_WITH_RDP is ON.
if(VINDAUGA_WITH_RDP)
    list(APPEND VINDAUGA_CORE_SOURCES
        Rdp/RdpSession.h
        Rdp/RdpSession.cpp
        Rdp/AadInteractiveAuth.h
        Rdp/RdstlsCredentialPrompt.h
        Rdp/VaapiDeviceSelector.h
        Rdp/VaapiDeviceSelector.cpp
    )
endif()

qt_add_library(vindauga_core STATIC ${VINDAUGA_CORE_SOURCES})

# NetworkAuth is used by AuthManager and Qt6Keychain by CredentialStore; neither pulls in
# Qt Quick/GUI. QDesktopServices is deliberately avoided (see AuthManager.cpp).
target_link_libraries(vindauga_core PUBLIC Qt6::Core Qt6::Network Qt6::NetworkAuth Qt6Keychain::Qt6Keychain)
target_include_directories(vindauga_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vindauga_core PUBLIC VINDAUGA_VERSION="${PROJECT_VERSION}")

if(VINDAUGA_WITH_RDP)
    target_link_libraries(vindauga_core PUBLIC PkgConfig::FREERDP)
    target_compile_definitions(vindauga_core PUBLIC VINDAUGA_WITH_RDP)
    # Qt6::Gui is needed only for QImage (RdpSession::frameReady). It is headless-friendly;
    # the "no Qt Quick/GUI" rule for core/ targets Qt Quick/QML/Widgets.
    target_link_libraries(vindauga_core PUBLIC Qt6::Gui)
endif()
