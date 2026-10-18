# Core logic: no Qt Quick/GUI dependency.
set(VINDAUGA_CORE_SOURCES
    Logging.h
    Logging.cpp
    Result.h
    Models.h
    Feed/FeedParser.h
    Feed/FeedParser.cpp
    Feed/FeedClient.h
    Feed/FeedClient.cpp
    Feed/FeedCache.h
    Feed/FeedCache.cpp
    Auth/AadTokenProvider.h
    Auth/AuthManager.h
    Auth/AuthManager.cpp
    Auth/CredentialStore.h
    Auth/CredentialStore.cpp
    Connections/ConnectionStore.h
    Connections/ConnectionStore.cpp
    Settings/AppSettings.h
    Settings/AppSettings.cpp
    Rdp/PressedInputTracker.h
)

# RdpSession needs libfreerdp and is only built when VINDAUGA_WITH_RDP is ON.
if(VINDAUGA_WITH_RDP)
    list(APPEND VINDAUGA_CORE_SOURCES
        Rdp/RdpSession.h
        Rdp/RdpSession.cpp
        Rdp/AadInteractiveAuth.h
        Rdp/CertificatePrompt.h
        Rdp/RdstlsCredentialPrompt.h
        Rdp/VaapiDeviceSelector.h
        Rdp/VaapiDeviceSelector.cpp
    )
endif()

qt_add_library(vindauga_core STATIC ${VINDAUGA_CORE_SOURCES})

# Qt6Keychain is used by CredentialStore; it does not pull in Qt Quick/GUI. AuthManager
# is plain QNetworkAccessManager (auth-code + PKCE with the redirect captured by a web
# view), so Qt6::NetworkAuth is not needed.
target_link_libraries(vindauga_core PUBLIC Qt6::Core Qt6::Network Qt6Keychain::Qt6Keychain)
target_include_directories(vindauga_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vindauga_core PUBLIC VINDAUGA_VERSION="${PROJECT_VERSION}")

if(VINDAUGA_WITH_RDP)
    target_link_libraries(vindauga_core PUBLIC PkgConfig::FREERDP)
    target_compile_definitions(vindauga_core PUBLIC VINDAUGA_WITH_RDP)
    # Qt6::Gui is needed only for QImage (RdpSession::frameReady). It is headless-friendly;
    # the "no Qt Quick/GUI" rule for core/ targets Qt Quick/QML/Widgets.
    target_link_libraries(vindauga_core PUBLIC Qt6::Gui)
endif()
