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

#include "IDicomDataset.h"

#include <memory>
#include <vector>

namespace OrthancPlugins
{
  class DicomDatasetReader : public boost::noncopyable
  {
  private:
    const IDicomDataset&  dataset_;

  public:
    DicomDatasetReader(const IDicomDataset& dataset);

    const IDicomDataset& GetDataset() const
    {
      return dataset_;
    }

    std::string GetStringValue(const DicomPath& path,
                               const std::string& defaultValue) const;

    std::string GetMandatoryStringValue(const DicomPath& path) const;

    bool GetIntegerValue(int& target,
                         const DicomPath& path) const;

    bool GetUnsignedIntegerValue(unsigned int& target,
                                 const DicomPath& path) const;

    bool GetFloatValue(float& target,
                       const DicomPath& path) const;

    bool GetDoubleValue(double& target,
                        const DicomPath& path) const;
  };
}
