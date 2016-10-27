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
#include "CurlOrthancConnection.h"

#include "../Orthanc/Core/OrthancException.h"

namespace OrthancWSI
{
  void CurlOrthancConnection::ApplyGet(std::string& result,
                                       const std::string& uri)
  {
    Orthanc::HttpClient client(parameters_, uri);

    // Don't follow 3xx HTTP (avoid redirections to "unsupported.png" in Orthanc)
    client.SetRedirectionFollowed(false);  
   
    if (!client.Apply(result))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }
  }

  void CurlOrthancConnection::ApplyPost(std::string& result,
                                        const std::string& uri,
                                        const std::string& body)
  {
    Orthanc::HttpClient client(parameters_, uri);

    client.SetMethod(Orthanc::HttpMethod_Post);
    client.SetBody(body);

    // Don't follow 3xx HTTP (avoid redirections to "unsupported.png" in Orthanc)
    client.SetRedirectionFollowed(false);  
   
    if (!client.Apply(result))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }
  }
}
