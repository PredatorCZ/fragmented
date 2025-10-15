cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 23)
project(Fragmented)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-strict-aliasing")

option(CLI "" ON)
option(GLTF "" ON)
set(EXPOSE_SYMBOLS spike;pugixml;gltf)

set(TPD_PATH ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party)

add_subdirectory(${TPD_PATH}/spike)
include(version)
toolset_version()

set_target_properties(spike_cli PROPERTIES OUTPUT_NAME fragmented)

add_spike_subdir(chicken)
add_spike_subdir(psarc)
add_spike_subdir(thing)
add_spike_subdir(trapt)
add_spike_subdir(chaos)
