/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2025 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2025 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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
#include "OrthancPluginConnection.h"

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <OrthancException.h>

namespace OrthancWSI
{
  void OrthancPluginConnection::RestApiGet(std::string& result,
                                           const std::string& uri) 
  {
    OrthancPlugins::MemoryBuffer buffer;

    if (buffer.RestApiGet(uri, false))
    {
      buffer.ToString(result);
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }
  }


  void OrthancPluginConnection::RestApiPost(std::string& result,
                                            const std::string& uri,
                                            const std::string& body)
  {
    OrthancPlugins::MemoryBuffer buffer;

    if (buffer.RestApiPost(uri, body.c_str(), body.size(), false))
    {
      buffer.ToString(result);
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }
  }


  void OrthancPluginConnection::RestApiPut(std::string& result,
                                           const std::string& uri,
                                           const std::string& body)
  {
    OrthancPlugins::MemoryBuffer buffer;

    if (buffer.RestApiPut(uri, body.c_str(), body.size(), false))
    {
      buffer.ToString(result);
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }
  }


  void OrthancPluginConnection::RestApiDelete(const std::string& uri)
  {
    OrthancPlugins::MemoryBuffer buffer;

    if (!::OrthancPlugins::RestApiDelete(uri, false))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }
  }
}
