add_executable(tst_core core/tst_core.cpp)
target_link_libraries(tst_core PRIVATE ms_core Qt6::Test)
add_test(NAME core COMMAND tst_core)

# Engine integration tests drive PulseAudio/X11/nginx-rtmp: Linux test bench only.
if(MS_BUILD_ENGINE AND UNIX AND NOT APPLE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/engine/CMakeLists.txt)
  add_subdirectory(engine)
endif()
