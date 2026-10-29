cmake_minimum_required(VERSION 3.16)
project(qt-eglfs-fbdev LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui)
find_package(Qt6EglFSDeviceIntegrationPrivate REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GBM REQUIRED gbm)

qt_add_plugin(qeglfs-fbdev-integration
    CLASS_NAME QEglFSFbdevIntegrationPlugin
    PLUGIN_TYPE egldeviceintegrations
)

target_sources(qeglfs-fbdev-integration PRIVATE
    qeglfsfbdevmain.cpp
    qeglfsfbdevintegration.cpp
    qeglfsfbdevintegration.h
)

target_include_directories(qeglfs-fbdev-integration PRIVATE
    ${GBM_INCLUDE_DIRS}
)

# The DBC image is headless: make Qt select Mesa's non-X11 EGL types.
target_compile_definitions(qeglfs-fbdev-integration PRIVATE QT_EGL_NO_X11)

target_link_libraries(qeglfs-fbdev-integration PRIVATE
    Qt6::Core
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::EglFSDeviceIntegrationPrivate
    ${GBM_LIBRARIES}
    EGL
)

if(NOT QT6_INSTALL_PLUGINS)
    set(QT6_INSTALL_PLUGINS "lib/qt6/plugins")
endif()

install(TARGETS qeglfs-fbdev-integration
    LIBRARY DESTINATION ${QT6_INSTALL_PLUGINS}/egldeviceintegrations
)

option(BUILD_TESTING "Build surface format and Qt Quick rendering tests" OFF)
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Quick Test)
    add_executable(surface-format-test
        tests/SurfaceFormatTest.cpp
        qeglfsfbdevintegration.cpp
    )
    target_include_directories(surface-format-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${GBM_INCLUDE_DIRS}
    )
    target_compile_definitions(surface-format-test PRIVATE QT_EGL_NO_X11)
    target_link_libraries(surface-format-test PRIVATE
        Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::GuiPrivate
        Qt6::Quick Qt6::Test Qt6::EglFSDeviceIntegrationPrivate
        ${GBM_LIBRARIES} EGL
    )
    add_test(NAME surface-format COMMAND surface-format-test)
    set_tests_properties(surface-format PROPERTIES TIMEOUT 60)
endif()
