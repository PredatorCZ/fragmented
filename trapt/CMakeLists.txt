project(SAI2GLTF)

build_target(
  NAME
  sai_to_gltf
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  sai_to_gltf.cpp
  LINKS
  spike-interface
  gltf-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Convert trapt sai to GLTF"
  START_YEAR
  2024)
