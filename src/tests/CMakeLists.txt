cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)

find_package(fmt REQUIRED)

include_directories("${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
link_libraries(agbplay fmt::fmt)

add_executable(test-resampler-sinc TestResamplerSinc.cpp)
target_compile_options(test-resampler-sinc PRIVATE -Wall -Wextra -Wconversion)

find_package(Threads REQUIRED)
add_executable(test-safety TestSafety.cpp)
target_include_directories(test-safety PRIVATE ../agbplay-util)
target_link_libraries(test-safety PRIVATE Threads::Threads)
add_test(NAME safety COMMAND test-safety)
set_tests_properties(safety PROPERTIES TIMEOUT 30)
# Inject a deterministic fake sink to exercise the actual playback worker without a sound device.
add_executable(test-playback TestPlayback.cpp ../agbplay-util/PlaybackEngine.cpp)
target_include_directories(test-playback PRIVATE ../agbplay-util)
target_link_libraries(test-playback PRIVATE Threads::Threads)
add_test(NAME playback COMMAND test-playback)
set_tests_properties(playback PROPERTIES TIMEOUT 60)
