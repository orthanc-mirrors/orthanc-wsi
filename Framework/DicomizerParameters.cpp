/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2018 Osimis S.A., Belgium
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


#include "PrecompiledHeadersWSI.h"
#include "DicomizerParameters.h"

#include "Targets/FolderTarget.h"
#include "Targets/OrthancTarget.h"

#include "../Resources/Orthanc/Core/OrthancException.h"

#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>

namespace OrthancWSI
{
  static unsigned int ChooseNumberOfThreads()
  {
    unsigned int nthreads = boost::thread::hardware_concurrency();

    if (nthreads % 2 == 0)
    {
      nthreads = nthreads / 2;
    }
    else
    {
      nthreads = nthreads / 2 + 1;
    }

    if (nthreads == 0)
    {
      nthreads = 1;
    }

    return nthreads;
  }


  DicomizerParameters::DicomizerParameters() :
    safetyCheck_(false),
    repaintBackground_(false),
    targetCompression_(ImageCompression_Jpeg),
    hasTargetTileSize_(false),
    targetTileWidth_(512),
    targetTileHeight_(512),
    maxDicomFileSize_(10 * 1024 * 1024),   // 10MB
    reconstructPyramid_(false),
    pyramidLevelsCount_(0),
    pyramidLowerLevelsCount_(0),
    smooth_(false),
    jpegQuality_(90),
    forceReencode_(false),
    opticalPath_(OpticalPath_Brightfield)
  {
    backgroundColor_[0] = 255;
    backgroundColor_[1] = 255;
    backgroundColor_[2] = 255;
    threadsCount_ = ChooseNumberOfThreads();
  }


  void DicomizerParameters::SetBackgroundColor(uint8_t red,
                                               uint8_t green,
                                               uint8_t blue)
  {
    repaintBackground_ = true;
    backgroundColor_[0] = red;
    backgroundColor_[1] = green;
    backgroundColor_[2] = blue;
  }


  void DicomizerParameters::SetTargetTileSize(unsigned int width,
                                              unsigned int height)
  {
    hasTargetTileSize_ = true;
    targetTileWidth_ = width;
    targetTileHeight_ = height;
  }


  unsigned int DicomizerParameters::GetTargetTileWidth(unsigned int defaultWidth) const
  {
    if (hasTargetTileSize_ &&
        targetTileWidth_ != 0)
    {
      return targetTileWidth_;
    }
    else
    {
      return defaultWidth;
    }
  }


  unsigned int DicomizerParameters::GetTargetTileHeight(unsigned int defaultHeight) const
  {
    if (hasTargetTileSize_ &&
        targetTileHeight_ != 0)
    {
      return targetTileHeight_;
    }
    else
    {
      return defaultHeight;
    }
  }


  void DicomizerParameters::SetThreadsCount(unsigned int threads)
  {
    if (threads == 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    threadsCount_ = threads;
  }


  void DicomizerParameters::SetDicomMaxFileSize(unsigned int size)
  {
    maxDicomFileSize_ = size;
  }


  void DicomizerParameters::SetPyramidLevelsCount(unsigned int count)
  {
    if (count == 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    pyramidLevelsCount_ = count;
  }


  unsigned int DicomizerParameters::GetPyramidLevelsCount(const IPyramidWriter& target,
                                                          const ITiledPyramid& source) const
  {
    if (!reconstructPyramid_)
    {
      // Only makes sense if reconstructing the pyramid
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }

    if (pyramidLevelsCount_ != 0)
    {
      return pyramidLevelsCount_;
    }

    // By default, the number of levels for the pyramid is taken so that
    // the upper level is reduced either to 1 column of tiles, or to 1
    // row of tiles.
    unsigned int totalWidth = source.GetLevelWidth(0);
    unsigned int totalHeight = source.GetLevelHeight(0);

    unsigned int countLevels = 1;
    for (;;)
    {
      unsigned int zoom = 1 << (countLevels - 1);

      if (CeilingDivision(totalWidth, zoom) <= target.GetTileWidth() ||
          CeilingDivision(totalHeight, zoom) <= target.GetTileHeight())
      {
        break;
      }

      countLevels += 1;
    }

    return countLevels;
  }


  void DicomizerParameters::SetPyramidLowerLevelsCount(unsigned int count)
  {
    if (count == 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    pyramidLowerLevelsCount_ = count;
  }


  unsigned int DicomizerParameters::GetPyramidLowerLevelsCount(const IPyramidWriter& target,
                                                               const ITiledPyramid& source) const
  {
    if (!reconstructPyramid_)
    {
      // Only makes sense if reconstructing the pyramid
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }

    if (pyramidLowerLevelsCount_ != 0)
    {
      return pyramidLowerLevelsCount_;
    }

    unsigned int fullNumberOfTiles = (
      CeilingDivision(source.GetLevelWidth(0), source.GetTileWidth()) * 
      CeilingDivision(source.GetLevelHeight(0), source.GetTileHeight()));

    // By default, the number of lower levels in the pyramid is chosen
    // as a compromise between the number of tasks (there should not be
    // too few tasks, otherwise multithreading would not be efficient)
    // and memory consumption (maximum 64MB of RAM due to the decoding
    // of the tiles of the source image per thread: cf. PyramidReader).
    unsigned int result = 1;
    for (;;)
    {
      unsigned int zoom = 1 << (result - 1);
      unsigned int numberOfTiles = CeilingDivision(fullNumberOfTiles, zoom * zoom);

      if (result + 1 > target.GetLevelCount() ||
          numberOfTiles < 4 * GetThreadsCount() ||
          zoom * target.GetTileWidth() > 4096 ||
          zoom * target.GetTileHeight() > 4096)
      {
        break;
      }

      result += 1;
    }

    return result - 1;
  }


  void DicomizerParameters::SetJpegQuality(int quality)
  {
    if (quality <= 0 ||
        quality > 100)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    jpegQuality_ = quality;
  }


  IFileTarget* DicomizerParameters::CreateTarget() const
  {
    if (folder_.empty() ||
        folderPattern_.empty())
    {
      return new OrthancTarget(orthanc_);
    }
    else
    {
      return new FolderTarget(folder_ + "/" + folderPattern_);
    }
  }
}
