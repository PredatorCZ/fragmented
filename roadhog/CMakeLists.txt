project(HOGPExtract)

build_target(
  NAME
  hogp_extract
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  hogp_extract.cpp
  LINKS
  zlib-objects
  spike-interface
  INCLUDES
  ${TPD_PATH}/zlib
  AUTHOR
  "Lukas Cone"
  DESCR
  "RoadHog 3 Archive Extractor"
  START_YEAR
  2024)

project(HOGPCreate)

build_target(
  NAME
  hogp_create
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  hogp_create.cpp
  LINKS
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Create RoadHog 3 Archive"
  START_YEAR
  2024)

project(NutCracker)

build_target(
  NAME
  nut_decompile
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  nut_decompile.cpp
  LINKS
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Squirel 64bit decompiler"
  START_YEAR
  2024)

project(BINExtract)

build_target(
  NAME
  bin_extract
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  bin_extract.cpp
  LINKS
  zlib-objects
  spike-interface
  INCLUDES
  ${TPD_PATH}/zlib
  AUTHOR
  "Lukas Cone"
  DESCR
  "RoadHog 2 Archive Extractor"
  START_YEAR
  2024)
