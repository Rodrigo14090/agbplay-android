cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(FindPkgConfig)
find_package(PkgConfig REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)


file(GLOB_RECURSE AGBPLAY_GUI_SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/../agbplay-common/*.cpp")
file(GLOB_RECURSE AGBPLAY_GUI_HEADERS "${CMAKE_CURRENT_LIST_DIR}/*.h" "${CMAKE_CURRENT_LIST_DIR}/*.hpp")
file(GLOB_RECURSE AGBPLAY_GUI_UIS "${CMAKE_CURRENT_LIST_DIR}/*.ui")
file(GLOB_RECURSE AGBPLAY_GUI_QRC "${CMAKE_CURRENT_LIST_DIR}/*.qrc")
if(WIN32)
    FILE(GLOB_RECURSE AGBPLAY_GUI_WIN32_RC "${CMAKE_CURRENT_LIST_DIR}/*.rc")
endif()

qt_add_executable(agbplay-gui MANUAL_FINALIZATION WIN32 ${AGBPLAY_GUI_SOURCES} ${AGBPLAY_GUI_HEADERS} ${AGBPLAY_GUI_UIS} ${AGBPLAY_GUI_QRC} ${AGBPLAY_GUI_WIN32_RC})
if(AGBPLAY_QT_AUDIO)
    target_compile_definitions(agbplay-gui PRIVATE AGBPLAY_QT_AUDIO=1)
else()
    pkg_check_modules(portaudio REQUIRED IMPORTED_TARGET portaudiocpp)
    target_link_libraries(agbplay-gui PRIVATE PkgConfig::portaudio)
endif()
if(ANDROID)
    set_target_properties(agbplay-gui PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${PROJECT_SOURCE_DIR}/android"
        QT_ANDROID_PACKAGE_NAME "org.agbplay.android"
        QT_ANDROID_APP_NAME "agbplay"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 35
        QT_ANDROID_VERSION_CODE 1
        QT_ANDROID_VERSION_NAME "1.0.0-android-dev")
endif()

target_compile_options(agbplay-gui PRIVATE -Wall -Wextra -Wconversion)



target_link_libraries(agbplay-gui
    PRIVATE
    agbplay
    agbplay-util
    Qt6::Core
    Qt6::Widgets
)

qt_finalize_executable(agbplay-gui)
