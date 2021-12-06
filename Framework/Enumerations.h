/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2021 Osimis S.A., Belgium
 * Copyright (C) 2021-2021 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#pragma once

#include <Enumerations.h>

#include <stdint.h>
#include <string>

namespace OrthancWSI
{
  // WARNING - Don't change the enum values below, as this would break
  // serialization of "DicomPyramidInstance"
  enum ImageCompression
  {
    ImageCompression_Unknown = 1,
    ImageCompression_None = 2,
    ImageCompression_Dicom = 3,
    ImageCompression_Png = 4,
    ImageCompression_Jpeg = 5,
    ImageCompression_Jpeg2000 = 6,
    ImageCompression_Tiff = 7
  };

  enum OpticalPath
  {
    OpticalPath_None,
    OpticalPath_Brightfield
  };

  const char* EnumerationToString(ImageCompression compression);

  ImageCompression DetectFormatFromFile(const std::string& path);

  ImageCompression DetectFormatFromMemory(const void* buffer,
                                          size_t size);

  inline unsigned int CeilingDivision(unsigned int a,
                                      unsigned int b)
  {
    if (a % b == 0)
    {
      return a / b;
    }
    else
    {
      return a / b + 1;
    }
  }
}
