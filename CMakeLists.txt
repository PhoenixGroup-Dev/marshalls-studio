cmake_minimum_required(VERSION 3.22)
project(MarshallsStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MS_BUILD_TESTS "Build automated tests" ON)
option(MS_BUILD_ENGINE "Build the libobs media engine and the application" ON)

if(MSVC)
  add_compile_options(/W3 /utf-8 /permissive-)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)

add_compile_definitions(MS_VERSION="${PROJECT_VERSION}")

add_subdirectory(src/core)
if(MS_BUILD_ENGINE)
  find_package(libobs REQUIRED)
  add_subdirectory(src/engine)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/CMakeLists.txt)
    add_subdirectory(src/ui)
    add_subdirectory(src/app)
  endif()
endif()

if(MS_BUILD_TESTS)
  # The Qt build bundled with obs-deps (Windows) has no QtTest module.
  find_package(Qt6 QUIET COMPONENTS Test)
  if(TARGET Qt6::Test)
    enable_testing()
    add_subdirectory(tests)
  else()
    message(WARNING "Qt6 Test não encontrado: testes automatizados desativados neste build")
  endif()
endif()

