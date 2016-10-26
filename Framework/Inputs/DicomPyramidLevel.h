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

#include "DicomPyramidInstance.h"

#include <list>

namespace OrthancWSI
{
  class DicomPyramidLevel : public boost::noncopyable
  {
  private:
    typedef std::pair<unsigned int, unsigned int>                 TileLocation;
    typedef std::pair<const DicomPyramidInstance*, unsigned int>  TileContent;
    typedef std::map<TileLocation, TileContent>                   Tiles;
    typedef std::list<const DicomPyramidInstance*>                Instances;

    unsigned int   totalWidth_;
    unsigned int   totalHeight_;
    unsigned int   tileWidth_;
    unsigned int   tileHeight_;
    Instances      instances_;
    Tiles          tiles_;

    void RegisterFrame(const DicomPyramidInstance& instance,
                       unsigned int frame);

    bool LookupTile(TileContent& tile,
                    unsigned int tileX,
                    unsigned int tileY) const;

  public:
    DicomPyramidLevel(const DicomPyramidInstance& instance);

    void AddInstance(const DicomPyramidInstance& instance);

    unsigned int GetTotalWidth() const
    {
      return totalWidth_;
    }

    unsigned int GetTotalHeight() const
    {
      return totalHeight_;
    }

    unsigned int GetTileWidth() const
    {
      return tileWidth_;
    }

    unsigned int GetTileHeight() const
    {
      return tileHeight_;
    }

    bool DownloadRawTile(ImageCompression& compression /* out */,
                         Orthanc::PixelFormat& format /* out */,
                         std::string& raw /* out */,
                         IOrthancConnection& orthanc,
                         unsigned int tileX,
                         unsigned int tileY) const;
  };
}