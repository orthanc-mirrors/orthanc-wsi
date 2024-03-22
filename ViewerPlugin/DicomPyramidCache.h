/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2024 Osimis S.A., Belgium
 * Copyright (C) 2021-2024 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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

#include "../Framework/Inputs/DicomPyramid.h"

#include <Cache/LeastRecentlyUsedIndex.h>

#include <boost/thread/mutex.hpp>
#include <string>


namespace OrthancWSI
{
  class DicomPyramidCache : public boost::noncopyable
  {
  private:
    typedef Orthanc::LeastRecentlyUsedIndex<std::string, DicomPyramid*>  Cache;

    std::unique_ptr<OrthancStone::IOrthancConnection>  orthanc_;

    boost::mutex  mutex_;
    size_t        maxSize_;
    Cache         cache_;

    DicomPyramidCache(OrthancStone::IOrthancConnection* orthanc /* takes ownership */,
                      size_t maxSize);

    DicomPyramid* GetCachedPyramid(const std::string& seriesId);

    DicomPyramid& GetPyramid(const std::string& seriesId,
                             boost::mutex::scoped_lock& lock);

  public:
    ~DicomPyramidCache();

    static void InitializeInstance(size_t maxSize);

    static void FinalizeInstance();

    static DicomPyramidCache& GetInstance();

    void Invalidate(const std::string& seriesId);

    class Locker : public boost::noncopyable
    {
    private:
      DicomPyramidCache&         cache_;
      boost::mutex::scoped_lock  lock_;
      DicomPyramid&              pyramid_;

    public:
      explicit Locker(const std::string& seriesId);

      DicomPyramid& GetPyramid() const
      {
        return pyramid_;
      }
    };
  };
}
