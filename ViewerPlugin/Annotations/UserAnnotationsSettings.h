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


#pragma once

#include "LayersCollection.h"
#include "UserLayer.h"
#include "ImportedLayer.h"


namespace OrthancWSI
{
  class UserAnnotationsSettings : public ISerializable
  {
  private:
    LayersCollection  userLayers_;
    LayersCollection  importedLayers_;

  public:
    UserAnnotationsSettings()
    {
    }

    explicit UserAnnotationsSettings(const Json::Value& serialized);

    std::string AddUserLayer(UserLayer* layer);

    UserLayer& GetUserLayer(const std::string& layerId) const;

    ImportedLayer& GetImportedLayer(const std::string& layerId) const;

    LayersCollection& GetUserLayers()
    {
      return userLayers_;
    }

    const LayersCollection& GetUserLayers() const
    {
      return userLayers_;
    }

    LayersCollection& GetImportedLayers()
    {
      return importedLayers_;
    }

    const LayersCollection& GetImportedLayers() const
    {
      return importedLayers_;
    }

    std::string CreateUserLayer();

    virtual void Serialize(Json::Value& serialized) const ORTHANC_OVERRIDE;

    void ImportLayer(const UserId& author,
                     const UserLayer& layer);
  };
}
