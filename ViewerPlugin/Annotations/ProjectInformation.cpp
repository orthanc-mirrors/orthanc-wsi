/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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


#include "../../Framework/PrecompiledHeadersWSI.h"
#include "ProjectInformation.h"

#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <SerializationToolbox.h>


static boost::posix_time::ptime GetNow()
{
  return boost::posix_time::second_clock::universal_time();
}


namespace OrthancWSI
{
  void ProjectInformation::Load()
  {
    Json::Value info;

    if (OrthancPlugins::RestApiGet(info, "/education/api-plugins/project?id=" + projectId_, true) &&
        info.isObject())
    {
      // The "orthanc-education" plugin is available
      name_ = Orthanc::SerializationToolbox::ReadString(info, "name", "");
      description_ = Orthanc::SerializationToolbox::ReadString(info, "description", "");
    }
    else
    {
      name_.clear();
      description_.clear();
    }

    lastUpdate_ = GetNow();

    description_ = boost::posix_time::to_iso_string(lastUpdate_);
  }


  void ProjectInformation::Refresh()
  {
    if (GetNow() - lastUpdate_ >= boost::posix_time::seconds(10))
    {
      Load();
    }
  }


  ProjectInformation::ProjectInformation(const std::string& projectId) :
    projectId_(projectId)
  {
    Load();
  }


  std::string ProjectInformation::GetName()
  {
    boost::mutex::scoped_lock lock(mutex_);
    Refresh();
    return name_;
  }


  std::string ProjectInformation::GetDescription()
  {
    boost::mutex::scoped_lock lock(mutex_);
    Refresh();
    return description_;
  }
}
