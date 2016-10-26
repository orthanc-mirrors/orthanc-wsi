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


#pragma once

#include "../Enumerations.h"

#include "../Orthanc/Core/Images/ImageAccessor.h"

#include <boost/noncopyable.hpp>
#include <string>


namespace OrthancWSI
{
  /**
   * Class that represents a whole-slide image. It is assumed to be
   * thread-safe, which is the case of libtiff and openslide.
   **/
  class ITiledPyramid : public boost::noncopyable
  {
  public:
    virtual ~ITiledPyramid()
    {
    }

    virtual unsigned int GetLevelCount() const = 0;

    virtual unsigned int GetLevelWidth(unsigned int level) const = 0;

    virtual unsigned int GetLevelHeight(unsigned int level) const = 0;

    virtual unsigned int GetTileWidth() const = 0;

    virtual unsigned int GetTileHeight() const = 0;

    virtual bool ReadRawTile(std::string& tile,
                             unsigned int level,
                             unsigned int tileX,
                             unsigned int tileY) = 0;

    virtual Orthanc::ImageAccessor* DecodeTile(unsigned int level,
                                               unsigned int tileX,
                                               unsigned int tileY) = 0;

    // Only makes sense for images with raw access to tiles
    virtual ImageCompression GetImageCompression() const = 0;

    virtual Orthanc::PixelFormat GetPixelFormat() const = 0;
  };
}