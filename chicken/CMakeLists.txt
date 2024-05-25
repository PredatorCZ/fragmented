project(MH2Extract)

build_target(
  NAME
  mh2_extract
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  mh2_extract.cpp
  LINKS
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Extract moorhuhn2.wtn"
  START_YEAR
  2024)

project(MH3Extract)

build_target(
  NAME
  mh3_extract
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  mh3_extract.cpp
  LINKS
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Extract moorhuhn3.dat"
  START_YEAR
  2024)
