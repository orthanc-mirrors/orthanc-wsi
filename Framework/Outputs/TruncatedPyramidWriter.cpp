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
#include "TruncatedPyramidWriter.h"

#include "../../Resources/Orthanc/Core/OrthancException.h"

namespace OrthancWSI
{
  TruncatedPyramidWriter::TruncatedPyramidWriter(IPyramidWriter& lower,
                                                 unsigned int upperLevelIndex) :
    lowerLevels_(lower),
    upperLevel_(lower.GetPixelFormat(),
                lower.GetCountTilesX(upperLevelIndex),
                lower.GetCountTilesY(upperLevelIndex),
                lower.GetTileWidth(),
                lower.GetTileHeight()),
    upperLevelIndex_(upperLevelIndex)
  {
    if (upperLevelIndex > lower.GetLevelCount())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  unsigned int TruncatedPyramidWriter::GetCountTilesX(unsigned int level) const
  {
    if (level < upperLevelIndex_)
    {
      return lowerLevels_.GetCountTilesX(level);
    }
    else if (level == upperLevelIndex_)
    {
      return upperLevel_.GetCountTilesX(0);
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  unsigned int TruncatedPyramidWriter::GetCountTilesY(unsigned int level) const
  {
    if (level < upperLevelIndex_)
    {
      return lowerLevels_.GetCountTilesY(level);
    }
    else if (level == upperLevelIndex_)
    {
      return upperLevel_.GetCountTilesY(0);
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  void TruncatedPyramidWriter::WriteRawTile(const std::string& tile,
                                            ImageCompression compression,
                                            unsigned int level,
                                            unsigned int x,
                                            unsigned int y)
  {
    if (level < upperLevelIndex_)
    {
      lowerLevels_.WriteRawTile(tile, compression, level, x, y);
    }
    else if (level == upperLevelIndex_)
    {
      upperLevel_.WriteRawTile(tile, compression, 0, x, y);
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  void TruncatedPyramidWriter::EncodeTile(const Orthanc::ImageAccessor& tile,
                                          unsigned int level,
                                          unsigned int x, 
                                          unsigned int y)
  {
    if (level < upperLevelIndex_)
    {
      lowerLevels_.EncodeTile(tile, level, x, y);
    }
    else if (level == upperLevelIndex_)
    {
      upperLevel_.EncodeTile(tile, 0, x, y);
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }
}
