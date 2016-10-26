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

#include "PyramidReader.h"
#include "../Outputs/IPyramidWriter.h"
#include "../Orthanc/Core/MultiThreading/BagOfTasks.h"


namespace OrthancWSI
{
  class ReconstructPyramidCommand : public Orthanc::ICommand
  {
  private:
    IPyramidWriter& target_;
    PyramidReader  source_;

    unsigned int upToLevel_;
    unsigned int x_;
    unsigned int y_;
    unsigned int shiftTargetLevel_;

    Orthanc::ImageAccessor* Explore(unsigned int level,
                                    unsigned int offsetX,
                                    unsigned int offsetY);

  public:
    ReconstructPyramidCommand(IPyramidWriter& target,
                              ITiledPyramid& source,
                              unsigned int upToLevel,
                              unsigned int x,
                              unsigned int y,
                              const DicomizerParameters& parameters);

    void SetShiftTargetLevel(unsigned int shift)
    {
      shiftTargetLevel_ = shift;
    }

    unsigned int GetShiftTargetLevel() const
    {
      return shiftTargetLevel_;
    }

    virtual bool Execute();

    static void PrepareBagOfTasks(Orthanc::BagOfTasks& tasks,
                                  IPyramidWriter& target,
                                  ITiledPyramid& source,
                                  unsigned int countLevels,
                                  unsigned int shiftTargetLevel,    
                                  const DicomizerParameters& parameters);
  };
}