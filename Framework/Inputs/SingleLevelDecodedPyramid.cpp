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


#include "../PrecompiledHeadersWSI.h"
#include "SingleLevelDecodedPyramid.h"
#include "../ImageToolbox.h"

#include <OrthancException.h>

namespace OrthancWSI
{
  void SingleLevelDecodedPyramid::ReadRegion(Orthanc::ImageAccessor& target,
                                             unsigned int level,
                                             unsigned int x,
                                             unsigned int y)
  {
    Orthanc::ImageAccessor region;
    image_.GetRegion(region, x, y, target.GetWidth(), target.GetHeight());
    ImageToolbox::Copy(target, region);
  }


  unsigned int SingleLevelDecodedPyramid::GetLevelWidth(unsigned int level) const
  {
    if (level != 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    return image_.GetWidth();
  }


  unsigned int SingleLevelDecodedPyramid::GetLevelHeight(unsigned int level) const
  {
    if (level != 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    return image_.GetHeight();
  }
  

  Orthanc::PhotometricInterpretation SingleLevelDecodedPyramid::GetPhotometricInterpretation() const
  {
    switch (image_.GetFormat())
    {
      case Orthanc::PixelFormat_Grayscale8:
        return Orthanc::PhotometricInterpretation_Monochrome2;

      case Orthanc::PixelFormat_RGB24:
        return Orthanc::PhotometricInterpretation_RGB;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
    }
  }
}
