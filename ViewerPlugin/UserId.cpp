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

#include "UserId.h"

#include <OrthancException.h>
#include <SerializationToolbox.h>
#include <Toolbox.h>


static const char* const KEY_TYPE = "type";
static const char* const KEY_NAME = "name";


void UserId::Setup(Type type,
                   const std::string& name)
{
  type_ = type;
  name_ = name;

  switch (type_)
  {
  case Type_Invalid:
  case Type_Administrator:
    if (!name.empty())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    break;

  case Type_Standard:
    if (name.empty())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    break;

  default:
    throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
  }
}


UserId::UserId(const Json::Value& serialized)
{
  Setup(static_cast<Type>(Orthanc::SerializationToolbox::ReadInteger(serialized, KEY_TYPE)),
        Orthanc::SerializationToolbox::ReadString(serialized, KEY_NAME));
}


bool UserId::operator<(const UserId& other) const
{
  if (type_ < other.type_)
  {
    return true;
  }
  else if (type_ > other.type_)
  {
    return false;
  }
  else
  {
    return name_ < other.name_;
  }
}


std::string UserId::GetKey() const
{
  switch (type_)
  {
  case Type_Administrator:
    return "root";

  case Type_Standard:
  {
    // The pipe character "|" is not part of Base64, so we can safely use it to separate components
    std::string s;
    Orthanc::Toolbox::EncodeBase64(s, name_);
    return "user_" + s;
  }

  case Type_Invalid:
    throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);

  default:
    throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
  }
}


void UserId::Serialize(Json::Value& target) const
{
  if (type_ == Type_Invalid)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
  }
  else
  {
    target = Json::objectValue;
    target[KEY_TYPE] = static_cast<int>(type_);
    target[KEY_NAME] = name_;
  }
}
