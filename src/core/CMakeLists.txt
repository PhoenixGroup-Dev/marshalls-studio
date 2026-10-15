add_library(ms_core STATIC
  StudioConfig.cpp StudioConfig.h
  ConfigStore.cpp ConfigStore.h
  ProgramController.cpp ProgramController.h
  ShortcutMap.cpp ShortcutMap.h
  Planning.cpp Planning.h
  Log.cpp Log.h
  SecretStore.cpp SecretStore.h)
target_include_directories(ms_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ms_core PUBLIC Qt6::Core Qt6::Gui)
if(WIN32)
  target_link_libraries(ms_core PRIVATE crypt32)
endif()
