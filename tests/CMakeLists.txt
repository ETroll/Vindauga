qt_add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE vindauga_core Qt6::Test Qt6::Core)
add_test(NAME smoke COMMAND test_smoke)

qt_add_executable(test_feedparser test_feedparser.cpp)
target_link_libraries(test_feedparser PRIVATE vindauga_core Qt6::Test Qt6::Core)
target_compile_definitions(test_feedparser PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME feedparser COMMAND test_feedparser)

qt_add_executable(test_connectionstore test_connectionstore.cpp)
target_link_libraries(test_connectionstore PRIVATE vindauga_core Qt6::Test Qt6::Core)
target_compile_definitions(test_connectionstore PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME connectionstore COMMAND test_connectionstore)

if(VINDAUGA_WITH_RDP)
    qt_add_executable(test_rdpsession test_rdpsession.cpp)
    target_link_libraries(test_rdpsession PRIVATE vindauga_core Qt6::Test Qt6::Core)
    target_compile_definitions(test_rdpsession PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME rdpsession COMMAND test_rdpsession)

    # Tests AadWebAuthenticator against a locally simulated redirect; no real network
    # or MFA. Runs on the offscreen platform since there is no visible window.
    qt_add_executable(test_aadwebauthenticator test_aadwebauthenticator.cpp)
    target_link_libraries(test_aadwebauthenticator PRIVATE vindauga_ui Qt6::Test Qt6::Core)
    add_test(NAME aadwebauthenticator COMMAND test_aadwebauthenticator)

    # Pure logic (parse/compile/repair); no X server, but needs xkeyboard-config in
    # /usr/share/X11/xkb (skipped otherwise).
    qt_add_executable(test_x11keymapguard test_x11keymapguard.cpp)
    target_link_libraries(test_x11keymapguard PRIVATE vindauga_ui Qt6::Test Qt6::Core)
    add_test(NAME x11keymapguard COMMAND test_x11keymapguard)
    set_tests_properties(aadwebauthenticator PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Verifies only that RdpRenderNode's compiled .qsb shader resources exist and are
    # valid; the QRhi pipeline itself requires a GPU-backed session.
    qt_add_executable(test_rdprendernode test_rdprendernode.cpp)
    target_link_libraries(test_rdprendernode PRIVATE vindauga_ui Qt6::Test Qt6::Core Qt6::GuiPrivate)
    add_test(NAME rdprendernode COMMAND test_rdprendernode)
    set_tests_properties(rdprendernode PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
