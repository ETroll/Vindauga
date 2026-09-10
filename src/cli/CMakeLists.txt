# Headless CLI; depends on core/ only.
qt_add_executable(vindauga_cli vindauga_cli.cpp)
target_link_libraries(vindauga_cli PRIVATE vindauga_core Qt6::Core)

# The rdp-connect-test command uses ui/backend/AadWebAuthenticator (QtWebEngine-based
# AAD capture), so it links vindauga_ui when RDP support is enabled.
if(VINDAUGA_WITH_RDP)
    target_link_libraries(vindauga_cli PRIVATE vindauga_ui)
endif()
