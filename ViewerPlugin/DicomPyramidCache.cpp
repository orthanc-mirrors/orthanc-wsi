/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2020 Osimis S.A., Belgium
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


#include "../Framework/PrecompiledHeadersWSI.h"
#include "DicomPyramidCache.h"

#include <Compatibility.h>  // For std::unique_ptr

#include <cassert>

namespace OrthancWSI
{
  DicomPyramid* DicomPyramidCache::GetCachedPyramid(const std::string& seriesId)
  {
    // Mutex is assumed to be locked
    DicomPyramid* pyramid = NULL;

    // Is the series of interest already cached as a pyramid?
    if (cache_.Contains(seriesId, pyramid))
    {
      if (pyramid == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }

      // Tag the series as the most recently used
      cache_.MakeMostRecent(seriesId);
    }

    return pyramid;
  }


  DicomPyramid& DicomPyramidCache::GetPyramid(const std::string& seriesId,
                                              boost::mutex::scoped_lock& lock)
  {
    // Mutex is assumed to be locked

    {
      DicomPyramid* pyramid = GetCachedPyramid(seriesId);
      if (pyramid != NULL)
      {
        return *pyramid;
      }
    }

    // Unlock the mutex to construct the pyramid (this is a
    // time-consuming operation, we don't want it to block other clients)
    lock.unlock();

    std::unique_ptr<DicomPyramid> pyramid
      (new DicomPyramid(orthanc_, seriesId, true /* use metadata cache */));

    {
      // The pyramid is constructed: Store it into the cache
      lock.lock();

      DicomPyramid* cached = GetCachedPyramid(seriesId);
      if (cached != NULL)
      {
        // The pyramid was already constructed by another request in
        // between, reuse the cached value (the auto_ptr destroys
        // the just-constructed pyramid)
        return *cached;
      }

      if (cache_.GetSize() == maxSize_)
      {
        // The cache has grown too large: First delete the least
        // recently used entry
        DicomPyramid* oldest = NULL;
        cache_.RemoveOldest(oldest);

        if (oldest == NULL)
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
        }
        else
        {
          delete oldest;
        }
      }

      // Now we have at least one free entry in the cache
      assert(cache_.GetSize() < maxSize_);

      // Add a new element to the cache and make it the most
      // recently used entry
      DicomPyramid* payload = pyramid.release();
      cache_.Add(seriesId, payload);
      return *payload;
    }
  }


  DicomPyramidCache::DicomPyramidCache(OrthancStone::IOrthancConnection& orthanc,
                                       size_t maxSize) :
    orthanc_(orthanc),
    maxSize_(maxSize)
  {
  }


  DicomPyramidCache::~DicomPyramidCache()
  {
    while (!cache_.IsEmpty())
    {
      DicomPyramid* pyramid = NULL;
      std::string seriesId = cache_.RemoveOldest(pyramid);

      if (pyramid != NULL)
      {
        delete pyramid;
      }        
    }
  }


  void DicomPyramidCache::Invalidate(const std::string& seriesId)
  {
    boost::mutex::scoped_lock  lock(mutex_);

    if (cache_.Contains(seriesId))
    {
      std::unique_ptr<DicomPyramid> pyramid(cache_.Invalidate(seriesId));

      if (pyramid.get() == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }
    }
  }


  DicomPyramidCache::Locker::Locker(DicomPyramidCache& cache,
                                    const std::string& seriesId) :
    lock_(cache.mutex_),
    pyramid_(cache.GetPyramid(seriesId, lock_))
  {
  }
}
