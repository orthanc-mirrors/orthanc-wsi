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


#include "../PrecompiledHeadersWSI.h"
#include "InMemoryTiledImage.h"

#include "../ImageToolbox.h"
#include "../../Resources/Orthanc/Core/Logging.h"
#include "../../Resources/Orthanc/Core/OrthancException.h"

namespace OrthancWSI
{
  static void CheckLevel(unsigned int level)
  {
    if (level != 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  InMemoryTiledImage::InMemoryTiledImage(Orthanc::PixelFormat format,
                                         unsigned int countTilesX,
                                         unsigned int countTilesY,
                                         unsigned int tileWidth,
                                         unsigned int tileHeight) :
    format_(format),
    countTilesX_(countTilesX),
    countTilesY_(countTilesY),
    tileWidth_(tileWidth),
    tileHeight_(tileHeight)
  {
  }


  InMemoryTiledImage::~InMemoryTiledImage()
  {
    for (Tiles::iterator it = tiles_.begin(); it != tiles_.end(); ++it)
    {
      delete it->second;
    }
  }


  unsigned int InMemoryTiledImage::GetCountTilesX(unsigned int level) const
  {
    CheckLevel(level);
    return countTilesX_;
  }


  unsigned int InMemoryTiledImage::GetCountTilesY(unsigned int level) const
  {
    CheckLevel(level);
    return countTilesY_;
  }


  unsigned int InMemoryTiledImage::GetLevelWidth(unsigned int level) const
  {
    CheckLevel(level);
    return tileWidth_ * countTilesX_;
  }


  unsigned int InMemoryTiledImage::GetLevelHeight(unsigned int level) const
  { 
    CheckLevel(level);
    return tileHeight_ * countTilesY_;
  }


  bool InMemoryTiledImage::ReadRawTile(std::string& tile,
                                       ImageCompression& compression,
                                       unsigned int level,
                                       unsigned int tileX,
                                       unsigned int tileY)
  {
    CheckLevel(level);
    return false;  // Unavailable
  }


  Orthanc::ImageAccessor* InMemoryTiledImage::DecodeTile(unsigned int level,
                                                         unsigned int tileX,
                                                         unsigned int tileY)
  {
    CheckLevel(level);

    if (tileX >= countTilesX_ ||
        tileY >= countTilesY_)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    {
      boost::mutex::scoped_lock lock(mutex_);

      Tiles::const_iterator it = tiles_.find(std::make_pair(tileX, tileY));
      if (it != tiles_.end())
      {
        return new Orthanc::ImageAccessor(*it->second);
      }
      else
      {
        LOG(ERROR) << "The following tile has not been set: " << tileX << "," << tileY;
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }
    }
  }


  void InMemoryTiledImage::WriteRawTile(const std::string& raw,
                                        ImageCompression compression,
                                        unsigned int level,
                                        unsigned int tileX,
                                        unsigned int tileY)
  {
    std::auto_ptr<Orthanc::ImageAccessor> decoded(ImageToolbox::DecodeTile(raw, compression));
    EncodeTile(*decoded, level, tileX, tileY);
  }
      

  void InMemoryTiledImage::EncodeTile(const Orthanc::ImageAccessor& tile,
                                      unsigned int level,
                                      unsigned int tileX,
                                      unsigned int tileY)
  {
    CheckLevel(level);

    if (tileX >= countTilesX_ ||
        tileY >= countTilesY_)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    {
      boost::mutex::scoped_lock lock(mutex_);

      Tiles::iterator it = tiles_.find(std::make_pair(tileX, tileY));
      if (it == tiles_.end())
      {
        tiles_[std::make_pair(tileX, tileY)] = ImageToolbox::Clone(tile);
      }
      else
      {
        if (it->second)
        {
          delete it->second;
        }

        it->second = ImageToolbox::Clone(tile);
      }
    }
  }
}
