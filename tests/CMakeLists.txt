add_executable(tst_core core/tst_core.cpp)
target_link_libraries(tst_core PRIVATE ms_core Qt6::Test)
add_test(NAME core COMMAND tst_core)

if(MS_BUILD_ENGINE)
  add_subdirectory(engine)
endif()
