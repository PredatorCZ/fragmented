cmake_minimum_required(VERSION 3.12)

project(CPC2GLTF)

build_target(
  NAME
  cpc_to_gltf
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  cpc_to_gltf.cpp
  LINKS
  gltf-interface
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Convert Chaos Legion CPC/item to GLTF"
  START_YEAR
  2025)
