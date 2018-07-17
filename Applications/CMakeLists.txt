cmake_minimum_required(VERSION 2.8)
project(OrthancWSIApplications)

include(${CMAKE_SOURCE_DIR}/../Resources/CMake/Version.cmake)


#####################################################################
## Parameters of the build
#####################################################################

# Generic parameters
SET(STATIC_BUILD OFF CACHE BOOL "Static build of the third-party libraries (necessary for Windows)")
SET(ALLOW_DOWNLOADS OFF CACHE BOOL "Allow CMake to download packages")
SET(ENABLE_PROFILING OFF CACHE BOOL "Whether to enable the generation of profiling information with gprof")

# Advanced parameters to fine-tune linking against system libraries
SET(USE_SYSTEM_LIBTIFF ON CACHE BOOL "Use the system version of libtiff")
SET(USE_SYSTEM_OPENJPEG ON CACHE BOOL "Use the system version of OpenJpeg")
SET(USE_SYSTEM_ORTHANC_SDK ON CACHE BOOL "Use the system version of the Orthanc plugin SDK")



#####################################################################
## Configure mandatory third-party components
#####################################################################

SET(ORTHANC_WSI_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
SET(USE_OPENJPEG_JP2 ON)

include(${CMAKE_SOURCE_DIR}/../Resources/Orthanc/DownloadOrthancFramework.cmake)

UNSET(STANDALONE_BUILD CACHE)
SET(STANDALONE_BUILD ON)       # Embed DCMTK's dictionaries for static builds

set(ORTHANC_FRAMEWORK_PLUGIN OFF)
include(${ORTHANC_ROOT}/Resources/CMake/OrthancFrameworkParameters.cmake)

SET(ENABLE_CRYPTO_OPTIONS ON)
SET(ENABLE_DCMTK ON)
SET(ENABLE_DCMTK_JPEG OFF)          # Disable DCMTK's support for JPEG, that clashes with libtiff
SET(ENABLE_DCMTK_JPEG_LOSSLESS OFF) # Disable DCMTK's support for JPEG-LS
SET(ENABLE_DCMTK_NETWORKING OFF)    # Disable DCMTK's support for DICOM networking
SET(ENABLE_JPEG ON)
SET(ENABLE_LOCALE ON)               # Enable support for locales (notably in Boost)
SET(ENABLE_PNG ON)
SET(ENABLE_SSL ON)
SET(ENABLE_WEB_CLIENT ON)
SET(ENABLE_ZLIB ON)
SET(HAS_EMBEDDED_RESOURCES ON)

include(${ORTHANC_ROOT}/Resources/CMake/VisualStudioPrecompiledHeaders.cmake)
include(${ORTHANC_ROOT}/Resources/CMake/OrthancFrameworkConfiguration.cmake)
include_directories(${ORTHANC_ROOT})


# Include components specific to WSI
include(${ORTHANC_WSI_DIR}/Resources/CMake/BoostExtendedConfiguration.cmake)
include(${ORTHANC_WSI_DIR}/Resources/CMake/OpenJpegConfiguration.cmake)
include(${ORTHANC_WSI_DIR}/Resources/CMake/LibTiffConfiguration.cmake)



#####################################################################
## Find the Orthanc SDK
#####################################################################

if (STATIC_BUILD OR NOT USE_SYSTEM_ORTHANC_SDK)
  include_directories(${CMAKE_SOURCE_DIR}/../Resources/Orthanc/Sdk-1.0.0)
else ()
  CHECK_INCLUDE_FILE_CXX(orthanc/OrthancCPlugin.h HAVE_ORTHANC_H)
  if (NOT HAVE_ORTHANC_H)
    message(FATAL_ERROR "Please install the headers of the Orthanc plugins SDK")
  endif()
endif()


#####################################################################
## Create the list of the source files that depend upon the
## precompiled headers
#####################################################################

set(ORTHANC_WSI_SOURCES
  ${ORTHANC_WSI_DIR}/Framework/Algorithms/PyramidReader.cpp
  ${ORTHANC_WSI_DIR}/Framework/Algorithms/ReconstructPyramidCommand.cpp
  ${ORTHANC_WSI_DIR}/Framework/Algorithms/TranscodeTileCommand.cpp
  ${ORTHANC_WSI_DIR}/Framework/DicomToolbox.cpp
  ${ORTHANC_WSI_DIR}/Framework/DicomizerParameters.cpp
  ${ORTHANC_WSI_DIR}/Framework/Enumerations.cpp
  ${ORTHANC_WSI_DIR}/Framework/ImageToolbox.cpp
  ${ORTHANC_WSI_DIR}/Framework/ImagedVolumeParameters.cpp
  ${ORTHANC_WSI_DIR}/Framework/Inputs/DecodedTiledPyramid.cpp
  ${ORTHANC_WSI_DIR}/Framework/Inputs/DicomPyramid.cpp
  ${ORTHANC_WSI_DIR}/Framework/Inputs/DicomPyramidInstance.cpp
  ${ORTHANC_WSI_DIR}/Framework/Inputs/DicomPyramidLevel.cpp
  ${ORTHANC_WSI_DIR}/Framework/Inputs/HierarchicalTiff.cpp
  ${ORTHANC_WSI_DIR}/Framework/Inputs/OpenSlideLibrary.cpp
  ${ORTHANC_WSI_DIR}/Framework/Inputs/OpenSlidePyramid.cpp
  ${ORTHANC_WSI_DIR}/Framework/Inputs/PyramidWithRawTiles.cpp
  ${ORTHANC_WSI_DIR}/Framework/Inputs/SingleLevelDecodedPyramid.cpp
  ${ORTHANC_WSI_DIR}/Framework/Inputs/TiledPyramidStatistics.cpp
  ${ORTHANC_WSI_DIR}/Framework/Jpeg2000Reader.cpp
  ${ORTHANC_WSI_DIR}/Framework/Jpeg2000Writer.cpp
  ${ORTHANC_WSI_DIR}/Framework/Targets/FolderTarget.cpp
  ${ORTHANC_WSI_DIR}/Framework/Targets/OrthancTarget.cpp
  ${ORTHANC_WSI_DIR}/Framework/Outputs/DicomPyramidWriter.cpp
  ${ORTHANC_WSI_DIR}/Framework/Outputs/HierarchicalTiffWriter.cpp
  ${ORTHANC_WSI_DIR}/Framework/Outputs/InMemoryTiledImage.cpp
  ${ORTHANC_WSI_DIR}/Framework/Outputs/MultiframeDicomWriter.cpp
  ${ORTHANC_WSI_DIR}/Framework/Outputs/PyramidWriterBase.cpp
  ${ORTHANC_WSI_DIR}/Framework/Outputs/TruncatedPyramidWriter.cpp
  )

EmbedResources(
  ${DCMTK_DICTIONARIES}
  BRIGHTFIELD_OPTICAL_PATH  ${ORTHANC_WSI_DIR}/Resources/BrightfieldOpticalPath.json
  SAMPLE_DATASET            ${ORTHANC_WSI_DIR}/Resources/SampleDataset.json
  SRGB_ICC_PROFILE          ${ORTHANC_WSI_DIR}/Resources/sRGB.icc
  )

add_definitions(
  -DHAS_ORTHANC_EXCEPTION=1
  -DORTHANC_ENABLE_LOGGING_PLUGIN=0
  )


#####################################################################
## Setup precompiled headers for Microsoft Visual Studio
#####################################################################

if (MSVC)
  add_definitions(-DORTHANC_USE_PRECOMPILED_HEADERS=1)

  set(TMP
    ${ORTHANC_CORE_SOURCES_INTERNAL}
    ${ORTHANC_DICOM_SOURCES_INTERNAL}
    )
  
  ADD_VISUAL_STUDIO_PRECOMPILED_HEADERS(
    "PrecompiledHeaders.h" "${ORTHANC_ROOT}/Core/PrecompiledHeaders.cpp"
    TMP ORTHANC_CORE_PCH)

  ADD_VISUAL_STUDIO_PRECOMPILED_HEADERS(
    "PrecompiledHeadersWSI.h" "${ORTHANC_WSI_DIR}/Framework/PrecompiledHeadersWSI.cpp"
    ORTHANC_WSI_SOURCES ORTHANC_WSI_PCH)

  source_group(ThirdParty\\OrthancCore FILES ${ORTHANC_CORE_SOURCES})
endif()


#####################################################################
## Create the static library containing the framework
#####################################################################

add_library(OrthancWSIFramework STATIC
  ${ORTHANC_CORE_PCH}
  ${ORTHANC_CORE_SOURCES_INTERNAL}
  ${ORTHANC_CORE_SOURCES_DEPENDENCIES}
  ${ORTHANC_DICOM_SOURCES_INTERNAL}
  ${ORTHANC_DICOM_SOURCES_DEPENDENCIES}
  ${ORTHANC_WSI_PCH}
  ${ORTHANC_WSI_SOURCES}
  ${AUTOGENERATED_SOURCES}
  ${BOOST_EXTENDED_SOURCES}

  ${ORTHANC_ROOT}/Plugins/Samples/Common/OrthancPluginCppWrapper.cpp
  ${ORTHANC_ROOT}/Plugins/Samples/Common/DicomDatasetReader.cpp
  ${ORTHANC_ROOT}/Plugins/Samples/Common/DicomPath.cpp
  ${ORTHANC_ROOT}/Plugins/Samples/Common/DicomTag.cpp
  ${ORTHANC_ROOT}/Plugins/Samples/Common/FullOrthancDataset.cpp
  ${ORTHANC_ROOT}/Plugins/Samples/Common/IOrthancConnection.cpp
  ${ORTHANC_ROOT}/Plugins/Samples/Common/OrthancHttpConnection.cpp
  ${ORTHANC_ROOT}/Plugins/Samples/Common/OrthancPluginConnection.cpp
  ${ORTHANC_ROOT}/Plugins/Samples/Common/SimplifiedOrthancDataset.cpp

  # Mandatory components
  ${LIBTIFF_SOURCES}
  ${OPENJPEG_SOURCES}
  )


#####################################################################
## Build the WSI DICOM-izer
#####################################################################

# Create the Windows resources, if need be
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  execute_process(
    COMMAND 
    ${PYTHON_EXECUTABLE} ${ORTHANC_ROOT}/Resources/WindowsResources.py
    ${ORTHANC_WSI_VERSION} OrthancWSIDicomizer OrthancWSIDicomizer.exe "Companion tool to Orthanc for whole-slide imaging"
    ERROR_VARIABLE Failure
    OUTPUT_FILE ${AUTOGENERATED_DIR}/OrthancWSIDicomizer.rc
    )

  if (Failure)
    message(FATAL_ERROR "Error while computing the version information: ${Failure}")
  endif()

  set(DICOMIZER_RESOURCES ${AUTOGENERATED_DIR}/OrthancWSIDicomizer.rc)
endif()


add_executable(OrthancWSIDicomizer
  Dicomizer.cpp
  ApplicationToolbox.cpp
  ${DICOMIZER_RESOURCES}
  )

target_link_libraries(OrthancWSIDicomizer OrthancWSIFramework ${DCMTK_LIBRARIES})

install(
  TARGETS OrthancWSIDicomizer
  RUNTIME DESTINATION bin
  )


#####################################################################
## Build the DICOM-to-TIFF conversion tool
#####################################################################

# Create the Windows resources, if need be
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  execute_process(
    COMMAND 
    ${PYTHON_EXECUTABLE} ${ORTHANC_ROOT}/Resources/WindowsResources.py
    ${ORTHANC_WSI_VERSION} OrthancWSIDicomToTiff OrthancWSIDicomToTiff.exe "Companion tool to Orthanc for whole-slide imaging"
    ERROR_VARIABLE Failure
    OUTPUT_FILE ${AUTOGENERATED_DIR}/OrthancWSIDicomToTiff.rc
    )

  if (Failure)
    message(FATAL_ERROR "Error while computing the version information: ${Failure}")
  endif()

  set(DICOM_TO_TIFF_RESOURCES ${AUTOGENERATED_DIR}/OrthancWSIDicomToTiff.rc)
endif()


add_executable(OrthancWSIDicomToTiff
  DicomToTiff.cpp
  ApplicationToolbox.cpp
  ${DICOM_TO_TIFF_RESOURCES}
  )

target_link_libraries(OrthancWSIDicomToTiff OrthancWSIFramework ${DCMTK_LIBRARIES})

install(
  TARGETS OrthancWSIDicomToTiff
  RUNTIME DESTINATION bin
  )


#####################################################################
## Generate the documentation if Doxygen is present
#####################################################################

find_package(Doxygen)
if (DOXYGEN_FOUND)
  configure_file(
    ${ORTHANC_WSI_DIR}/Resources/OrthancWSI.doxygen
    ${CMAKE_CURRENT_BINARY_DIR}/OrthancWSI.doxygen
    @ONLY)

  add_custom_target(doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/OrthancWSI.doxygen
    COMMENT "Generating documentation with Doxygen" VERBATIM
    )
else()
  message("Doxygen not found. The documentation will not be built.")
endif()
