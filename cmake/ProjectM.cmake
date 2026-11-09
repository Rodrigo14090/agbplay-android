include(FetchContent)

# Scope projectM's generic option names (BUILD_SHARED_LIBS, BUILD_TESTING, ...)
# so they cannot change agbplay's dependencies or test configuration.
function(agbplay_add_projectm)
    set(CMAKE_AUTOMOC OFF)
    set(CMAKE_AUTOUIC OFF)
    set(CMAKE_AUTORCC OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_DOCS OFF)
    set(ENABLE_PLAYLIST ON)
    set(ENABLE_SDL_UI OFF)
    set(ENABLE_INSTALL OFF)
    set(ENABLE_SYSTEM_PROJECTM_EVAL OFF)
    set(ENABLE_DEBUG_POSTFIX OFF)
    set(ENABLE_GLES ${ANDROID})
    FetchContent_Declare(agbplay_projectm
        GIT_REPOSITORY https://github.com/projectM-visualizer/projectm.git
        GIT_TAG 4967dc003e5206d7df3ff447287f7609c47d2343 # v4.1.8
        GIT_SUBMODULES vendor/projectm-eval
        GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(agbplay_projectm)
endfunction()
agbplay_add_projectm()
