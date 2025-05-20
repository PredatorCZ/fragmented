project(MMARCExtract)

build_target(
  NAME
  mmarc_extract
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  mmarc_extract.cpp
  LINKS
  zlib-objects
  spike-interface
  INCLUDES
  ${TPD_PATH}/zlib
  AUTHOR
  "Lukas Cone"
  DESCR
  "Mad Max Archive Extractor"
  START_YEAR
  2024)

project(HCARCExtract)

build_target(
  NAME
  hcarc_extract
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  hcarc_extract.cpp
  LINKS
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Hunter Classic Archive Extractor"
  START_YEAR
  2024)

project(Property2XML)

#set(CMAKE_BUILD_TYPE Debug)

build_target(
  NAME
  property_to_xml
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  property_to_xml.cpp
  LINKS
  spike-interface
  pugixml-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Apex Property to XML"
  START_YEAR
  2024)

project(SARCExtract)

build_target(
  NAME
  sarc_extract
  TYPE
  ESMODULE
  VERSION
  1
  SOURCES
  sarc_extract.cpp
  LINKS
  spike-interface
  AUTHOR
  "Lukas Cone"
  DESCR
  "Apex Small Archive Extractor"
  START_YEAR
  2024)
