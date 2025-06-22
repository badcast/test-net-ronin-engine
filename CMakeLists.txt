cmake_minimum_required(VERSION 3.10)

project(test LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

option(USE_SYSRONIN "Use system installed ronin engine" OFF)

find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL2_net REQUIRED SDL2_net)

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

file(GLOB_RECURSE HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

if(WIN32)
  add_executable(test WIN32 ${HEADERS} ${SOURCES})
else()
  add_executable(test ${HEADERS} ${SOURCES})
endif()

if(NOT USE_SYSRONIN)
  set(RONIN_NO_CONFIG ON)
  add_subdirectory(RoninEngine)
  target_link_libraries(test PRIVATE RoninEngine ${SDL2_net_LIBRARIES})
else()
  pkg_check_modules(RoninEngine REQUIRED RoninEngine)
  target_include_directories(test
                             PRIVATE ${RoninEngine_INCLUDE_DIRS})
  target_link_libraries(test PRIVATE ${RoninEngine_LIBRARIES})

  message(FATAL_ERROR ${SDL2_net_LIBRARIES})
endif()

# set the executable name
set_target_properties(
  test PROPERTIES OUTPUT_NAME game)

get_target_property(OUTPUT_NAME test OUTPUT_NAME)

target_compile_definitions(test
                           PUBLIC PIANO_BUILD_VERSION="${READVER}")

# INSTALLS

if(LINUX
   AND (CMAKE_BUILD_TYPE STREQUAL "Release"
        OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel"
        OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))

  include(GNUInstallDirs)

  set(EPICPIANO_DATA_DIRECTORY ${CMAKE_INSTALL_PREFIX}/share/${OUTPUT_NAME})

  target_compile_definitions(
    test
    PRIVATE EPICPIANO_DATA_DIRECTORY="${EPICPIANO_DATA_DIRECTORY}")

  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/resources/data/
          DESTINATION ${EPICPIANO_DATA_DIRECTORY})

  install(FILES "${CMAKE_BINARY_DIR}/bin/${OUTPUT_NAME}"
          DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
else()
  # create link resources "data:resources"
  file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
  set(_DATA_DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data")
  if(NOT EXISTS "${_DATA_DESTINATION}")
    message(STATUS "Linking data to \"${_DATA_DESTINATION}\"")
    file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/resources/data"
         "${_DATA_DESTINATION}" SYMBOLIC)
  else()
    message(STATUS "Symbolic link exists - not required")
  endif()
  unset(_DATA_DESTINATION)
endif()
