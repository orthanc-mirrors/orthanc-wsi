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
#include "IOrthancConnection.h"

#include "../Orthanc/Core/Logging.h"
#include "../Orthanc/Core/OrthancException.h"

#include <json/reader.h>


namespace OrthancWSI
{
  void IOrthancConnection::RestApiGet(Json::Value& result,
                                      IOrthancConnection& orthanc,
                                      const std::string& uri)
  {
    std::string content;
    orthanc.RestApiGet(content, uri);

    Json::Reader reader;
    if (!reader.parse(content, result))
    {
      LOG(ERROR) << "Cannot parse a JSON file";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }
  }


  void IOrthancConnection::RestApiPost(Json::Value& result,
                                       IOrthancConnection& orthanc,
                                       const std::string& uri,
                                       const std::string& body)
  {
    std::string content;
    orthanc.RestApiPost(content, uri, body);

    Json::Reader reader;
    if (!reader.parse(content, result))
    {
      LOG(ERROR) << "Cannot parse a JSON file";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }
  }
}
