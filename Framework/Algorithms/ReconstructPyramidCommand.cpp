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
#include "ReconstructPyramidCommand.h"

#include "../ImageToolbox.h"
#include "../../Resources/Orthanc/Core/Logging.h"
#include "../../Resources/Orthanc/Core/OrthancException.h"
#include "../../Resources/Orthanc/Core/Images/Image.h"

#include <cassert>

namespace OrthancWSI
{
  Orthanc::ImageAccessor* ReconstructPyramidCommand::Explore(unsigned int level,
                                                             unsigned int offsetX,
                                                             unsigned int offsetY)
  {
    unsigned int zoom = 1 << level;
    assert(x_ % zoom == 0 && y_ % zoom == 0);
    unsigned int x = x_ / zoom + offsetX;
    unsigned int y = y_ / zoom + offsetY;

    if (x >= target_.GetCountTilesX(level + shiftTargetLevel_) ||
        y >= target_.GetCountTilesY(level + shiftTargetLevel_))
    {
      return NULL;
    }

    std::auto_ptr<Orthanc::ImageAccessor> result;

    if (level == 0)
    {
      result.reset(new Orthanc::ImageAccessor(source_.GetDecodedTile(x, y)));

      ImageCompression compression;
      const std::string* rawTile = source_.GetRawTile(compression, x, y);

      if (rawTile != NULL)
      {
        // Simple transcoding
        target_.WriteRawTile(*rawTile, compression, level + shiftTargetLevel_, x, y);
      }
      else
      {
        // Re-encoding the file
        target_.EncodeTile(*result, level + shiftTargetLevel_, x, y);
      }
    }
    else
    {
      std::auto_ptr<Orthanc::ImageAccessor> mosaic(ImageToolbox::Allocate(source_.GetPixelFormat(), 
                                                                          2 * target_.GetTileWidth(), 
                                                                          2 * target_.GetTileHeight()));
      ImageToolbox::Set(*mosaic, 
                        source_.GetParameters().GetBackgroundColorRed(),
                        source_.GetParameters().GetBackgroundColorGreen(),
                        source_.GetParameters().GetBackgroundColorBlue());

      {
        std::auto_ptr<Orthanc::ImageAccessor> subTile(Explore(level - 1, 2 * offsetX, 2 * offsetY));
        if (subTile.get() != NULL)
        {
          ImageToolbox::Embed(*mosaic, *subTile, 0, 0);
        }
      }

      {
        std::auto_ptr<Orthanc::ImageAccessor> subTile(Explore(level - 1, 2 * offsetX + 1, 2 * offsetY));
        if (subTile.get() != NULL)
        {
          ImageToolbox::Embed(*mosaic, *subTile, target_.GetTileWidth(), 0);
        }
      }

      {
        std::auto_ptr<Orthanc::ImageAccessor> subTile(Explore(level - 1, 2 * offsetX, 2 * offsetY + 1));
        if (subTile.get() != NULL)
        {
          ImageToolbox::Embed(*mosaic, *subTile, 0, target_.GetTileHeight());
        }
      }

      {
        std::auto_ptr<Orthanc::ImageAccessor> subTile(Explore(level - 1, 2 * offsetX + 1, 2 * offsetY + 1));
        if (subTile.get() != NULL)
        {
          ImageToolbox::Embed(*mosaic, *subTile, target_.GetTileWidth(), target_.GetTileHeight());
        }
      }

      result.reset(ImageToolbox::Halve(*mosaic, source_.GetParameters().IsSmoothEnabled()));

      target_.EncodeTile(*result, level + shiftTargetLevel_, x, y);
    }

    return result.release();
  }

  
  ReconstructPyramidCommand::ReconstructPyramidCommand(IPyramidWriter& target,
                                                       ITiledPyramid& source,
                                                       unsigned int upToLevel,
                                                       unsigned int x,
                                                       unsigned int y,
                                                       const DicomizerParameters& parameters) :
    target_(target),
    source_(source, 0, target.GetTileWidth(), target.GetTileHeight(), parameters),
    upToLevel_(upToLevel),
    x_(x),
    y_(y),
    shiftTargetLevel_(0)
  {
    unsigned int zoom = 1 << upToLevel;
    if (x % zoom != 0 ||
        y % zoom != 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }

    if (target.GetPixelFormat() != source.GetPixelFormat())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageFormat);
    }
  }


  bool ReconstructPyramidCommand::Execute()
  {
    std::auto_ptr<Orthanc::ImageAccessor> root(Explore(upToLevel_, 0, 0));
    return true;
  }


  void ReconstructPyramidCommand::PrepareBagOfTasks(Orthanc::BagOfTasks& tasks,
                                                    IPyramidWriter& target,
                                                    ITiledPyramid& source,
                                                    unsigned int countLevels,
                                                    unsigned int shiftTargetLevel,    
                                                    const DicomizerParameters& parameters)
  {
    if (countLevels == 0)
    {
      return;
    }

    if (shiftTargetLevel + countLevels > target.GetLevelCount())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    const unsigned int targetCountTilesX = target.GetCountTilesX(shiftTargetLevel);
    const unsigned int targetCountTilesY = target.GetCountTilesY(shiftTargetLevel);
    const unsigned int step = 1 << (countLevels - 1);

    for (unsigned int y = 0; y < targetCountTilesY; y += step)
    {
      for (unsigned int x = 0; x < targetCountTilesX; x += step)
      {
        std::auto_ptr<ReconstructPyramidCommand> command;
        command.reset(new ReconstructPyramidCommand
                      (target, source, countLevels - 1, x, y, parameters));
        command->SetShiftTargetLevel(shiftTargetLevel);
        tasks.Push(command.release());
      }
    }
  }
}
