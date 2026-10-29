cmake_minimum_required(VERSION 3.20)
project(wallpaper_splitter VERSION 1.4.1 LANGUAGES CXX)

include(GNUInstallDirs)

set(WALLPAPER_SPLITTER_APP_ID "io.github.kstorbakken.wallpaper_splitter")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# necessary for JetBrain Fleet
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS
        Core
        DBus
        Gui
        Widgets)

qt_standard_project_setup()

find_package(KF6ConfigWidgets REQUIRED)
find_package(KF6Config REQUIRED)

# Compile application code once and share it with both regression executables.
add_library(wallpaper_splitter_core STATIC
        src/wallpapersplitter.cpp
        src/wallpapersplitter.ui
        src/screensitem.cpp
        src/graphicsview.cpp
        src/centeredtextitem.cpp
        src/resizableimageitem.cpp
        src/outputservice.cpp
        src/wallpaperpreview.cpp
        src/batchsplitter.cpp
        src/appsettings.cpp
        src/settingsdialog.cpp
        src/setlibrary.cpp
        src/setlibrarydialog.cpp
        src/slideshow.cpp
        src/slideshowdialog.cpp
        src/monitorlayout.cpp
        src/monitorsdialog.cpp
)

target_include_directories(wallpaper_splitter_core PUBLIC src)
target_link_libraries(wallpaper_splitter_core PUBLIC
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Widgets
        KF6::ConfigWidgets
        KF6::ConfigCore
)

qt_add_executable(wallpaper_splitter src/main.cpp)
target_link_libraries(wallpaper_splitter PRIVATE wallpaper_splitter_core)

target_compile_definitions(wallpaper_splitter PRIVATE
        WALLPAPER_SPLITTER_APP_ID="${WALLPAPER_SPLITTER_APP_ID}"
        WALLPAPER_SPLITTER_VERSION="${PROJECT_VERSION}")

install(TARGETS wallpaper_splitter RUNTIME DESTINATION bin)

# Install metadata
install(FILES assets/icon.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME ${WALLPAPER_SPLITTER_APP_ID}.svg)

install(FILES assets/metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
    RENAME ${WALLPAPER_SPLITTER_APP_ID}.metainfo.xml)

install(FILES assets/wallpaper_splitter.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    RENAME ${WALLPAPER_SPLITTER_APP_ID}.desktop)

# Native release packages. Keep this metadata here so local CPack builds and
# release CI produce the same artifacts.
set(CPACK_PACKAGE_NAME "wallpaper-splitter")
set(CPACK_PACKAGE_VENDOR "Wallpaper Splitter contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Split and span a wallpaper across multiple screens on KDE Plasma")
set(CPACK_PACKAGE_HOMEPAGE_URL
    "https://github.com/kstorbakken/wallpaper_splitter")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT
    "Wallpaper Splitter contributors <kstorbakken@users.noreply.github.com>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")

set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-image-formats-plugins, qt6-wayland")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-only")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtimageformats, qt6-qtwayland")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

include(CPack)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test Qml)

    foreach(test_name IN ITEMS splitimage slideshow)
        qt_add_executable(${test_name}_test tests/${test_name}_test.cpp)
        target_link_libraries(${test_name}_test PRIVATE wallpaper_splitter_core Qt6::Test)
        # Both suites exercise the real CLI, including targeted test builds.
        add_dependencies(${test_name}_test wallpaper_splitter)
        add_test(NAME ${test_name}_test COMMAND ${test_name}_test)
        set_tests_properties(${test_name}_test PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config/${test_name}")
    endforeach()
    target_link_libraries(splitimage_test PRIVATE Qt6::Qml)
endif()
