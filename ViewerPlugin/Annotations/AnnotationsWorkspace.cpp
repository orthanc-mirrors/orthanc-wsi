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
#include "AnnotationsWorkspace.h"

#include "../ViewerToolbox.h"

#include <OrthancException.h>

#include <boost/regex.hpp>


static const char* const KEY_ACTIVE_INSTRUCTORS = "active-instructors";
static const char* const KEY_ACTIVE_LEARNERS = "active-learners";


namespace OrthancWSI
{
  class AnnotationsWorkspace::PersistentInfo : public ISerializable
  {
  private:
    // An active user is always of type "standard"
    std::set<UserId>   activeInstructors_;
    std::set<UserId>   activeLearners_;


    static void ParseActiveUsers(std::set<UserId>& target,
                                 const Json::Value& serialized)
    {
      if (!serialized.isArray())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      target.clear();

      for (Json::Value::ArrayIndex i = 0; i < serialized.size(); i++)
      {
        target.insert(UserId(serialized[i]));
      }
    }


    static void SerializeActiveUsers(Json::Value& serialized,
                                     const std::set<UserId>& source)
    {
      serialized = Json::arrayValue;

      for (std::set<UserId>::const_iterator it = source.begin(); it != source.end(); ++it)
      {
        Json::Value user;
        it->Serialize(user);
        serialized.append(user);
      }
    }

  public:
    PersistentInfo()
    {
    }


    explicit PersistentInfo(const Json::Value& serialized)
    {
      if (!serialized.isObject() ||
          !serialized.isMember(KEY_ACTIVE_INSTRUCTORS) ||
          !serialized.isMember(KEY_ACTIVE_LEARNERS))
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      ParseActiveUsers(activeInstructors_, serialized[KEY_ACTIVE_INSTRUCTORS]);
      ParseActiveUsers(activeLearners_, serialized[KEY_ACTIVE_LEARNERS]);
    }


    // Return "true" iff. the user was not already tagged as active or
    // if the user has changed their role in the project (from learner
    // to instructor, or from instructor to learner)
    bool AddActiveUser(const UserId& user,
                       ProjectRole role)
    {
      switch (role)
      {
        case ProjectRole_Instructor:
          if (activeInstructors_.find(user) == activeInstructors_.end())
          {
            activeLearners_.erase(user);  // Accomodate with change in the role
            activeInstructors_.insert(user);
            return true;
          }
          else
          {
            return false;
          }

        case ProjectRole_Learner:
          if (activeLearners_.find(user) == activeLearners_.end())
          {
            activeInstructors_.erase(user);  // Accomodate with change in the role
            activeLearners_.insert(user);
            return true;
          }
          else
          {
            return false;
          }

          break;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
    }


    virtual void Serialize(Json::Value& serialized) const ORTHANC_OVERRIDE
    {
      serialized = Json::objectValue;
      SerializeActiveUsers(serialized[KEY_ACTIVE_INSTRUCTORS], activeInstructors_);
      SerializeActiveUsers(serialized[KEY_ACTIVE_LEARNERS], activeLearners_);
    }


    class ActiveUsersIterator : public boost::noncopyable
    {
    private:
      std::set<UserId>::const_iterator  instructorsIterator_;
      std::set<UserId>::const_iterator  instructorsEnd_;
      std::set<UserId>::const_iterator  learnersIterator_;
      std::set<UserId>::const_iterator  learnersEnd_;

    public:
      explicit ActiveUsersIterator(const PersistentInfo& that) :
        instructorsIterator_(that.activeInstructors_.begin()),
        instructorsEnd_(that.activeInstructors_.end()),
        learnersIterator_(that.activeLearners_.begin()),
        learnersEnd_(that.activeLearners_.end())
      {
      }

      bool IsDone() const
      {
        return (instructorsIterator_ == instructorsEnd_ &&
                learnersIterator_ == learnersEnd_);
      }

      const UserId& GetUser() const
      {
        if (instructorsIterator_ != instructorsEnd_)
        {
          return *instructorsIterator_;
        }
        else if (learnersIterator_ != learnersEnd_)
        {
          return *learnersIterator_;
        }
        else
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }
      }

      ProjectRole GetRole() const
      {
        if (instructorsIterator_ != instructorsEnd_)
        {
          return ProjectRole_Instructor;
        }
        else if (learnersIterator_ != learnersEnd_)
        {
          return ProjectRole_Learner;
        }
        else
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }
      }

