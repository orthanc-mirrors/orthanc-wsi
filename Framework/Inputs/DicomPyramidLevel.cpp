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
  DicomPyramidLevel::TileContent& DicomPyramidLevel::GetTileContent(unsigned int tileX,
                                                                    unsigned int tileY)
  {
    if (tileX >= countTilesX_ ||
        tileY >= countTilesY_)
    {
      LOG(ERROR) << "Tile location (" << tileX << "," << tileY << ") is outside the image";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    return tiles_[tileY * countTilesX_ + tileX];
  }

  void DicomPyramidLevel::RegisterFrame(const DicomPyramidInstance& instance,
                                        unsigned int frame)
  {
    unsigned int tileX = instance.GetFrameLocationX(frame);
    unsigned int tileY = instance.GetFrameLocationY(frame);
    TileContent& tile = GetTileContent(tileX, tileY);

    if (tile.instance_ != NULL)
    {
      LOG(ERROR) << "Tile with location (" << tileX << "," 
                 << tileY << ") is indexed twice in level of size "
                 << totalWidth_ << "x" << totalHeight_;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    tile.instance_ = &instance;
    tile.frame_ = frame;
  }


  bool DicomPyramidLevel::LookupTile(TileContent& tile,
                                     unsigned int tileX,
                                     unsigned int tileY) const
  {
    const TileContent& tmp = const_cast<DicomPyramidLevel&>(*this).GetTileContent(tileX, tileY);

    if (tmp.instance_ == NULL)
    {
      return false;
    }
    else
    {
      tile = tmp;
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

    countTilesX_ = CeilingDivision(totalWidth_, tileWidth_);
    countTilesY_ = CeilingDivision(totalHeight_, tileHeight_);
    tiles_.resize(countTilesX_ * countTilesY_);
      
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
      assert(tile.instance_ != NULL);
      const DicomPyramidInstance& instance = *tile.instance_;

      std::string uri = ("/instances/" + instance.GetInstanceId() + 
                         "/frames/" + boost::lexical_cast<std::string>(tile.frame_) + "/raw");

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
