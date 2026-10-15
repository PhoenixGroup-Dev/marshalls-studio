add_library(ms_engine STATIC
  Engine.cpp Engine.h
  ObsPlatform.cpp ObsPlatform.h
  LowerThird.cpp LowerThird.h
  ThumbnailService.cpp ThumbnailService.h
  Outputs.cpp Outputs.h
  RtmpService.cpp RtmpService.h
  Studio.cpp Studio.h)
target_include_directories(ms_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ms_engine PUBLIC ms_core Qt6::Core Qt6::Gui OBS::libobs)
if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
  target_link_libraries(ms_engine PUBLIC X11::X11)
endif()
if(WIN32)
  target_link_libraries(ms_engine PUBLIC ole32)
endif()
