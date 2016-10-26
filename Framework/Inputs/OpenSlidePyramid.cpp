/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
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


#include "OpenSlidePyramid.h"

#include "../Orthanc/Core/Images/ImageProcessing.h"
#include "../Orthanc/Core/OrthancException.h"
#include "../Orthanc/Core/Logging.h"

namespace OrthancWSI
{
  void OpenSlidePyramid::ReadRegion(Orthanc::ImageAccessor& target,
                                    unsigned int level,
                                    unsigned int x,
                                    unsigned int y)
  {
    std::auto_ptr<Orthanc::ImageAccessor> source(image_.ReadRegion(level, x, y, target.GetWidth(), target.GetHeight()));
    Orthanc::ImageProcessing::Convert(target, *source);
  }


  OpenSlidePyramid::OpenSlidePyramid(const std::string& path,
                                     unsigned int tileWidth,
                                     unsigned int tileHeight) :
    image_(path),
    tileWidth_(tileWidth),
    tileHeight_(tileHeight)
  {
  }
}