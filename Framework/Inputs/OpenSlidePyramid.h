/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017 Osimis, Belgium
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

#include "DecodedTiledPyramid.h"
#include "OpenSlideLibrary.h"

namespace OrthancWSI
{
  class OpenSlidePyramid : public DecodedTiledPyramid
  {
  private:
    OpenSlideLibrary::Image  image_;
    unsigned int             tileWidth_;
    unsigned int             tileHeight_;

  protected:
    virtual void ReadRegion(Orthanc::ImageAccessor& target,
                            unsigned int level,
                            unsigned int x,
                            unsigned int y);

  public:
    OpenSlidePyramid(const std::string& path,
                     unsigned int tileWidth,
                     unsigned int tileHeight);

    virtual unsigned int GetTileWidth() const
    {
      return tileWidth_;
    }

    virtual unsigned int GetTileHeight() const
    {
      return tileHeight_;
    }

    virtual unsigned int GetLevelCount() const
    {
      return image_.GetLevelCount();
    }

    virtual unsigned int GetLevelWidth(unsigned int level) const
    {
      return image_.GetLevelWidth(level);
    }

    virtual unsigned int GetLevelHeight(unsigned int level) const
    {
      return image_.GetLevelHeight(level);
    }

    virtual Orthanc::PixelFormat GetPixelFormat() const
    {
      return Orthanc::PixelFormat_RGB24;
    }
  };
}
