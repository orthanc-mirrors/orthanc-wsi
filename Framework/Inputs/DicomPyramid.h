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

#include "PyramidWithRawTiles.h"
#include "DicomPyramidInstance.h"
#include "DicomPyramidLevel.h"

namespace OrthancWSI
{
  class DicomPyramid : public PyramidWithRawTiles
  {
  private:
    struct Comparator;

    IOrthancConnection&                 orthanc_;
    std::string                         seriesId_;
    std::vector<DicomPyramidInstance*>  instances_;
    std::vector<DicomPyramidLevel*>     levels_;

    void Clear();

    void RegisterInstances(const std::string& seriesId);

    void Check(const std::string& seriesId) const;

    void CheckLevel(size_t level) const;

  public:
    DicomPyramid(IOrthancConnection& orthanc,
                 const std::string& seriesId);

    virtual ~DicomPyramid()
    {
      Clear();
    }

    const std::string& GetSeriesId() const
    {
      return seriesId_;
    }

    virtual unsigned int GetLevelCount() const
    {
      return levels_.size();
    }

    virtual unsigned int GetLevelWidth(unsigned int level) const;

    virtual unsigned int GetLevelHeight(unsigned int level) const;

    virtual unsigned int GetTileWidth() const;

    virtual unsigned int GetTileHeight() const;

    virtual bool ReadRawTile(std::string& tile,
                             unsigned int level,
                             unsigned int tileX,
                             unsigned int tileY);

    virtual ImageCompression GetImageCompression() const;

    virtual Orthanc::PixelFormat GetPixelFormat() const;
  };
}
