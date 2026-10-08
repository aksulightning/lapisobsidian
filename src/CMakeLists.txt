# This file is meant for use with ESP-IDF or PlatformIO.
# Compilation for other platforms must be done manually.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources}
  PRIV_REQUIRES esp_wifi nvs_flash esp_timer
  INCLUDE_DIRS ".")

foreach(feature LAPIS_WORLD_EDIT LAPIS_TREE_CHOPPER)
  if("$ENV{${feature}}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${feature}=1)
  endif()
endforeach()
