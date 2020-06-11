/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2020 Osimis S.A., Belgium
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
  enum ImageCompression
  {
    ImageCompression_Unknown,
    ImageCompression_None,
    ImageCompression_Dicom,
    ImageCompression_Png,
    ImageCompression_Jpeg,
    ImageCompression_Jpeg2000,
    ImageCompression_Tiff
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
