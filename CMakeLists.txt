cmake_minimum_required(VERSION 3.20)
project(wallpaper_splitter VERSION 1.1.4 LANGUAGES CXX)

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

qt_add_executable(wallpaper_splitter
        src/main.cpp
        src/wallpapersplitter.cpp
        src/wallpapersplitter.ui
        src/screensitem.cpp
        src/graphicsview.cpp
        src/centeredtextitem.cpp
        src/resizableimageitem.cpp
)

target_link_libraries(wallpaper_splitter PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Widgets
        KF6::ConfigWidgets
)

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

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    qt_add_executable(splitimage_test
            tests/splitimage_test.cpp
            src/wallpapersplitter.cpp
            src/wallpapersplitter.ui
            src/screensitem.cpp
            src/graphicsview.cpp
            src/centeredtextitem.cpp
            src/resizableimageitem.cpp)
    target_include_directories(splitimage_test PRIVATE src)
    target_link_libraries(splitimage_test PRIVATE
            Qt6::Core
            Qt6::DBus
            Qt6::Gui
            Qt6::Test
            Qt6::Widgets
            KF6::ConfigWidgets)
    add_test(NAME splitimage_test COMMAND splitimage_test)
    set_tests_properties(splitimage_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
