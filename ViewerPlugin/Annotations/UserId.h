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

#include <json/value.h>
#include <string>

class UserId
{
public:
  enum Type
  {
    Type_Root,
    Type_Standard,
    Type_Invalid
  };

private:
  Type         type_;
  std::string  name_;

  void Setup(Type type,
             const std::string& name);

public:
  explicit UserId()
  {
    Setup(Type_Invalid, "");
  }

  explicit UserId(Type type)
  {
    Setup(type, "");
  }

  UserId(Type type,
         const std::string& name)
  {
    Setup(type, name);
  }

  explicit UserId(const Json::Value& serialized);

  Type GetType() const
  {
    return type_;
  }

  const std::string& GetName() const
  {
    return name_;
  }

  bool operator<(const UserId& other) const;

  std::string GetKey() const;

  void Serialize(Json::Value& target) const;
};
