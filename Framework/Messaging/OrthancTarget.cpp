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
#include "OrthancTarget.h"

#include "CurlOrthancConnection.h"
#include "../DicomToolbox.h"
#include "../Orthanc/Core/OrthancException.h"
#include "../Orthanc/Core/Logging.h"

namespace OrthancWSI
{
  OrthancTarget::OrthancTarget(const Orthanc::WebServiceParameters& parameters) :
    orthanc_(new CurlOrthancConnection(parameters)),
    first_(true)
  {
  }


  void OrthancTarget::Write(const std::string& file)
  {
    Json::Value result;
    IOrthancConnection::RestApiPost(result, *orthanc_, "/instances", file);

    std::string instanceId = DicomToolbox::GetMandatoryStringTag(result, "ID");

    if (first_)
    {
      Json::Value instance;
      IOrthancConnection::RestApiGet(instance, *orthanc_, "/instances/" + instanceId);

      std::string seriesId = DicomToolbox::GetMandatoryStringTag(instance, "ParentSeries");

      LOG(WARNING) << "ID of the whole-slide image series in Orthanc: " << seriesId;
      first_ = false;
    }

    LOG(INFO) << "New instance was added to Orthanc: " << instanceId;
  }
}
