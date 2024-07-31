cmake_minimum_required(VERSION 3.3)

project(ExtractManifest)

build_target(
  NAME
  extract_mnfst
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  extract_mnfst.cpp
  crc64.c
  LINKS
  spike-interface
  z
  AUTHOR
  "Lukas Cone"
  DESCR
  "Extract manifest and upak data"
  START_YEAR
  2024)

project(ExtractOPPC)

build_target(
  NAME
  extract_oppc
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  extract_oppc.cpp
  crc64.c
  LINKS
  spike-interface
  z
  AUTHOR
  "Lukas Cone"
  DESCR
  "Extract oppc archive"
  START_YEAR
  2024)

project(BOD2JSON)

build_target(
  NAME
  bod_to_json
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  bod_to_json.cpp
  crc64.c
  object.cpp
  INCLUDES
  ${TPD_PATH}/spike/3rd_party/json
  LINKS
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Convert BOD to JSON"
  START_YEAR
  2024)

project(O3D2GLTF)

build_target(
  NAME
  o3d_to_gltf
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  o3d_to_gltf.cpp
  object.cpp
  INCLUDES
  ${TPD_PATH}/spike/3rd_party/json
  LINKS
  spike-interface
  gltf-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Convert O3D to GLTF"
  START_YEAR
  2024)

project(ANM2GLTF)

build_target(
  NAME
  anm_to_gltf
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  anm_to_gltf.cpp
  crc64.c
  INCLUDES
  ${TPD_PATH}/spike/3rd_party/json
  LINKS
  spike-interface
  gltf-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Convert ANM to GLTF"
  START_YEAR
  2024)

project(ExtractOBSP)

build_target(
  NAME
  extract_obsp
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  extract_obsp.cpp
  crc64.c
  object.cpp
  INCLUDES
  ${TPD_PATH}/spike/3rd_party/json
  LINKS
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Extract obsp archive"
  START_YEAR
  2024)
