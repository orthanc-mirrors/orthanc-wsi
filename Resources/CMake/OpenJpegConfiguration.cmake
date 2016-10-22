if (STATIC_BUILD OR NOT USE_SYSTEM_OPENJPEG)
  SET(OPENJPEG_SOURCES_DIR ${CMAKE_BINARY_DIR}/openjpeg-version.2.1)
  SET(OPENJPEG_URL "http://www.montefiore.ulg.ac.be/~jodogne/Orthanc/ThirdPartyDownloads/openjpeg-2.1.tar.gz")
  SET(OPENJPEG_MD5 "3e1c451c087f8462955426da38aa3b3d")

  if (IS_DIRECTORY "${OPENJPEG_SOURCES_DIR}")
    set(FirstRun OFF)
  else()
    set(FirstRun ON)
  endif()

  DownloadPackage(${OPENJPEG_MD5} ${OPENJPEG_URL} "${OPENJPEG_SOURCES_DIR}")

  execute_process(
    COMMAND ${PATCH_EXECUTABLE} -p0 -N -i ${CMAKE_CURRENT_LIST_DIR}/OpenJpegConfiguration.patch
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    RESULT_VARIABLE Failure
    )

  if (Failure AND FirstRun)
    message(FATAL_ERROR "Error while patching a file")
  endif()

  if (USE_OPENJPEG_JP2)
    set(OPENJPEG_SOURCES
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/bio.c
      #${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/cidx_manager.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/cio.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/dwt.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/event.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/function_list.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/image.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/invert.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/j2k.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/jp2.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/mct.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/mqc.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/openjpeg.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/opj_clock.c
      #${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/phix_manager.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/pi.c
      #${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/ppix_manager.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/raw.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/t1.c
      #${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/t1_generate_luts.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/t2.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/tcd.c
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/tgt.c
      #${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/thix_manager.c
      #${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/tpix_manager.c
      )

    configure_file(
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/opj_config.h.cmake.in
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/opj_config.h
      @ONLY
      )
    
    configure_file(
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/opj_config_private.h.cmake.in
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/opj_config_private.h
      @ONLY
      )

    include_directories(
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2
      )

    # The following definition disables explicit inlining. This is
    # necessary to bypass the "undefined reference to
    # `opj_t1_dec_sigpass_step_mqc'" error.
    add_definitions(
      #-DINLINE=
      )

  else()
    AUX_SOURCE_DIRECTORY(${OPENJPEG_SOURCES_DIR}/src/lib/openmj2 OPENJPEG_SOURCES)

    configure_file(
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/opj_config.h.cmake.in
      ${OPENJPEG_SOURCES_DIR}/src/lib/openmj2/opj_config.h
      @ONLY
      )
    
    configure_file(
      ${OPENJPEG_SOURCES_DIR}/src/lib/openjp2/opj_config_private.h.cmake.in
      ${OPENJPEG_SOURCES_DIR}/src/lib/openmj2/opj_config_private.h
      @ONLY
      )

    include_directories(
      ${OPENJPEG_SOURCES_DIR}/src/lib/openmj2
      )
  endif()


  add_definitions(
    -DOPJ_STATIC
    -DORTHANC_OPENJPEG_MAJOR_VERSION=2
    )

  if (NOT WIN32)
    add_definitions(
      -DOPJ_HAVE_STDINT_H=1
      -DOPJ_HAVE_INTTYPES_H=1
      )
  endif()

  source_group(ThirdParty\\OpenJpeg REGULAR_EXPRESSION ${OPENJPEG_SOURCES_DIR}/.*)

else()
  CHECK_INCLUDE_FILE_CXX(openjpeg.h HAVE_OPENJPEG_H)
  if (NOT HAVE_OPENJPEG_H)
    message(FATAL_ERROR "Please install the openjpeg-devel package")
  endif()

  find_path(OPENJPEG_INCLUDE_DIR openjpeg.h)

  CHECK_LIBRARY_EXISTS(openjpeg opj_image_create "" HAVE_OPENJPEG_LIB)
  if (NOT HAVE_OPENJPEG_LIB)
    message(FATAL_ERROR "Please install the openjpeg-devel package")
  endif()

  # Autodetection of the version of OpenJpeg
  file(STRINGS
    "${OPENJPEG_INCLUDE_DIR}/openjpeg.h" 
    OPENJPEG_VERSION REGEX
    "#define OPENJPEG_VERSION")

  string(REGEX REPLACE
    ".*OPENJPEG_VERSION.*\"([0-9]*)\\.([0-9]*)\\.([0-9]*)\"$"
    "\\1" 
    OPENJPEG_MAJOR_VERSION ${OPENJPEG_VERSION})

  if (OPENJPEG_MAJOR_VERSION EQUAL 1)
    add_definitions(-DORTHANC_OPENJPEG_MAJOR_VERSION=1)
  elseif (OPENJPEG_MAJOR_VERSION EQUAL 2)
    add_definitions(-DORTHANC_OPENJPEG_MAJOR_VERSION=2)
  else()
    message(FATAL_ERROR "Cannot parse the version of OpenJpeg")
  endif()

  link_libraries(openjpeg)
endif()
