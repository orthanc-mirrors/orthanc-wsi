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

#include "PyramidWriterBase.h"

#include <tiff.h>
#include <tiffio.h>
#include <deque>
#include <vector>
#include <boost/thread.hpp>

namespace OrthancWSI
{
  class HierarchicalTiffWriter : public PyramidWriterBase
  {
  private:
    class PendingTile;
    struct Comparator;

    TIFF* tiff_;

    boost::mutex              mutex_;
    std::deque<PendingTile*>  pending_;
    std::vector<Level>        levels_;
    unsigned int              currentLevel_;
    unsigned int              nextX_;
    unsigned int              nextY_;
    bool                      isFirst_;
 
    void Close()
    {
      TIFFClose(tiff_);
    }

    void StoreTile(const std::string& tile,
                   unsigned int tileX,
                   unsigned int tileY);

    void ConfigureLevel(const Level& level,
                        bool createLevel);

    void AdvanceToNextTile();

    void ScanPending();


  protected:
    virtual void WriteRawTileInternal(const std::string& tile,
                                      const Level& level,
                                      unsigned int tileX,
                                      unsigned int tileY);
        
    virtual void AddLevelInternal(const Level& level);


  public:
    HierarchicalTiffWriter(const std::string& path,
                           Orthanc::PixelFormat pixelFormat, 
                           ImageCompression compression,
                           unsigned int tileWidth,
                           unsigned int tileHeight);

    virtual ~HierarchicalTiffWriter();

    virtual void Flush();
  };
}