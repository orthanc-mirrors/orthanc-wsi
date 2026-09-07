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
#include "ImportedLayer.h"

#include "UserLayer.h"

#include <OrthancException.h>
#include <SerializationToolbox.h>


static const char* const KEY_VISIBLE = "visible";
static const char* const KEY_AUTHOR = "author";
static const char* const KEY_COLOR = "color";
static const char* const KEY_ID = "id";
static const char* const KEY_NAME = "name";


namespace OrthancWSI
{
  ImportedLayer::ImportedLayer(const UserId& author,
                               const UserLayer& layer) :
    isVisible_(true),
    color_(layer.GetColor()),
    author_(author),
    id_(layer.GetId()),
    name_(layer.GetName())
  {
  }


  ImportedLayer::ImportedLayer(const Json::Value& serialized)
  {
    if (!serialized.isObject() ||
        !serialized.isMember(KEY_AUTHOR))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    isVisible_ = Orthanc::SerializationToolbox::ReadBoolean(serialized, KEY_VISIBLE);
    author_ = UserId(serialized[KEY_AUTHOR]);
    id_ = Orthanc::SerializationToolbox::ReadString(serialized, KEY_ID);
    name_ = Orthanc::SerializationToolbox::ReadString(serialized, KEY_NAME);
    color_ = BackgroundColor::FromHexadecimalString(Orthanc::SerializationToolbox::ReadString(serialized, KEY_COLOR));
  }


  void ImportedLayer::Assign(const ImportedLayer& other)
  {
    if (other.GetId() != id_)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      isVisible_ = other.isVisible_;
      color_ = other.color_;
      author_ = other.author_;
      name_ = other.name_;
    }
  }


  void ImportedLayer::Serialize(Json::Value& serialized) const
  {
    serialized = Json::objectValue;
    serialized[KEY_VISIBLE] = isVisible_;
    serialized[KEY_COLOR] = color_.ToHexadecimalString();
    serialized[KEY_ID] = id_;
    serialized[KEY_NAME] = name_;

    author_.Serialize(serialized[KEY_AUTHOR]);
  }
}
