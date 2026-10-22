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

list(REMOVE_ITEM AGBPLAY_GUI_SOURCES "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
add_library(agbplay-ui STATIC ${AGBPLAY_GUI_SOURCES} ${AGBPLAY_GUI_HEADERS} ${AGBPLAY_GUI_UIS})
target_include_directories(agbplay-ui PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(agbplay-ui PUBLIC agbplay agbplay-util Qt6::Core Qt6::Widgets)
target_compile_options(agbplay-ui PRIVATE -Wall -Wextra -Wconversion)
qt_add_executable(agbplay-gui MANUAL_FINALIZATION WIN32 main.cpp ${AGBPLAY_GUI_QRC} ${AGBPLAY_GUI_WIN32_RC})
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
        QT_ANDROID_VERSION_CODE 10
        QT_ANDROID_VERSION_NAME "1.0.9-android-dev")
endif()

target_compile_options(agbplay-gui PRIVATE -Wall -Wextra -Wconversion)



target_link_libraries(agbplay-gui PRIVATE agbplay-ui)

if(BUILD_TESTING AND NOT ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_package(ZLIB REQUIRED)
    qt_add_executable(test-touch-ui ../tests/TestTouchUi.cpp ../tests/UiTestAudioOutput.cpp ${AGBPLAY_GUI_QRC})
    target_link_libraries(test-touch-ui PRIVATE agbplay-ui Qt6::Test ZLIB::ZLIB)
    add_test(NAME touch-ui COMMAND test-touch-ui)
    set_tests_properties(touch-ui PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

qt_finalize_executable(agbplay-gui)

if(ANDROID AND AGBPLAY_ANDROID_UI_PROBE)
    qt_add_executable(android-ui-probe MANUAL_FINALIZATION ../tests/AndroidUiProbe.cpp ${AGBPLAY_GUI_QRC})
    target_link_libraries(android-ui-probe PRIVATE agbplay-ui)
    set_target_properties(android-ui-probe PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${PROJECT_SOURCE_DIR}/android"
        QT_ANDROID_PACKAGE_NAME "org.agbplay.android"
        QT_ANDROID_APP_NAME "agbplay UI regression"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 35)
    qt_finalize_executable(android-ui-probe)

    find_package(ZLIB REQUIRED)
    qt_add_executable(android-background-probe MANUAL_FINALIZATION ../tests/BackgroundAudioProbe.cpp ${AGBPLAY_GUI_QRC})
    target_link_libraries(android-background-probe PRIVATE agbplay-ui ZLIB::ZLIB)
    set_target_properties(android-background-probe PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${PROJECT_SOURCE_DIR}/android"
        QT_ANDROID_PACKAGE_NAME "org.agbplay.android"
        QT_ANDROID_APP_NAME "agbplay background regression"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 35)
    qt_finalize_executable(android-background-probe)
endif()
