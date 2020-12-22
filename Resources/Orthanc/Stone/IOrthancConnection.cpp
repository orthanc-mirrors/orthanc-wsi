/**
 * Stone of Orthanc
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2020 Osimis S.A., Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 **/


#include "IOrthancConnection.h"

#include <OrthancException.h>
#include <Toolbox.h>

namespace OrthancStone
{
  void IOrthancConnection::ParseJson(Json::Value& result,
                                     const std::string& content)
  {
    if (!Orthanc::Toolbox::ReadJson(result, content))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }
  }


  void IOrthancConnection::ParseJson(Json::Value& result,
                                     const void* content,
                                     size_t size)
  {
    if (!Orthanc::Toolbox::ReadJson(result, content, size))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }
  }


  void IOrthancConnection::RestApiGet(Json::Value& result,
                                      IOrthancConnection& orthanc,
                                      const std::string& uri)
  {
    std::string content;
    orthanc.RestApiGet(content, uri);
    ParseJson(result, content);
  }


  void IOrthancConnection::RestApiPost(Json::Value& result,
                                       IOrthancConnection& orthanc,
                                       const std::string& uri,
                                       const std::string& body)
  {
    std::string content;
    orthanc.RestApiPost(content, uri, body);
    ParseJson(result, content);
  }


  void IOrthancConnection::RestApiPut(Json::Value& result,
                                      IOrthancConnection& orthanc,
                                      const std::string& uri,
                                      const std::string& body)
  {
    std::string content;
    orthanc.RestApiPut(content, uri, body);
    ParseJson(result, content);
  }
}
