project(BST2GLTF)

build_target(
  NAME
  bst_to_gltf
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  bst_to_gltf.cpp
  LINKS
  gltf-interface
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Convert The Thing bst animset to gltf"
  START_YEAR
  2025)