      void Next()
      {
        if (instructorsIterator_ != instructorsEnd_)
        {
          ++instructorsIterator_;
        }
        else if (learnersIterator_ != learnersEnd_)
        {
          ++learnersIterator_;
        }
        else
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }
      }
    };
  };


  void AnnotationsWorkspace::Load(const UserId& user)
  {
    const std::string key = id_.GetSettingsKey(user);

    Json::Value layers;
    if (ViewerToolbox::LookupKeyValueStore(layers, key))
    {
      std::unique_ptr<UserAnnotationsSettings> item(new UserAnnotationsSettings(layers));

      if (content_.find(user) == content_.end())  // Should never be false
      {
        content_[user] = item.release();
      }
    }
  }


  AnnotationsWorkspace::AnnotationsWorkspace(const AnnotationsWorkspaceId& id) :
    id_(id),
    projectInformation_(id.GetProjectId())
  {
    const std::string key = id.GetInfoKey();

    Json::Value info;

    if (ViewerToolbox::LookupKeyValueStore(info, key))
    {
      persistentInfo_.reset(new PersistentInfo(info));

      PersistentInfo::ActiveUsersIterator it(*persistentInfo_);
      while (!it.IsDone())
      {
        Load(it.GetUser());
      }
    }
    else
    {
      persistentInfo_.reset(new PersistentInfo);
      persistentInfo_->Serialize(info);
      ViewerToolbox::SetKeyValueStore(key, info);
    }
  }


  AnnotationsWorkspace::~AnnotationsWorkspace()
  {
    for (Content::iterator it = content_.begin(); it != content_.end(); ++it)
    {
      assert(it->second != NULL);
      delete it->second;
    }
  }


  void AnnotationsWorkspace::SearchActiveUsers(std::set<UserId>& target,
                                               const std::string& query)
  {
    Orthanc::ReaderWriterLock::ReadLock lock(mutex_);

    target.clear();

    const boost::regex re(query);

    PersistentInfo::ActiveUsersIterator it(*persistentInfo_);
    while (!it.IsDone())
    {
      if (boost::regex_search(it.GetUser().GetName(), re))
      {
        target.insert(it.GetUser());
      }
    }
  }


  void AnnotationsWorkspace::ListUsersSharingLayerWith(std::set<UserId>& target,
                                                       const UserId& user)
  {
    Orthanc::ReaderWriterLock::ReadLock lock(mutex_);

    target.clear();

    // Loop over all the users in this workspace
    for (Content::const_iterator it = content_.begin(); it != content_.end(); ++it)
    {
      assert(it->second != NULL);
      if (!user.Equals(it->first))  // Don't add self
      {
        LayersCollection::Iterator iterator(it->second->GetUserLayers());

        // Loop over all the user layers in this workspace
        while (!iterator.IsDone())
        {
          const UserLayer& layer = dynamic_cast<const UserLayer&>(iterator.GetLayer());

          if (layer.IsSharedWith(user))
          {
            target.insert(it->first);
            break;
          }
          else
          {
            iterator.Next();
          }
        }
      }
    }
  }


  AnnotationsWorkspace::UserReader::UserReader(AnnotationsWorkspace& that,
                                               const UserId& userId,
                                               ProjectRole userRole) :
    lock_(that.mutex_),
    that_(that),
    userId_(userId),
    userRole_(userRole)
  {
    Content::const_iterator found = that.content_.find(userId);

    if (found == that.content_.end())
    {
      userSettings_ = NULL;
    }
    else
    {
      assert(found->second != NULL);
      userSettings_ = found->second;
    }
  }


  void AnnotationsWorkspace::UserReader::ListLayers(Json::Value& serialized) const
  {
    serialized = Json::objectValue;

    if (IsValid())
    {
      userSettings_->Serialize(serialized);
    }
    else
    {
      UserAnnotationsSettings empty;
      empty.Serialize(serialized);
    }
  }


  void AnnotationsWorkspace::UserReader::ListLayersSharedWith(Json::Value& target,
                                                              const UserId& user) const
  {
    target = Json::arrayValue;

    if (IsValid())
    {
      LayersCollection::Iterator iterator(userSettings_->GetUserLayers());

      while (!iterator.IsDone())
      {
        const UserLayer& layer = dynamic_cast<const UserLayer&>(iterator.GetLayer());

        if (layer.IsSharedWith(user))
        {
          Json::Value item;
          layer.Serialize(item);
          target.append(item);
        }

        iterator.Next();
      }
    }
  }


  void AnnotationsWorkspace::UserReader::ListImportedLayers(std::set<UserId>& authors,
                                                            std::set<std::string>& layerIds) const
  {
    authors.clear();
    layerIds.clear();

    if (IsValid())
    {
      LayersCollection::Iterator iterator(userSettings_->GetImportedLayers());

      while (!iterator.IsDone())
      {
        const ImportedLayer& layer = dynamic_cast<const ImportedLayer&>(iterator.GetLayer());

        Content::const_iterator found = that_.content_.find(layer.GetAuthor());

        if (found != that_.content_.end())
        {
          assert(found->second != NULL);

          // Check that the layer has not been deleted in the meantime by its author,
          // and that the layer is still shared with this user
          if (found->second->GetUserLayers().HasLayer(layer.GetId()))
          {
            const UserLayer& authorLayer = found->second->GetUserLayer(layer.GetId());

            if (authorLayer.IsSharedWith(userId_))
            {
              authors.insert(layer.GetAuthor());
              layerIds.insert(layer.GetId());
            }
          }
        }

        iterator.Next();
      }
    }
  }


  void AnnotationsWorkspace::UserWriter::Commit()
  {
    ISerializable::SetKeyValueStore(that_.id_.GetSettingsKey(userId_), *userSettings_);
  }


  AnnotationsWorkspace::UserWriter::UserWriter(AnnotationsWorkspace& that,
                                               const UserId& userId,
                                               ProjectRole userRole) :
    lock_(that.mutex_),
    that_(that),
    userId_(userId),
    userRole_(userRole)
  {
    if (that.persistentInfo_->AddActiveUser(userId_, userRole))
    {
      // Only update the key-value store if this is the first time we
      // meet this user or if their role has changed in the project
      ISerializable::SetKeyValueStore(that.id_.GetInfoKey(), *that.persistentInfo_);
    }

    Content::iterator found = that.content_.find(userId_);

    if (found == that.content_.end())
    {
      std::unique_ptr<UserAnnotationsSettings> layers(new UserAnnotationsSettings);
      userSettings_ = layers.get();
      that.content_[userId_] = layers.release();
      Commit();
    }
    else
    {
      assert(found->second != NULL);
      userSettings_ = found->second;
    }
  }


  void AnnotationsWorkspace::UserWriter::CreateUserLayer(Json::Value& answer)
  {
    assert(userSettings_ != NULL);

    const std::string layerId = userSettings_->CreateUserLayer();
    Commit();

    const UserLayer& layer = userSettings_->GetUserLayer(layerId);
    layer.Serialize(answer);
  }


  void AnnotationsWorkspace::UserWriter::UpdateUserLayer(const UserLayer& updated)
  {
    assert(userSettings_ != NULL);

    UserLayer& layer = userSettings_->GetUserLayer(updated.GetId());
    layer.Assign(updated);
    Commit();
  }


  void AnnotationsWorkspace::UserWriter::DeleteUserLayer(const std::string& layerId)
  {
    assert(userSettings_ != NULL);
    userSettings_->GetUserLayers().DeleteLayer(layerId);
    Commit();
  }


  void AnnotationsWorkspace::UserWriter::ImportLayer(const UserId& author,
                                                     const std::string& layerId)
  {
    assert(userSettings_ != NULL);

    Content::const_iterator found = that_.content_.find(author);
    if (found == that_.content_.end())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }

    assert(found->second != NULL);
    const UserAnnotationsSettings& authorData = *found->second;

    const UserLayer& layer = authorData.GetUserLayer(layerId);
    if (!layer.IsSharedWith(userId_))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess);
    }

    userSettings_->ImportLayer(author, layer);
    Commit();
  }


  void AnnotationsWorkspace::UserWriter::RemoveImportedLayer(const std::string& layerId)
  {
    assert(userSettings_ != NULL);
    userSettings_->GetImportedLayers().DeleteLayer(layerId);
    Commit();
  }


  void AnnotationsWorkspace::UserWriter::UpdateImportedLayer(const ImportedLayer& updated)
  {
    assert(userSettings_ != NULL);

    ImportedLayer& layer = userSettings_->GetImportedLayer(updated.GetId());
    layer.Assign(updated);
    Commit();
  }
}
