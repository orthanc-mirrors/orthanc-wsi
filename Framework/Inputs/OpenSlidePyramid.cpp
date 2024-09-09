/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2024 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2024 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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


#include "../PrecompiledHeadersWSI.h"
#include "OpenSlidePyramid.h"

#include <Compatibility.h>  // For std::unique_ptr
#include <Images/ImageProcessing.h>
#include <OrthancException.h>
#include <SerializationToolbox.h>
#include <Logging.h>

#include <boost/math/special_functions/round.hpp>
#include <memory>

namespace OrthancWSI
{
  void OpenSlidePyramid::ReadRegion(Orthanc::ImageAccessor& target,
                                    unsigned int level,
                                    unsigned int x,
                                    unsigned int y)
  {
    std::unique_ptr<Orthanc::ImageAccessor> source(image_.ReadRegion(level, x, y, target.GetWidth(), target.GetHeight()));

    if (target.GetWidth() != source->GetWidth() ||
        target.GetHeight() != source->GetHeight())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageSize);
    }

    const unsigned int width = source->GetWidth();
    const unsigned int height = source->GetHeight();

    if (target.GetFormat() == Orthanc::PixelFormat_RGB24 &&
        source->GetFormat() == Orthanc::PixelFormat_BGRA32)
    {
      // Implements alpha blending: https://en.wikipedia.org/wiki/Alpha_compositing#Alpha_blending
      for (unsigned int y = 0; y < height; y++)
      {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(source->GetConstRow(y));
        uint8_t* q = reinterpret_cast<uint8_t*>(target.GetRow(y));
        for (unsigned int x = 0; x < width; x++)
        {
          /**
             Alpha blending using integer arithmetics only (16 bits avoids overflows)

             p = (1 - alpha) * background + alpha * value
             <=> p = (1 - p[3] / 255) * background + p[3] / 255 * value
             <=> p = ((255 - p[3]) * background + p[3] * value) / 255

           **/

          uint16_t alpha = p[3];
          q[0] = static_cast<uint8_t>(((255 - alpha) * backgroundColor_[0] + alpha * p[2]) / 255);
          q[1] = static_cast<uint8_t>(((255 - alpha) * backgroundColor_[1] + alpha * p[1]) / 255);
          q[2] = static_cast<uint8_t>(((255 - alpha) * backgroundColor_[2] + alpha * p[0]) / 255);

          p += 4;
          q += 3;
        }
      }
    }
    else
    {
      Orthanc::ImageProcessing::Convert(target, *source);
    }
  }


  OpenSlidePyramid::OpenSlidePyramid(const std::string& path,
                                     unsigned int tileWidth,
                                     unsigned int tileHeight) :
    image_(path),
    tileWidth_(tileWidth),
    tileHeight_(tileHeight)
  {
    backgroundColor_[0] = 255;
    backgroundColor_[1] = 255;
    backgroundColor_[2] = 255;
  }


  bool OpenSlidePyramid::LookupImagedVolumeSize(float& width,
                                                float& height) const
  {
    std::string s;
    double mppx;
    double mppy;

    if (image_.LookupProperty(s, "openslide.mpp-x") &&
        Orthanc::SerializationToolbox::ParseDouble(mppx, s) &&
        image_.LookupProperty(s, "openslide.mpp-y") &&
        Orthanc::SerializationToolbox::ParseDouble(mppy, s))
    {
      // In the 2 lines below, remember to switch X/Y when going from physical to pixel coordinates!
      width = mppy / 1000.0 * static_cast<double>(image_.GetLevelHeight(0));
      height = mppx / 1000.0 * static_cast<double>(image_.GetLevelWidth(0));
      return true;
    }
    else
    {
      return false;
    }
  }


  void OpenSlidePyramid::SetBackgroundColor(uint8_t red,
                                            uint8_t green,
                                            uint8_t blue)
  {
    backgroundColor_[0] = red;
    backgroundColor_[1] = green;
    backgroundColor_[2] = blue;
  }
}
