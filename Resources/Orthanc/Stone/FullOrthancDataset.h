/**
 * Stone of Orthanc
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

#include "IOrthancConnection.h"
#include "IDicomDataset.h"

#include <json/value.h>

namespace OrthancStone
{
  class FullOrthancDataset : public IDicomDataset
  {
  private:
    Json::Value   root_;

    const Json::Value* LookupPath(const DicomPath& path) const;

    void CheckRoot() const;

  public:
    FullOrthancDataset(IOrthancConnection& orthanc,
                       const std::string& uri);

    FullOrthancDataset(const std::string& content);

    FullOrthancDataset(const void* content,
                       size_t size);

    FullOrthancDataset(const Json::Value& root);

    virtual bool GetStringValue(std::string& result,
                                const DicomPath& path) const;

    virtual bool GetSequenceSize(size_t& size,
                                 const DicomPath& path) const;

    FullOrthancDataset* Clone() const
    {
      return new FullOrthancDataset(this->root_);
    }
  };
}
