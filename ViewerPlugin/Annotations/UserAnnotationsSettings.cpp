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
#include "UserAnnotationsSettings.h"

#include <Logging.h>
#include <OrthancException.h>

#include <boost/lexical_cast.hpp>


static const char* const KEY_USER_LAYERS = "user-layers";
static const char* const KEY_IMPORTED_LAYERS = "imported-layers";


namespace OrthancWSI
{
  UserAnnotationsSettings::UserAnnotationsSettings(const Json::Value& serialized)
  {
    if (!serialized.isObject() ||
        !serialized.isMember(KEY_USER_LAYERS) ||
        !serialized.isMember(KEY_IMPORTED_LAYERS) ||
        !serialized[KEY_USER_LAYERS].isArray() ||
        !serialized[KEY_IMPORTED_LAYERS].isArray())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }
    else
    {
      const Json::Value& a = serialized[KEY_USER_LAYERS];
      for (Json::Value::ArrayIndex i = 0; i < a.size(); i++)
      {
        userLayers_.AddLayer(new UserLayer(a[i]));
      }

      const Json::Value& b = serialized[KEY_IMPORTED_LAYERS];
      for (Json::Value::ArrayIndex i = 0; i < b.size(); i++)
      {
        importedLayers_.AddLayer(new ImportedLayer(b[i]));
      }
    }
  }


  std::string UserAnnotationsSettings::AddUserLayer(UserLayer* layer)
  {
    std::unique_ptr<UserLayer> protection(layer);

    if (layer == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }

    const std::string id = protection->GetId();

    userLayers_.AddLayer(protection.release());

    return id;
  }


  UserLayer& UserAnnotationsSettings::GetUserLayer(const std::string& layerId) const
  {
    return dynamic_cast<UserLayer&>(userLayers_.GetLayer(layerId));
  }


  ImportedLayer& UserAnnotationsSettings::GetImportedLayer(const std::string& layerId) const
  {
    return dynamic_cast<ImportedLayer&>(importedLayers_.GetLayer(layerId));
  }


  std::string UserAnnotationsSettings::CreateUserLayer()
  {
    static const uint8_t PALETTE[] = {
      0xe6, 0x39, 0x46,  // red: #e63946
      0x2a, 0x9d, 0x8f,
      0xe9, 0xc4, 0x6a,
      0x26, 0x46, 0x53,
      0xf4, 0xa2, 0x61
    };

    static const size_t PALETTE_SIZE = sizeof(PALETTE) / (3 * sizeof(uint8_t));

    size_t item = userLayers_.GetSize() % PALETTE_SIZE;

    BackgroundColor color(PALETTE[3 * item],
                          PALETTE[3 * item + 1],
                          PALETTE[3 * item + 2]);

    std::string name;
    if (userLayers_.GetSize() == 0)
    {
      name = "Default";
    }
    else
    {
      name = "Layer " + boost::lexical_cast<std::string>(userLayers_.GetSize() + 1);
    }

    return AddUserLayer(new UserLayer(color, name));
  }


  void UserAnnotationsSettings::Serialize(Json::Value& serialized) const
  {
    serialized = Json::objectValue;
    userLayers_.Serialize(serialized[KEY_USER_LAYERS]);
    importedLayers_.Serialize(serialized[KEY_IMPORTED_LAYERS]);
  }


  void UserAnnotationsSettings::ImportLayer(const UserId& author,
                                            const UserLayer& layer)
  {
    if (importedLayers_.HasLayer(layer.GetId()))
    {
      LOG(INFO) << "Cannot re-import already imported layer: " << layer.GetId();
    }
    else
    {
      importedLayers_.AddLayer(new ImportedLayer(author, layer));
    }
  }
}
