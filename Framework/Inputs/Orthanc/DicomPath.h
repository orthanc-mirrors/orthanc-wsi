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


#pragma once

#include "DicomTag.h"

#include <vector>
#include <stddef.h>

namespace OrthancPlugins
{
  class DicomPath
  {
  private:
    typedef std::pair<DicomTag, size_t>  Prefix;

    std::vector<Prefix>  prefix_;
    DicomTag             finalTag_;

    const Prefix& GetPrefixItem(size_t depth) const;

    Prefix& GetPrefixItem(size_t depth);

  public:
    DicomPath(const DicomTag& finalTag) :
    finalTag_(finalTag)
    {
    }

    DicomPath(const DicomTag& sequence,
              size_t index,
              const DicomTag& tag);

    DicomPath(const DicomTag& sequence1,
              size_t index1,
              const DicomTag& sequence2,
              size_t index2,
              const DicomTag& tag);

    DicomPath(const DicomTag& sequence1,
              size_t index1,
              const DicomTag& sequence2,
              size_t index2,
              const DicomTag& sequence3,
              size_t index3,
              const DicomTag& tag);

    void AddToPrefix(const DicomTag& tag,
                     size_t position)
    {
      prefix_.push_back(std::make_pair(tag, position));
    }

    size_t GetPrefixLength() const
    {
      return prefix_.size();
    }
    
    DicomTag GetPrefixTag(size_t depth) const
    {
      return GetPrefixItem(depth).first;
    }

    size_t GetPrefixIndex(size_t depth) const
    {
      return GetPrefixItem(depth).second;
    }

    void SetPrefixIndex(size_t depth,
                        size_t value)
    {
      GetPrefixItem(depth).second = value;
    }
    
    const DicomTag& GetFinalTag() const
    {
      return finalTag_;
    }

    void SetFinalTag(const DicomTag& tag)
    {
      finalTag_ = tag;
    }

    std::string Format() const;
  };
}
