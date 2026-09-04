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
#include "UserLayer.h"

#include <OrthancException.h>
#include <SerializationToolbox.h>
#include <Toolbox.h>

#include <cassert>


static const char* const KEY_COLOR = "color";
static const char* const KEY_ID = "id";
static const char* const KEY_NAME = "name";
static const char* const KEY_PUBLIC = "public";
static const char* const KEY_SHARED_WITH = "shared_with";
static const char* const KEY_VISIBLE = "visible";


namespace OrthancWSI
{
  UserLayer::UserLayer(const BackgroundColor& color,
                       const std::string& name) :
    isVisible_(true),
    color_(color),
    id_(Orthanc::Toolbox::GenerateUuid()),
    name_(name),
    isPublic_(false)
  {
  }


  UserLayer::UserLayer(const Json::Value& serialized)
  {
    if (!serialized.isObject() ||
        !serialized.isMember(KEY_SHARED_WITH) ||
        !serialized[KEY_SHARED_WITH].isArray())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    isVisible_ = Orthanc::SerializationToolbox::ReadBoolean(serialized, KEY_VISIBLE);
    color_ = BackgroundColor::FromHexadecimalString(Orthanc::SerializationToolbox::ReadString(serialized, KEY_COLOR));
    id_ = Orthanc::SerializationToolbox::ReadString(serialized, KEY_ID);
    name_ = Orthanc::SerializationToolbox::ReadString(serialized, KEY_NAME);
    isPublic_ = Orthanc::SerializationToolbox::ReadBoolean(serialized, KEY_PUBLIC);

    const Json::Value& v = serialized[KEY_SHARED_WITH];
    for (Json::Value::ArrayIndex i = 0; i < v.size(); i++)
    {
      sharedWith_.insert(UserId(v[i]));
    }
  }


  bool UserLayer::IsSharedWith(ProjectRole authorRole,
                               const UserId& viewerId,
                               ProjectRole viewerRole) const
  {
    assert(viewerId.GetType() == UserId::Type_Root ||
           viewerId.GetType() == UserId::Type_Standard);

    return (isPublic_ ||
            viewerId.GetType() == UserId::Type_Root ||
            sharedWith_.find(viewerId) != sharedWith_.end());
  }


  void UserLayer::Assign(const UserLayer& other)
  {
    if (other.GetId() != id_)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      isVisible_ = other.isVisible_;
      color_ = other.color_;
      name_ = other.name_;
      sharedWith_ = other.sharedWith_;
      isPublic_ = other.isPublic_;
    }
  }


  void UserLayer::Serialize(Json::Value& serialized) const
  {
    Json::Value sharedWith = Json::arrayValue;
    for (std::set<UserId>::const_iterator it = sharedWith_.begin(); it != sharedWith_.end(); ++it)
    {
      Json::Value item;
      it->Serialize(item);
      sharedWith.append(item);
    }

    serialized = Json::objectValue;
    serialized[KEY_VISIBLE] = isVisible_;
    serialized[KEY_COLOR] = color_.ToHexadecimalString();
    serialized[KEY_ID] = id_;
    serialized[KEY_NAME] = name_;
    serialized[KEY_PUBLIC] = isPublic_;
    serialized[KEY_SHARED_WITH] = sharedWith;
  }
}
