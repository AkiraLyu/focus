cmake_minimum_required(VERSION 3.24)
project(focus VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk4>=4.16 libadwaita-1>=1.6)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(CMARK REQUIRED IMPORTED_TARGET libcmark-gfm)
pkg_check_modules(DBUSMENU REQUIRED IMPORTED_TARGET dbusmenu-glib-0.4)
pkg_check_modules(CANBERRA REQUIRED IMPORTED_TARGET libcanberra)
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)

add_library(focus_core src/timer.cpp src/store.cpp src/focus_timer.cpp)
target_include_directories(focus_core PUBLIC src)
target_link_libraries(focus_core PUBLIC PkgConfig::SQLITE PkgConfig::GLIB)
target_compile_options(focus_core PRIVATE -Wall -Wextra -Wpedantic)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/resources.c
    COMMAND ${GLIB_COMPILE_RESOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/data/focus.gresource.xml
        --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/data
        --target=${CMAKE_CURRENT_BINARY_DIR}/resources.c --generate-source
    DEPENDS data/focus.gresource.xml data/style.css data/status-notifier.xml data/io.github.focus.Timer.svg)
add_library(focus_ui OBJECT src/application.cpp src/ui.cpp src/focus_page.cpp
    src/tasks_page.cpp src/notes_page.cpp src/stats_page.cpp src/markdown.cpp src/tray.cpp src/event_sound.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/resources.c)
target_link_libraries(focus_ui PUBLIC focus_core PkgConfig::GTK PkgConfig::CMARK PkgConfig::DBUSMENU PkgConfig::CANBERRA)
target_compile_options(focus_ui PRIVATE -Wall -Wextra -Wpedantic)
add_executable(focus src/main.cpp)
target_link_libraries(focus PRIVATE focus_ui)
target_compile_options(focus PRIVATE -Wall -Wextra -Wpedantic)

include(GNUInstallDirs)
install(TARGETS focus RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/io.github.focus.Timer.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/io.github.focus.Timer.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

include(CTest)
option(FOCUS_DESKTOP_TESTS "Enable tests that create desktop windows" OFF)
if(BUILD_TESTING)
    add_executable(core_test tests/core_test.cpp)
    target_link_libraries(core_test PRIVATE focus_core)
    add_test(NAME core_behavior COMMAND core_test)
    if(FOCUS_DESKTOP_TESTS)
        add_executable(background_test tests/background_test.cpp)
        target_link_libraries(background_test PRIVATE focus_ui)
        target_compile_options(background_test PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME background_lifecycle COMMAND background_test)
        set_tests_properties(background_lifecycle PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30 LABELS desktop)
        add_executable(tray_test tests/tray_test.cpp)
        target_link_libraries(tray_test PRIVATE focus_ui)
        target_compile_options(tray_test PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME tray_activation COMMAND tray_test)
        set_tests_properties(tray_activation PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30 LABELS desktop)
    endif()
endif()
