if (STATIC_BUILD OR NOT USE_SYSTEM_LIBTIFF)
  SET(LIBTIFF_SOURCES_DIR ${CMAKE_BINARY_DIR}/tiff-4.0.6)
  SET(LIBTIFF_URL "http://orthanc.osimis.io/ThirdPartyDownloads/tiff-4.0.6.tar.gz")
  SET(LIBTIFF_MD5 "d1d2e940dea0b5ad435f21f03d96dd72")

  DownloadPackage(${LIBTIFF_MD5} ${LIBTIFF_URL} "${LIBTIFF_SOURCES_DIR}")

  if (NOT EXISTS ${LIBTIFF_SOURCES_DIR}/libtiff/tif_config.h)
    file(WRITE ${LIBTIFF_SOURCES_DIR}/libtiff/tif_config.h "
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <string.h>
")
    file(WRITE ${LIBTIFF_SOURCES_DIR}/libtiff/tiffconf.h "
#if defined(_MSC_VER)
#  if !defined(ssize_t)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    define ssize_t SSIZE_T
#  endif
#  if !defined(snprintf) && (_MSC_VER < 1900)
#    define snprintf _snprintf
#  endif
#endif

#include <stdint.h>
#include <sys/types.h>
")
  endif()

  set(TIFF_FILLORDER FILLORDER_MSB2LSB)
  if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "i.*86.*" OR
      CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "amd64.*" OR
      CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64.*")
    set(TIFF_FILLORDER FILLORDER_LSB2MSB)
  endif()

  add_definitions(
    -DTIFF_INT8_T=int8_t
    -DTIFF_INT16_T=int16_t
    -DTIFF_INT32_T=int32_t
    -DTIFF_INT64_T=int64_t
    -DTIFF_UINT8_T=uint8_t
    -DTIFF_UINT16_T=uint16_t
    -DTIFF_UINT32_T=uint32_t
    -DTIFF_UINT64_T=uint64_t
    -DTIFF_SSIZE_T=ssize_t
    -DHAVE_IEEEFP=1
    -DHOST_FILLORDER=${TIFF_FILLORDER}
    -DHAVE_SNPRINTF=1
    -DJPEG_SUPPORT=1
    -DLZW_SUPPORT=1
    )

  if (MSVC)
    # The "%" must be escaped if using Visual Studio
    add_definitions(
      -DTIFF_INT64_FORMAT="%%lld"
      -DTIFF_UINT64_FORMAT="%%llu"
      -DTIFF_SSIZE_FORMAT="%%d"
      )
  else()
    add_definitions(
      -DTIFF_INT64_FORMAT="%lld"
      -DTIFF_UINT64_FORMAT="%llu"
      -DTIFF_SSIZE_FORMAT="%d"
      )
  endif()

  set(LIBTIFF_SOURCES
    #${LIBTIFF_SOURCES_DIR}/libtiff/mkg3states.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_aux.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_close.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_codec.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_color.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_compress.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_dir.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_dirinfo.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_dirread.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_dirwrite.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_dumpmode.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_error.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_extension.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_fax3.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_fax3sm.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_flush.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_getimage.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_jbig.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_jpeg_12.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_jpeg.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_luv.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_lzma.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_lzw.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_next.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_ojpeg.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_open.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_packbits.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_pixarlog.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_predict.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_print.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_read.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_strip.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_swab.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_thunder.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_tile.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_unix.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_version.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_warning.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_write.c
    ${LIBTIFF_SOURCES_DIR}/libtiff/tif_zip.c
    )

  include_directories(${LIBTIFF_SOURCES_DIR}/libtiff)

  if ("${CMAKE_SYSTEM_VERSION}" STREQUAL "Windows")
    list(APPEND LIBTIFF_SOURCES
      ${LIBTIFF_SOURCES_DIR}/libtiff/tif_win32.c
      )
  endif()

  source_group(ThirdParty\\libtiff REGULAR_EXPRESSION ${LIBTIFF_SOURCES_DIR}/.*)

else()
  CHECK_INCLUDE_FILE_CXX(tiff.h HAVE_LIBTIFF_H)
  if (NOT HAVE_LIBTIFF_H)
    message(FATAL_ERROR "Please install the libtiff-dev package")
  endif()

  CHECK_LIBRARY_EXISTS(tiff TIFFGetField "" HAVE_LIBTIFF_LIB)
  if (NOT HAVE_LIBTIFF_LIB)
    message(FATAL_ERROR "Please install the libtiff-dev package")
  endif()

  link_libraries(tiff)
endif()
