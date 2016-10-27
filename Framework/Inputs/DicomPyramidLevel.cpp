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


#include "../PrecompiledHeadersWSI.h"
#include "DicomPyramidLevel.h"

#include "../Orthanc/Core/Logging.h"
#include "../Orthanc/Core/OrthancException.h"

#include <boost/lexical_cast.hpp>

namespace OrthancWSI
{
  void DicomPyramidLevel::RegisterFrame(const DicomPyramidInstance& instance,
                                        unsigned int frame)
  {
    TileLocation location(instance.GetFrameLocationX(frame), 
                          instance.GetFrameLocationY(frame));

    if (tiles_.find(location) != tiles_.end())
    {
      LOG(ERROR) << "Tile with location (" << location.first << "," 
                 << location.second << ") is indexed twice in level of size "
                 << totalWidth_ << "x" << totalHeight_;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    tiles_[location] = std::make_pair(&instance, frame);
  }


  bool DicomPyramidLevel::LookupTile(TileContent& tile,
                                     unsigned int tileX,
                                     unsigned int tileY) const
  {
    Tiles::const_iterator found = tiles_.find(std::make_pair(tileX, tileY));
    if (found == tiles_.end())
    {
      return false;
    }
    else
    {
      tile = found->second;
      return true;
    }
  }


  DicomPyramidLevel::DicomPyramidLevel(const DicomPyramidInstance& instance) :
    totalWidth_(instance.GetTotalWidth()),
    totalHeight_(instance.GetTotalHeight()),
    tileWidth_(instance.GetTileWidth()),
    tileHeight_(instance.GetTileHeight())
  {
    if (totalWidth_ == 0 ||
        totalHeight_ == 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
      
    AddInstance(instance);
  }


  void DicomPyramidLevel::AddInstance(const DicomPyramidInstance& instance)
  {
    if (instance.GetTotalWidth() != totalWidth_ ||
        instance.GetTotalHeight() != totalHeight_ ||
        instance.GetTileWidth() != tileWidth_ ||
        instance.GetTileHeight() != tileHeight_)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageSize);
    }

    instances_.push_back(&instance);

    for (size_t frame = 0; frame < instance.GetFrameCount(); frame++)
    {
      RegisterFrame(instance, frame);
    }
  }


  bool DicomPyramidLevel::DownloadRawTile(ImageCompression& compression /* out */,
                                          Orthanc::PixelFormat& format /* out */,
                                          std::string& raw /* out */,
                                          IOrthancConnection& orthanc,
                                          unsigned int tileX,
                                          unsigned int tileY) const
  {
    TileContent tile;
    if (LookupTile(tile, tileX, tileY))
    {
      assert(tile.first != NULL);
      const DicomPyramidInstance& instance = *tile.first;

      std::string uri = ("/instances/" + instance.GetInstanceId() + 
                         "/frames/" + boost::lexical_cast<std::string>(tile.second) + "/raw");

      orthanc.RestApiGet(raw, uri);

      compression = instance.GetImageCompression();
      format = instance.GetPixelFormat();

      return true;
    }
    else
    {
      return false;
    }
  }
}
