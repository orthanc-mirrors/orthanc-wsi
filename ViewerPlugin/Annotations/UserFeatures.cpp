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
#include "UserFeatures.h"

#include "../ViewerToolbox.h"

#include <Compression/GzipCompressor.h>
#include <OrthancException.h>
#include <SerializationToolbox.h>
#include <Toolbox.h>

#include <boost/lexical_cast.hpp>


static const char* const KEY_FEATURES = "features";
static const char* const KEY_LAYER_ID = "layer-id";
static const char* const KEY_TYPE = "type";
static const char* const KEY_VERSION = "version";


namespace OrthancWSI
{
  void UserFeatures::Load()
  {
    content_ = Json::arrayValue;

    std::string compressed;
    if (ViewerToolbox::LookupKeyValueStore(compressed, key_))
    {
      std::string uncompressed;
      Orthanc::GzipCompressor compressor;
      Orthanc::IBufferCompressor::Uncompress(uncompressed, compressor, compressed);

      Json::Value unserialized;

      if (!Orthanc::Toolbox::ReadJson(unserialized, uncompressed) ||
          !unserialized.isObject() ||
          !unserialized.isMember(KEY_FEATURES) ||
          !unserialized[KEY_FEATURES].isArray())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }

      const unsigned int version = Orthanc::SerializationToolbox::ReadUnsignedInteger(unserialized, KEY_VERSION);

      if (version == ORTHANC_WSI_ANNOTATIONS_VERSION)
      {
        content_ = unserialized[KEY_FEATURES];
      }
      else
      {
        switch (version)
        {
          // Implement version conversion here

          default:
            throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "Cannot load annotations from version: " +
                                            boost::lexical_cast<std::string>(version));
        }
      }
    }
  }


  void UserFeatures::Save() const
  {
    assert(content_.isArray());

    Json::Value unserialized;
    unserialized[KEY_VERSION] = static_cast<unsigned int>(ORTHANC_WSI_ANNOTATIONS_VERSION);
    unserialized[KEY_FEATURES] = content_;

    std::string serialized;
    Orthanc::Toolbox::WriteFastJson(serialized, unserialized);

    std::string compressed;
    Orthanc::GzipCompressor compressor;
    Orthanc::IBufferCompressor::Compress(compressed, compressor, serialized);

    ViewerToolbox::SetKeyValueStore(key_, compressed);
  }


  UserFeatures::UserFeatures(const std::string& key) :
    key_(key)
  {
    Load();
  }


  void UserFeatures::GetContent(Json::Value& target)
  {
    Orthanc::ReaderWriterLock::ReadLock lock(mutex_);

    assert(content_.isArray());
    target = content_;
  }


  void UserFeatures::SetContent(const Json::Value& content)
  {
    if (!content.isArray())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    for (Json::Value::ArrayIndex i = 0; i < content.size(); i++)
    {
      if (!content[i].isObject() ||
          !content[i].isMember(KEY_LAYER_ID) ||
          !content[i].isMember(KEY_TYPE) ||
          !content[i][KEY_LAYER_ID].isString() ||
          !content[i][KEY_TYPE].isString())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }
    }

    {
      Orthanc::ReaderWriterLock::WriteLock lock(mutex_);
      content_ = content;
      Save();
    }
  }
}
