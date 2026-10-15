add_library(ms_ui STATIC
  Theme.cpp Theme.h
  Icons.cpp Icons.h
  ProgramMonitor.cpp ProgramMonitor.h
  SourceGrid.cpp SourceGrid.h
  ControlPanel.cpp ControlPanel.h
  GraphicsPanel.cpp GraphicsPanel.h
  AudioMixer.cpp AudioMixer.h
  TopBar.cpp TopBar.h
  SourceDialogs.cpp SourceDialogs.h
  SceneEditor.cpp SceneEditor.h
  GraphicsDialogs.cpp GraphicsDialogs.h
  SettingsDialog.cpp SettingsDialog.h
  MainWindow.cpp MainWindow.h)
target_include_directories(ms_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ms_ui PUBLIC ms_engine Qt6::Widgets)
