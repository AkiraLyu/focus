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
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)

add_library(focus_core src/timer.cpp src/store.cpp)
target_include_directories(focus_core PUBLIC src)
target_link_libraries(focus_core PUBLIC PkgConfig::SQLITE PkgConfig::GLIB)
target_compile_options(focus_core PRIVATE -Wall -Wextra -Wpedantic)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/resources.c
    COMMAND ${GLIB_COMPILE_RESOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/data/focus.gresource.xml
        --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/data
        --target=${CMAKE_CURRENT_BINARY_DIR}/resources.c --generate-source
    DEPENDS data/focus.gresource.xml data/style.css data/io.github.focus.Timer.svg)
add_executable(focus src/main.cpp src/application.cpp src/ui.cpp src/focus_page.cpp
    src/tasks_page.cpp src/notes_page.cpp src/stats_page.cpp src/markdown.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/resources.c)
target_link_libraries(focus PRIVATE focus_core PkgConfig::GTK PkgConfig::CMARK)
target_compile_options(focus PRIVATE -Wall -Wextra -Wpedantic)

include(GNUInstallDirs)
install(TARGETS focus RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/io.github.focus.Timer.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/io.github.focus.Timer.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

include(CTest)
if(BUILD_TESTING)
    add_executable(core_test tests/core_test.cpp)
    target_link_libraries(core_test PRIVATE focus_core)
    add_test(NAME core_behavior COMMAND core_test)
endif()
