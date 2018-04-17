/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2018 Osimis S.A., Belgium
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

#include <Core/Images/ImageAccessor.h>
#include "Enumerations.h"
#include "Inputs/ITiledPyramid.h"

#include <stdint.h>

namespace OrthancWSI
{
  namespace ImageToolbox
  {
    Orthanc::ImageAccessor* Allocate(Orthanc::PixelFormat format,
                                     unsigned int width,
                                     unsigned int height);

    void Embed(Orthanc::ImageAccessor& target,
               const Orthanc::ImageAccessor& source,
               unsigned int x,
               unsigned int y);

    inline void Copy(Orthanc::ImageAccessor& target,
                     const Orthanc::ImageAccessor& source)
    {
      Embed(target, source, 0, 0);
    }

    void Set(Orthanc::ImageAccessor& image,
             uint8_t r,
             uint8_t g,
             uint8_t b);

    Orthanc::ImageAccessor* DecodeTile(const std::string& source,
                                       ImageCompression compression);
    
    void EncodeTile(std::string& target,
                    const Orthanc::ImageAccessor& source,
                    ImageCompression compression,
                    uint8_t quality);  // Only for JPEG compression

    void ChangeTileCompression(std::string& target,
                               const std::string& source,
                               ImageCompression sourceCompression,
                               ImageCompression targetCompression,
                               uint8_t quality);  // Only for JPEG compression

    Orthanc::ImageAccessor* Halve(const Orthanc::ImageAccessor& source,
                                  bool smooth);

    Orthanc::ImageAccessor* Clone(const Orthanc::ImageAccessor& accessor);

    Orthanc::ImageAccessor* Render(ITiledPyramid& pyramid,
                                   unsigned int level);
  }
}
