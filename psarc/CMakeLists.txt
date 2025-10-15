cmake_minimum_required(VERSION 3.12)

project(PsarcExtract)

file(GLOB ZLIB_SOURCES "${TPD_PATH}/zlib/*.c")

build_target(
  NAME
  extract_psarc
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  extract_psarc.cpp
  md5.c
  ${TPD_PATH}/lzma/LzmaDec.c
  ${ZLIB_SOURCES}
  INCLUDES
  ${TPD_PATH}/lzma
  ${TPD_PATH}/zlib
  LINKS
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Extract PlayStation archive"
  START_YEAR
  2023)
