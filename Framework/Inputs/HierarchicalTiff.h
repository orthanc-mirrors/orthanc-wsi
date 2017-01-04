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

#include "PyramidWithRawTiles.h"

#include <tiff.h>
#include <tiffio.h>
#include <vector>
#include <boost/thread.hpp>

namespace OrthancWSI
{
  class HierarchicalTiff : public PyramidWithRawTiles
  {
  private:
    struct Level
    {
      tdir_t    directory_;
      unsigned int  width_;
      unsigned int  height_;
      std::string  headers_;

      Level(TIFF* tiff,
            tdir_t    directory,
            unsigned int  width,
            unsigned int  height);
    };

    struct Comparator;

    boost::mutex          mutex_;
    TIFF*                 tiff_;
    Orthanc::PixelFormat  pixelFormat_;
    ImageCompression      compression_;
    unsigned int          tileWidth_;
    unsigned int          tileHeight_;
    std::vector<Level>    levels_;

    void Finalize();

    void CheckLevel(unsigned int level) const;

    bool GetCurrentCompression(ImageCompression& compression);

    bool GetCurrentPixelFormat(Orthanc::PixelFormat& pixelFormat,
                               ImageCompression compression);

    bool Initialize();

  public:
    HierarchicalTiff(const std::string& path);

    virtual ~HierarchicalTiff()
    {
      Finalize();
    }

    virtual unsigned int GetLevelCount() const
    {
      return levels_.size();
    }

    virtual unsigned int GetLevelWidth(unsigned int level) const;

    virtual unsigned int GetLevelHeight(unsigned int level) const;

    virtual unsigned int GetTileWidth() const
    {
      return tileWidth_;
    }

    virtual unsigned int GetTileHeight() const
    {
      return tileHeight_;
    }

    virtual bool ReadRawTile(std::string& tile,
                             ImageCompression& compression,
                             unsigned int level,
                             unsigned int tileX,
                             unsigned int tileY);

    virtual Orthanc::PixelFormat GetPixelFormat() const
    {
      return pixelFormat_;
    }

    ImageCompression GetImageCompression()
    {
      return compression_;
    }
  };
}
