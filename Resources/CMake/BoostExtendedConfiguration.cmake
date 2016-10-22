SET(ORTHANC_BOOST_COMPONENTS program_options)

include(${ORTHANC_ROOT}/Resources/CMake/BoostConfiguration.cmake)

if (BOOST_STATIC)
  list(APPEND BOOST_SOURCES
    ${BOOST_SOURCES_DIR}/libs/program_options/src/cmdline.cpp
    ${BOOST_SOURCES_DIR}/libs/program_options/src/config_file.cpp
    ${BOOST_SOURCES_DIR}/libs/program_options/src/convert.cpp
    ${BOOST_SOURCES_DIR}/libs/program_options/src/options_description.cpp
    ${BOOST_SOURCES_DIR}/libs/program_options/src/parsers.cpp
    ${BOOST_SOURCES_DIR}/libs/program_options/src/positional_options.cpp
    ${BOOST_SOURCES_DIR}/libs/program_options/src/split.cpp
    ${BOOST_SOURCES_DIR}/libs/program_options/src/utf8_codecvt_facet.cpp
    ${BOOST_SOURCES_DIR}/libs/program_options/src/value_semantic.cpp
    ${BOOST_SOURCES_DIR}/libs/program_options/src/variables_map.cpp
    #${BOOST_SOURCES_DIR}/libs/program_options/src/winmain.cpp
    )
  add_definitions(-DBOOST_PROGRAM_OPTIONS_NO_LIB)
endif()

