cmake_minimum_required(VERSION 3.12)

project(SSGExtract)

file(GLOB ZLIB_SOURCES "${TPD_PATH}/zlib/*.c")

build_target(
  NAME
  extract_ssg
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  extract_ssg.cpp
  ${ZLIB_SOURCES}
  INCLUDES
  ${TPD_PATH}/zlib
  LINKS
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Extract Hexane SSG archive"
  START_YEAR
  2024)
