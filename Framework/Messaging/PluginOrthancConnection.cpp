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
#include "PluginOrthancConnection.h"

#include "../Orthanc/Core/OrthancException.h"

namespace OrthancWSI
{
  class PluginOrthancConnection::MemoryBuffer : public boost::noncopyable
  {
  private:
    OrthancPluginContext*      context_;
    OrthancPluginMemoryBuffer  buffer_;

    void Clear()
    {
      if (buffer_.data != NULL)
      {
        OrthancPluginFreeMemoryBuffer(context_, &buffer_);
        buffer_.data = NULL;
        buffer_.size = 0;
      }
    }


  public:
    MemoryBuffer(OrthancPluginContext* context) : 
      context_(context)
    {
      buffer_.data = NULL;
      buffer_.size = 0;
    }

    ~MemoryBuffer()
    {
      Clear();
    }

    void RestApiGet(const std::string& uri)
    {
      Clear();

      OrthancPluginErrorCode error = OrthancPluginRestApiGet(context_, &buffer_, uri.c_str());

      if (error == OrthancPluginErrorCode_Success)
      {
        // OK, success
      }
      else if (error == OrthancPluginErrorCode_UnknownResource ||
               error == OrthancPluginErrorCode_InexistentItem)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Plugin);
      }
    }

    void RestApiPost(const std::string& uri,
                     const std::string& body)
    {
      Clear();

      OrthancPluginErrorCode error = OrthancPluginRestApiPost(context_, &buffer_, uri.c_str(), body.c_str(), body.size());

      if (error == OrthancPluginErrorCode_Success)
      {
        // OK, success
      }
      else if (error == OrthancPluginErrorCode_UnknownResource ||
               error == OrthancPluginErrorCode_InexistentItem)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Plugin);
      }
    }

    void ToString(std::string& target) const
    {
      if (buffer_.size == 0)
      {
        target.clear();
      }
      else
      {
        target.assign(reinterpret_cast<const char*>(buffer_.data), buffer_.size);
      }
    }
  };



  void PluginOrthancConnection::ApplyGet(std::string& result,
                                         const std::string& uri)
  {
    MemoryBuffer buffer(context_);
    buffer.RestApiGet(uri);
    buffer.ToString(result);
  }

    
  void PluginOrthancConnection::ApplyPost(std::string& result,
                                          const std::string& uri,
                                          const std::string& body)
  {
    MemoryBuffer buffer(context_);
    buffer.RestApiPost(uri, body);
    buffer.ToString(result);
  }
}
