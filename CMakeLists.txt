cmake_minimum_required(VERSION 3.24)
project(focus VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(PkgConfig REQUIRED)
find_package(Intl REQUIRED)
find_package(Gettext REQUIRED)
include(GNUInstallDirs)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk4>=4.16 libadwaita-1>=1.6)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(CMARK REQUIRED IMPORTED_TARGET libcmark-gfm)
pkg_check_modules(DBUSMENU REQUIRED IMPORTED_TARGET dbusmenu-glib-0.4)
pkg_check_modules(CANBERRA REQUIRED IMPORTED_TARGET libcanberra)
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)

configure_file(src/i18n_config.hpp.in i18n_config.hpp @ONLY)
set(FOCUS_TRANSLATION ${CMAKE_CURRENT_BINARY_DIR}/locale/zh_CN/LC_MESSAGES/focus.mo)
add_custom_command(OUTPUT ${FOCUS_TRANSLATION}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/locale/zh_CN/LC_MESSAGES
    COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --check --check-format
        -o ${FOCUS_TRANSLATION} ${CMAKE_CURRENT_SOURCE_DIR}/po/zh_CN.po
    DEPENDS po/zh_CN.po VERBATIM)
add_custom_target(translations ALL DEPENDS ${FOCUS_TRANSLATION})
install(FILES ${FOCUS_TRANSLATION} DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/zh_CN/LC_MESSAGES)
find_program(GETTEXT_XGETTEXT_EXECUTABLE xgettext)
if(GETTEXT_XGETTEXT_EXECUTABLE)
    file(GLOB FOCUS_TRANSLATION_SOURCES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp)
    add_custom_target(update-pot
        COMMAND ${GETTEXT_XGETTEXT_EXECUTABLE} --language=C++ --from-code=UTF-8
            --keyword=_ --keyword=P_:1,2 --add-comments=Translators
            --package-name=focus --package-version=${PROJECT_VERSION}
            "--copyright-holder=Focus contributors"
            --output=${CMAKE_CURRENT_BINARY_DIR}/focus.pot ${FOCUS_TRANSLATION_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM)
endif()

add_library(focus_core src/timer.cpp src/store.cpp src/focus_timer.cpp src/i18n.cpp)
add_dependencies(focus_core translations)
target_include_directories(focus_core PUBLIC src)
target_include_directories(focus_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(focus_core PUBLIC GETTEXT_PACKAGE="focus")
target_link_libraries(focus_core PUBLIC PkgConfig::SQLITE PkgConfig::GLIB Intl::Intl)
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

install(TARGETS focus RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/io.github.focus.Timer.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/io.github.focus.Timer.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

include(CTest)
option(FOCUS_DESKTOP_TESTS "Enable tests that create desktop windows" OFF)
if(BUILD_TESTING)
    # gettext may intentionally disable translations for C.UTF-8. Generate a
    # private locale so translation tests also work in a minimal build chroot.
    find_program(LOCALEDEF_EXECUTABLE localedef REQUIRED)
    set(FOCUS_TEST_LOCALES ${CMAKE_CURRENT_BINARY_DIR}/test-locales)
    set(FOCUS_TEST_LOCALE ${FOCUS_TEST_LOCALES}/en_US.UTF-8/LC_MESSAGES/SYS_LC_MESSAGES)
    add_custom_command(OUTPUT ${FOCUS_TEST_LOCALE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${FOCUS_TEST_LOCALES}
        COMMAND ${LOCALEDEF_EXECUTABLE} --no-archive -i en_US -f UTF-8 ${FOCUS_TEST_LOCALES}/en_US.UTF-8
        VERBATIM)
    add_custom_target(test-locales DEPENDS ${FOCUS_TEST_LOCALE})
    add_executable(core_test tests/core_test.cpp)
    add_dependencies(core_test test-locales)
    target_link_libraries(core_test PRIVATE focus_core)
    add_test(NAME core_behavior COMMAND core_test)
    set_tests_properties(core_behavior PROPERTIES ENVIRONMENT "LANGUAGE=zh_CN;LC_ALL=en_US.UTF-8;LOCPATH=${FOCUS_TEST_LOCALES}")
    add_executable(i18n_test tests/i18n_test.cpp)
    add_dependencies(i18n_test test-locales)
    target_link_libraries(i18n_test PRIVATE focus_core)
    target_compile_options(i18n_test PRIVATE -Wall -Wextra -Wpedantic)
    foreach(language IN ITEMS en zh_CN fr)
        add_test(NAME i18n_${language} COMMAND i18n_test ${language})
        set_tests_properties(i18n_${language} PROPERTIES ENVIRONMENT "LANGUAGE=${language};LC_ALL=en_US.UTF-8;LOCPATH=${FOCUS_TEST_LOCALES}")
    endforeach()
    if(FOCUS_DESKTOP_TESTS)
        add_executable(background_test tests/background_test.cpp)
        target_link_libraries(background_test PRIVATE focus_ui)
        target_compile_options(background_test PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME background_lifecycle COMMAND background_test)
        set_tests_properties(background_lifecycle PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30 LABELS desktop)
        add_executable(tray_test tests/tray_test.cpp)
        add_dependencies(tray_test test-locales)
        target_link_libraries(tray_test PRIVATE focus_ui)
        target_compile_options(tray_test PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME tray_activation COMMAND tray_test)
        set_tests_properties(tray_activation PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30 LABELS desktop)
        set_tests_properties(tray_activation PROPERTIES ENVIRONMENT "LANGUAGE=zh_CN;LC_ALL=en_US.UTF-8;LOCPATH=${FOCUS_TEST_LOCALES}")
    endif()
endif()
