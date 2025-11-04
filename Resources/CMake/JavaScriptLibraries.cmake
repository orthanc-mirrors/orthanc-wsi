# Orthanc - A Lightweight, RESTful DICOM Store
# Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
# Department, University Hospital of Liege, Belgium
# Copyright (C) 2017-2023 Osimis S.A., Belgium
# Copyright (C) 2024-2025 Orthanc Team SRL, Belgium
# Copyright (C) 2021-2025 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
#
# This program is free software: you can redistribute it and/or
# modify it under the terms of the GNU Affero General Public License
# as published by the Free Software Foundation, either version 3 of
# the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Affero General Public License for more details.
# 
# You should have received a copy of the GNU Affero General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.


set(BASE_URL "https://orthanc.uclouvain.be/downloads/third-party-downloads")

DownloadPackage(
  "b7de8bba890ccc3858920bd66dd79d2d"
  "${BASE_URL}/WSI/openlayers-10.6.1-package.tar.gz"
  "openlayers-10.6.1-package")

set(JAVASCRIPT_LIBS_DIR  ${CMAKE_CURRENT_BINARY_DIR}/javascript-libs)
file(MAKE_DIRECTORY ${JAVASCRIPT_LIBS_DIR})

file(COPY
  ${CMAKE_CURRENT_BINARY_DIR}/openlayers-10.6.1-package/dist/ol.js
  DESTINATION
  ${JAVASCRIPT_LIBS_DIR}/js
  )

file(COPY
  ${CMAKE_CURRENT_BINARY_DIR}/openlayers-10.6.1-package/ol.css
  DESTINATION
  ${JAVASCRIPT_LIBS_DIR}/css
  )
