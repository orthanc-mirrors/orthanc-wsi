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

#include "AnnotationsWorkspaceId.h"
#include "ProjectInformation.h"
#include "UserAnnotationsSettings.h"

#include <IDynamicObject.h>
#include <MultiThreading/ReaderWriterLock.h>


namespace OrthancWSI
{
  class AnnotationsWorkspace : public Orthanc::IDynamicObject
  {
  private:
    class PersistentInfo;

    void Load(const UserId& user);

    typedef std::map<UserId, UserAnnotationsSettings*>   Content;

    Orthanc::ReaderWriterLock        mutex_;
    AnnotationsWorkspaceId           id_;
    std::unique_ptr<PersistentInfo>  persistentInfo_;
    Content                          content_;
    ProjectInformation               projectInformation_;

  public:
    explicit AnnotationsWorkspace(const AnnotationsWorkspaceId& id);

    ~AnnotationsWorkspace();

    const AnnotationsWorkspaceId& GetId() const
    {
      return id_;
    }

    std::string GetProjectName()
    {
      return projectInformation_.GetName();
    }

    std::string GetProjectDescription()
    {
      return projectInformation_.GetDescription();
    }

    void SearchActiveUsers(std::set<UserId>& target,
                           const std::string& query);

    void ListUsersSharingLayerWith(std::set<UserId>& target,
                                   const UserId& user);


    class UserReader : public boost::noncopyable
    {
    private:
      Orthanc::ReaderWriterLock::ReadLock lock_;
      AnnotationsWorkspace&               that_;
      UserId                              userId_;
      const UserAnnotationsSettings*      userSettings_;

    public:
      UserReader(AnnotationsWorkspace& that,
                 const UserId& userId);

      bool IsValid() const
      {
        return userSettings_ != NULL;
      }

      void ListLayers(Json::Value& serialized) const;

      void ListLayersSharedWith(Json::Value& target,
                                const UserId& user) const;

      void ListImportedLayers(std::set<UserId>& authors,
                              std::set<std::string>& layerIds) const;
    };


    class UserWriter : public boost::noncopyable
    {
    private:
      Orthanc::ReaderWriterLock::WriteLock  lock_;
      AnnotationsWorkspace&                 that_;
      UserId                                userId_;
      UserAnnotationsSettings*              userSettings_;

      void Commit();

    public:
      UserWriter(AnnotationsWorkspace& that,
                 const UserId& userId);

      void CreateUserLayer(Json::Value& answer);

      void UpdateUserLayer(const UserLayer& updated);

      void DeleteUserLayer(const std::string& layerId);

      void ImportLayer(const UserId& author,
                       const std::string& layerId);

      void RemoveImportedLayer(const std::string& layerId);

      void UpdateImportedLayer(const ImportedLayer& updated);
    };
  };
}
