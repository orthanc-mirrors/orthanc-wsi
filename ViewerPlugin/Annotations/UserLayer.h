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

#include "../../Framework/BackgroundColor.h"
#include "../ViewerConfiguration.h"
#include "ILayer.h"
#include "UserId.h"

#include <Compatibility.h>

#include <set>


namespace OrthancWSI
{
  class UserLayer : public ILayer
  {
  private:
    bool              isVisible_;
    BackgroundColor   color_;
    std::string       id_;
    std::string       name_;
    std::set<UserId>  sharedWith_;
    bool              isPublic_;

  public:
    UserLayer(const BackgroundColor& color,
              const std::string& name);

    explicit UserLayer(const Json::Value& serialized);

    virtual std::string GetId() const ORTHANC_OVERRIDE
    {
      return id_;
    }

    bool IsVisible() const
    {
      return isVisible_;
    }

    const BackgroundColor& GetColor() const
    {
      return color_;
    }

    const std::string& GetName() const
    {
      return name_;
    }

    bool IsSharedWith(ProjectRole authorRole,
                      const UserId& viewerId,
                      ProjectRole viewerRole) const;

    void Assign(const UserLayer& other);

    virtual void Serialize(Json::Value& serialized) const ORTHANC_OVERRIDE;
  };
}
