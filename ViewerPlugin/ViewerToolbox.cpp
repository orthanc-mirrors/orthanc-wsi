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


#include "../Framework/PrecompiledHeadersWSI.h"
#include "ViewerToolbox.h"

#include <Logging.h>
#include <Toolbox.h>

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"


static const char* const KEY_VALUE_STORE = "wsi";


namespace OrthancWSI
{
  namespace ViewerToolbox
  {
    void AnswerJson(OrthancPluginRestOutput* output,
                    const Json::Value& value)
    {
      std::string s;
      Orthanc::Toolbox::WriteFastJson(s, value);
      OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, s.c_str(), s.size(), "application/json");
    }


    void AnswerEmpty(OrthancPluginRestOutput* output)
    {
      Json::Value answer = Json::objectValue;
      AnswerJson(output, answer);
    }


    void SetKeyValueStore(const std::string& key,
                          const std::string& value)
    {
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
      OrthancPlugins::KeyValueStore store(KEY_VALUE_STORE);
      store.Store(key, value);
#else
      LOG(WARNING) << "Your Orthanc SDK is too old to save annotations";
#endif
    }


    void SetKeyValueStore(const std::string& key,
                          const Json::Value& value)
    {
      std::string s;
      Orthanc::Toolbox::WriteFastJson(s, value);
      SetKeyValueStore(key, s);
    }


    bool LookupKeyValueStore(std::string& value,
                             const std::string& key)
    {
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
      OrthancPlugins::KeyValueStore store(KEY_VALUE_STORE);
      return store.GetValue(value, key);
#else
      LOG(WARNING) << "Your Orthanc SDK is too old to load annotations";
      return false;
#endif
    }


    bool LookupKeyValueStore(Json::Value& value,
                             const std::string& key)
    {
      std::string s;
      if (LookupKeyValueStore(s, key))
      {
        if (Orthanc::Toolbox::ReadJson(value, s))
        {
          return true;
        }
        else
        {
          LOG(WARNING) << "Discarding incorrect JSON in the key-value store: " << key;
          return false;
        }
      }
      else
      {
        return false;
      }
    }
  }
}
