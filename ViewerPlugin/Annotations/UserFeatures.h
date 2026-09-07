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

#include <IDynamicObject.h>
#include <MultiThreading/ReaderWriterLock.h>

#include <json/value.h>
#include <string>


namespace OrthancWSI
{
  class UserFeatures : public Orthanc::IDynamicObject
  {
  private:
    Orthanc::ReaderWriterLock  mutex_;
    std::string                key_;
    Json::Value                content_;

    void Load();

    void Save() const;

  public:
    explicit UserFeatures(const std::string& key);

    void GetContent(Json::Value& target);

    void SetContent(const Json::Value& content);
  };
}
