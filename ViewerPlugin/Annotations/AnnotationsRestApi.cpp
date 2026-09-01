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
#include "AnnotationsRestApi.h"

#include "../../Framework/BackgroundColor.h"
#include "../ViewerConfiguration.h"
#include "../ViewerToolbox.h"
#include "IAuthenticatedUser.h"

#include <Cache/SharedObjectCache.h>
#include <Compression/GzipCompressor.h>
#include <Logging.h>
#include <MultiThreading/ReaderWriterLock.h>
#include <SerializationToolbox.h>

#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <boost/regex.hpp>


namespace OrthancWSI
{
  class AnnotationsWorkspaceId
  {
  private:
    std::string            projectId_;
    Orthanc::ResourceType  level_;
    std::string            resourceId_;
    unsigned int           frameNumber_;

    std::string GetKeyPrefix() const
    {
      switch (level_)
      {
      case Orthanc::ResourceType_Series:
        return projectId_ + "|series|" + resourceId_;

      case Orthanc::ResourceType_Instance:
        return projectId_ + "|instance|" + boost::lexical_cast<std::string>(frameNumber_) + "|" + resourceId_;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }
    }

  public:
    AnnotationsWorkspaceId(const std::string& projectId,
                           Orthanc::ResourceType level,
                           const std::string& resourceId,
                           unsigned int frameNumber) :
      projectId_(projectId),
      level_(level),
      resourceId_(resourceId),
      frameNumber_(frameNumber)
    {
      if (level_ != Orthanc::ResourceType_Series &&
          level_ != Orthanc::ResourceType_Instance)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }

      // The pipe symbol is disallowed as it is used to build the key, cf. GetKey()
      if (!Orthanc::Toolbox::IsAsciiString(projectId_) ||
          projectId_.find('|') != std::string::npos)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                        "Project name containing non-ASCII characters or the pipe symbol: " + projectId_);
      }

      if (!Orthanc::Toolbox::IsAsciiString(resourceId_) ||
          resourceId_.find('|') != std::string::npos)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                        "Resource ID containing non-ASCII characters or the pipe symbol: " + resourceId_);
      }
    }

    const std::string& GetProjectId() const
    {
      return projectId_;
    }

    Orthanc::ResourceType GetLevel() const
    {
      return level_;
    }

    const std::string& GetResourceId() const
    {
      return resourceId_;
    }

    std::string GetInfoKey() const
    {
      return GetKeyPrefix() + "|info";
    }

    std::string GetSettingsKey(const UserId& user) const
    {
      return GetKeyPrefix() + "|settings|" + user.GetKey();
    }

    std::string GetFeaturesKey(const UserId& user) const
    {
      return GetKeyPrefix() + "|features|" + user.GetKey();
    }
  };


  static const char* const KEY_AUTHOR = "author";
  static const char* const KEY_COLOR = "color";
  static const char* const KEY_FEATURES = "features";
  static const char* const KEY_ID = "id";
  static const char* const KEY_LAYERS = "layers";
  static const char* const KEY_LAYER_ID = "layer-id";
  static const char* const KEY_NAME = "name";
  static const char* const KEY_PUBLIC = "public";
  static const char* const KEY_SHARED_WITH = "shared_with";
  static const char* const KEY_TYPE = "type";
  static const char* const KEY_VERSION = "version";
  static const char* const KEY_VISIBLE = "visible";

  static const char* const KEY_VALUE_STORE = "wsi";


  class ISerializable : public boost::noncopyable
  {
  public:
    virtual ~ISerializable()
    {
    }

    virtual void Serialize(Json::Value& serialized) const = 0;

    static void Serialize(std::string& serialized,
                          const ISerializable& obj)
    {
      Json::Value value;
      obj.Serialize(value);
      Orthanc::Toolbox::WriteFastJson(serialized, value);
    }
  };


  class ILayer : public ISerializable
  {
  public:
    virtual std::string GetId() const = 0;
  };


  class LayersCollection : public ISerializable
  {
  private:
    typedef std::list<ILayer*>                        Content;
    typedef std::map<std::string, Content::iterator>  Index;

    Content  content_;
    Index    index_;

  public:
    ~LayersCollection()
    {
      for (Content::iterator it = content_.begin(); it != content_.end(); ++it)
      {
        assert(*it != NULL);
        delete *it;
      }
    }

    size_t GetSize() const
    {
      assert(content_.size() == index_.size());
      return content_.size();
    }

    void AddLayer(ILayer* layer /* takes ownership */)
    {
      std::unique_ptr<ILayer> protection(layer);

      if (layer == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
      }

      const std::string id = protection->GetId();

      if (index_.find(id) != index_.end())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls, "Duplicate layer ID");
      }
      else
      {
        content_.push_back(protection.release());

        Content::iterator it = content_.end();
        --it;  // Points to the element we just inserted
        index_[id] = it;
      }
    }

    bool HasLayer(const std::string& id) const
    {
      return (index_.find(id) != index_.end());
    }

    ILayer& GetLayer(const std::string& id) const
    {
      Index::const_iterator found = index_.find(id);

      if (found == index_.end())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
      }
      else
      {
        assert(*(found->second) != NULL);
        return **(found->second);
      }
    }

    void DeleteLayer(const std::string& id)
    {
      Index::iterator found = index_.find(id);

      if (found == index_.end())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
      }
      else
      {
        assert(*(found->second) != NULL);
        delete *(found->second);
        content_.erase(found->second);
        index_.erase(found);
      }
    }

    virtual void Serialize(Json::Value& serialized) const ORTHANC_OVERRIDE
    {
      serialized = Json::arrayValue;

      for (Content::const_iterator it = content_.begin(); it != content_.end(); ++it)
      {
        assert(*it != NULL);

        Json::Value item;
        (*it)->Serialize(item);
        serialized.append(item);
      }
    }

    class Iterator : public boost::noncopyable
    {
    private:
      Content::const_iterator  it_;
      Content::const_iterator  end_;

    public:
      Iterator(const LayersCollection& that) :
        it_(that.content_.begin()),
        end_(that.content_.end())
      {
      }

      bool IsDone() const
      {
        return it_ == end_;
      }

      const ILayer& GetLayer() const
      {
        if (IsDone())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }
        else
        {
          assert(*it_ != NULL);
          return **it_;
        }
      }

      void Next()
      {
        if (IsDone())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }
        else
        {
          it_++;
        }
      }
    };
  };


  static void SetKeyValueStore(const std::string& key,
                               const std::string& value)
  {
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
    OrthancPlugins::KeyValueStore store(KEY_VALUE_STORE);
    store.Store(key, value);
#else
    LOG(WARNING) << "Your Orthanc SDK is too old to save annotations";
#endif
  }


  static void SetKeyValueStore(const std::string& key,
                               const Json::Value& value)
  {
    std::string s;
    Orthanc::Toolbox::WriteFastJson(s, value);
    SetKeyValueStore(key, s);
  }


  static void SetKeyValueStore(const std::string& key,
                               const ISerializable& value)
  {
    std::string s;
    ISerializable::Serialize(s, value);
    SetKeyValueStore(key, s);
  }


  static bool LookupKeyValueStore(std::string& value,
                                  const std::string& key)
  {
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
    OrthancPlugins::KeyValueStore store(KEY_VALUE_STORE);
    return store.GetValue(value, key);
#else
    LOG(WARNING) << "Your Orthanc SDK is too old to load annotations";
    return false;
#endif
  }


  static bool LookupKeyValueStore(Json::Value& value,
                                  const std::string& key)
  {
    std::string s;
    if (LookupKeyValueStore(s, key))
    {
      if (Orthanc::Toolbox::ReadJson(value, s))
      {
        return true;
      }
      else
      {
        LOG(WARNING) << "Discarding incorrect JSON in the key-value store: " << key;
        return false;
      }
    }
    else
    {
      return false;
    }
  }


  static const char* const KEY_ACTIVE_USERS = "active-users";
  static const char* const KEY_PROJECT_NAME = "project-name";
  static const char* const KEY_PROJECT_DESCRIPTION = "project-description";
  static const char* const KEY_USER_LAYERS = "user-layers";
  static const char* const KEY_IMPORTED_LAYERS = "imported-layers";


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
              const std::string& name) :
      isVisible_(true),
      color_(color),
      id_(Orthanc::Toolbox::GenerateUuid()),
      name_(name),
      isPublic_(false)
    {
    }

    UserLayer(const Json::Value& serialized)
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

    bool IsSharedWith(const UserId& user) const
    {
      assert(user.GetType() == UserId::Type_Root ||
             user.GetType() == UserId::Type_Standard);

      return (isPublic_ ||
              user.GetType() == UserId::Type_Root ||
              sharedWith_.find(user) != sharedWith_.end());
    }

    void Assign(const UserLayer& other)
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

    virtual void Serialize(Json::Value& serialized) const ORTHANC_OVERRIDE
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
  };


  class ImportedLayer : public ILayer
  {
  private:
    bool             isVisible_;
    BackgroundColor  color_;
    UserId           author_;
    std::string      id_;
    std::string      name_;

  public:
    ImportedLayer(const UserId& author,
                  const UserLayer& layer) :
      isVisible_(true),
      color_(layer.GetColor()),
      author_(author),
      id_(layer.GetId()),
      name_(layer.GetName())
    {
    }

    ImportedLayer(const Json::Value& serialized)
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

    void Assign(const ImportedLayer& other)
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

    const UserId& GetAuthor() const
    {
      return author_;
    }

    const std::string& GetName() const
    {
      return name_;
    }

    virtual void Serialize(Json::Value& serialized) const ORTHANC_OVERRIDE
    {
      serialized = Json::objectValue;
      serialized[KEY_VISIBLE] = isVisible_;
      serialized[KEY_COLOR] = color_.ToHexadecimalString();
      serialized[KEY_ID] = id_;
      serialized[KEY_NAME] = name_;

      author_.Serialize(serialized[KEY_AUTHOR]);
    }
  };


  class UserAnnotationsSettings : public ISerializable
  {
  private:
    LayersCollection  userLayers_;
    LayersCollection  importedLayers_;

  public:
    UserAnnotationsSettings()
    {
    }

    UserAnnotationsSettings(const Json::Value& serialized)
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

    std::string AddUserLayer(UserLayer* layer)
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

    UserLayer& GetUserLayer(const std::string& layerId) const
    {
      return dynamic_cast<UserLayer&>(userLayers_.GetLayer(layerId));
    }

    ImportedLayer& GetImportedLayer(const std::string& layerId) const
    {
      return dynamic_cast<ImportedLayer&>(importedLayers_.GetLayer(layerId));
    }

    std::string CreateUserLayer()
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

    virtual void Serialize(Json::Value& serialized) const ORTHANC_OVERRIDE
    {
      serialized = Json::objectValue;
      userLayers_.Serialize(serialized[KEY_USER_LAYERS]);
      importedLayers_.Serialize(serialized[KEY_IMPORTED_LAYERS]);
    }

    void ImportLayer(const UserId& author,
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
  };


  class AnnotationsWorkspace : public Orthanc::IDynamicObject
  {
  private:
    class Info : public ISerializable
    {
    private:
      std::string        projectName_;
      std::string        projectDescription_;
      std::set<UserId>   activeUsers_;

    public:
      Info()
      {
      }

      Info(const Json::Value& serialized)
      {
        projectName_ = Orthanc::SerializationToolbox::ReadString(serialized, KEY_PROJECT_NAME);
        projectDescription_ = Orthanc::SerializationToolbox::ReadString(serialized, KEY_PROJECT_DESCRIPTION);

        const Json::Value& users = serialized[KEY_ACTIVE_USERS];

        if (!users.isArray())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
        }

        for (Json::Value::ArrayIndex i = 0; i < users.size(); i++)
        {
          activeUsers_.insert(UserId(users[i]));
        }
      }

      const std::string& GetProjectName() const
      {
        return projectName_;
      }

      void SetProjectName(const std::string& name)
      {
        projectName_ = name;
      }

      const std::string& GetProjectDescription() const
      {
        return projectDescription_;
      }

      void SetProjectDescription(const std::string& description)
      {
        projectDescription_ = description;
      }

      // Return "true" iff. the user was not already tagged as active
      bool AddActiveUser(const UserId& user)
      {
        if (activeUsers_.find(user) == activeUsers_.end())
        {
          activeUsers_.insert(user);
          return true;
        }
        else
        {
          return false;
        }
      }

      const std::set<UserId>& GetActiveUsers() const
      {
        return activeUsers_;
      }

      virtual void Serialize(Json::Value& serialized) const ORTHANC_OVERRIDE
      {
        Json::Value users = Json::arrayValue;
        for (std::set<UserId>::const_iterator it = activeUsers_.begin(); it != activeUsers_.end(); ++it)
        {
          Json::Value user;
          it->Serialize(user);
          users.append(user);
        }

        serialized = Json::objectValue;
        serialized[KEY_PROJECT_NAME] = projectName_;
        serialized[KEY_PROJECT_DESCRIPTION] = projectDescription_;
        serialized[KEY_ACTIVE_USERS] = users;
      }
    };


    void Load(const UserId& user)
    {
      const std::string key = id_.GetSettingsKey(user);

      Json::Value layers;
      if (LookupKeyValueStore(layers, key))
      {
        std::unique_ptr<UserAnnotationsSettings> item(new UserAnnotationsSettings(layers));

        if (content_.find(user) == content_.end())  // Should never be false
        {
          content_[user] = item.release();
        }
      }
    }

    const Info& GetInfo() const
    {
      assert(info_.get() != NULL);
      return *info_;
    }


    typedef std::map<UserId, UserAnnotationsSettings*>   Content;

    Orthanc::ReaderWriterLock  mutex_;
    AnnotationsWorkspaceId     id_;
    std::unique_ptr<Info>      info_;
    Content                    content_;

  public:
    AnnotationsWorkspace(const AnnotationsWorkspaceId& id) :
      id_(id)
    {
      const std::string key = id.GetInfoKey();

      Json::Value info;

      if (LookupKeyValueStore(info, key))
      {
        info_.reset(new Info(info));

        for (std::set<UserId>::const_iterator it = info_->GetActiveUsers().begin();
             it != info_->GetActiveUsers().end(); ++it)
        {
          Load(*it);
        }
      }
      else
      {
        info_.reset(new Info);

        if (OrthancPlugins::RestApiGet(info, "/education/api-plugins/project?id=" + id.GetProjectId(), true))
        {
          // The "orthanc-education" plugin is available
          info_->SetProjectName(Orthanc::SerializationToolbox::ReadString(info, "name"));
          info_->SetProjectDescription(Orthanc::SerializationToolbox::ReadString(info, "description"));
        }

        info_->Serialize(info);
        SetKeyValueStore(key, info);
      }
    }

    ~AnnotationsWorkspace()
    {
      for (Content::iterator it = content_.begin(); it != content_.end(); ++it)
      {
        assert(it->second != NULL);
        delete it->second;
      }
    }


    void SearchActiveUsers(std::set<UserId>& target,
                           const std::string& query)
    {
      Orthanc::ReaderWriterLock::ReadLock lock(mutex_);

      target.clear();

      const boost::regex re(query);
      const std::set<UserId>& activeUsers = info_->GetActiveUsers();

      for (std::set<UserId>::const_iterator it = activeUsers.begin(); it != activeUsers.end(); ++it)
      {
        if (boost::regex_search(it->GetName(), re))
        {
          target.insert(*it);
        }
      }
    }


    void ListUsersSharingLayerWith(std::set<UserId>& target,
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


    class UserReader : public boost::noncopyable
    {
    private:
      Orthanc::ReaderWriterLock::ReadLock lock_;
      AnnotationsWorkspace&               that_;
      UserId                              userId_;
      const UserAnnotationsSettings*      userSettings_;

    public:
      UserReader(AnnotationsWorkspace& that,
                 const UserId& userId) :
        lock_(that.mutex_),
        that_(that),
        userId_(userId)
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

      bool IsValid() const
      {
        return userSettings_ != NULL;
      }

      const std::string& GetProjectName() const
      {
        return that_.GetInfo().GetProjectName();
      }

      const std::string& GetProjectDescription() const
      {
        return that_.GetInfo().GetProjectDescription();
      }

      void ListLayers(Json::Value& serialized) const
      {
        serialized = Json::objectValue;

        if (IsValid())
        {
          userSettings_->Serialize(serialized);
        }
        else
        {
          serialized[KEY_USER_LAYERS] = Json::arrayValue;
          serialized[KEY_IMPORTED_LAYERS] = Json::arrayValue;
        }
      }

      void ListLayersSharedWith(Json::Value& target,
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

      void ListImportedLayers(std::set<UserId>& authors,
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
                const UserLayer& authorLayer = dynamic_cast<const UserLayer&>(found->second->GetUserLayers().GetLayer(layer.GetId()));

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
    };


    class UserWriter : public boost::noncopyable
    {
    private:
      Orthanc::ReaderWriterLock::WriteLock  lock_;
      AnnotationsWorkspace&                 that_;
      UserId                                userId_;
      UserAnnotationsSettings*              userSettings_;

      void Commit()
      {
        SetKeyValueStore(that_.id_.GetSettingsKey(userId_), *userSettings_);
      }

    public:
      UserWriter(AnnotationsWorkspace& that,
                 const UserId& userId) :
        lock_(that.mutex_),
        that_(that),
        userId_(userId)
      {
        if (that.info_->AddActiveUser(userId_))
        {
          // Only update the key-value store if this is the first time we meet this user
          SetKeyValueStore(that.id_.GetInfoKey(), *that.info_);
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

      void CreateUserLayer(Json::Value& answer)
      {
        assert(userSettings_ != NULL);

        const std::string layerId = userSettings_->CreateUserLayer();
        Commit();

        UserLayer& layer = userSettings_->GetUserLayer(layerId);
        layer.Serialize(answer);
      }

      void UpdateUserLayer(const UserLayer& updated)
      {
        assert(userSettings_ != NULL);

        UserLayer& layer = userSettings_->GetUserLayer(updated.GetId());
        layer.Assign(updated);
        Commit();
      }

      void DeleteUserLayer(const std::string& layerId)
      {
        assert(userSettings_ != NULL);
        userSettings_->GetUserLayers().DeleteLayer(layerId);
        Commit();
      }

      void ImportLayer(const UserId& author,
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

        UserLayer& layer = authorData.GetUserLayer(layerId);
        if (!layer.IsSharedWith(userId_))
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess);
        }

        userSettings_->ImportLayer(author, layer);
        Commit();
      }

      void RemoveImportedLayer(const std::string& layerId)
      {
        assert(userSettings_ != NULL);
        userSettings_->GetImportedLayers().DeleteLayer(layerId);
        Commit();
      }

      void UpdateImportedLayer(const ImportedLayer& updated)
      {
        assert(userSettings_ != NULL);

        ImportedLayer& layer = userSettings_->GetImportedLayer(updated.GetId());
        layer.Assign(updated);
        Commit();
      }
    };
  };


  class CachedAnnotationsWorkspace : public boost::noncopyable
  {
  private:
    boost::shared_ptr<Orthanc::IDynamicObject>  cached_;

    static Orthanc::SharedObjectCache& GetCache()
    {
      static boost::mutex  mutex;
      static std::unique_ptr<Orthanc::SharedObjectCache>  cache;

      {
        boost::mutex::scoped_lock lock(mutex);

        if (cache.get() == NULL)
        {
          cache.reset(new Orthanc::SharedObjectCache(ViewerConfiguration::GetInstance().GetAnnotationsCacheSize()));
        }

        return *cache;
      }
    }

  public:
    CachedAnnotationsWorkspace(const AnnotationsWorkspaceId& id)
    {
      const std::string key = id.GetInfoKey();

      cached_ = GetCache().GetCachedValue(key);

      if (cached_.get() == NULL)
      {
        cached_.reset(new AnnotationsWorkspace(id));
        GetCache().Store(key, cached_, 1);
      }
    }

    AnnotationsWorkspace& GetContent() const
    {
      return dynamic_cast<AnnotationsWorkspace&>(*cached_);
    }
  };


  class AnnotationsCommandContext : public boost::noncopyable
  {
  private:
    std::unique_ptr<IAuthenticatedUser>          user_;
    Json::Value                                  body_;
    std::unique_ptr<AnnotationsWorkspaceId>      workspaceId_;
    std::unique_ptr<CachedAnnotationsWorkspace>  workspace_;

  public:
    AnnotationsCommandContext(const OrthancPluginHttpRequest* request)
    {
      if (request->method != OrthancPluginHttpMethod_Post)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }

      user_.reset(IAuthenticatedUser::FromHttpRequest(request));

      if (!Orthanc::Toolbox::ReadJson(body_, request->body, request->bodySize) ||
          !body_.isObject())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
      }

      const std::string projectId = Orthanc::SerializationToolbox::ReadString(body_, "project", "" /* default project */);
      const std::string level = Orthanc::SerializationToolbox::ReadString(body_, "level");
      const std::string resourceId = Orthanc::SerializationToolbox::ReadString(body_, "resource");
      unsigned int frameNumber = Orthanc::SerializationToolbox::ReadUnsignedInteger(body_, "frame", 0 /* default frame */);

      workspaceId_.reset(new AnnotationsWorkspaceId(projectId, Orthanc::StringToResourceType(level.c_str()), resourceId, frameNumber));

      IAuthenticatedUser::ProjectRole role = user_->GetRoleInProject(workspaceId_->GetProjectId());

      if (role != IAuthenticatedUser::ProjectRole_Instructor &&
          role != IAuthenticatedUser::ProjectRole_Learner)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess, "User \"" + user_->Format() +
                                        "\" is not instructor or learner of project \"" + workspaceId_->GetProjectId() + "\"");
      }

      workspace_.reset(new CachedAnnotationsWorkspace(*workspaceId_));
    }

    const IAuthenticatedUser& GetUser() const
    {
      assert(user_.get() != NULL);
      return *user_;
    }

    const AnnotationsWorkspaceId& GetWorkspaceId() const
    {
      assert(workspaceId_.get() != NULL);
      return *workspaceId_;
    }

    std::string GetBodyString(const char* field) const
    {
      return Orthanc::SerializationToolbox::ReadString(body_, field);
    }

    const Json::Value& GetBodyField(const char* field) const
    {
      if (!body_.isMember(field))
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
      }
      else
      {
        return body_[field];
      }
    }

    AnnotationsWorkspace& GetWorkspace()
    {
      assert(workspace_.get() != NULL);
      return workspace_->GetContent();
    }
  };


  static bool ProtectPostRequest(OrthancPluginRestOutput* output,
                                 const OrthancPluginHttpRequest* request)
  {
    if (request->method != OrthancPluginHttpMethod_Post)
    {
      OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
      return false;
    }
    else
    {
      return true;
    }
  }


  void GetWorkspaceInfo(OrthancPluginRestOutput* output,
                        const char* url,
                        const OrthancPluginHttpRequest* request)
  {
    if (ProtectPostRequest(output, request))
    {
      AnnotationsCommandContext context(request);

      Json::Value answer;

      {
        AnnotationsWorkspace::UserReader reader(context.GetWorkspace(), context.GetUser().GetAnnotatingId());
        answer["description"] = reader.GetProjectDescription();
        answer["name"] = reader.GetProjectName();
      }

      answer["enabled"] = ViewerConfiguration::GetInstance().AreAnnotationsEnabled();
      answer["sharing"] = (ViewerConfiguration::GetInstance().AreAnnotationsEnabled() &&
                           ViewerConfiguration::GetInstance().IsAnnotationsSharingEnabled());
      answer["user"] = context.GetUser().Format();

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
      answer["persistent"] = true;
#else
      answer["persistent"] = false;
#endif

      ViewerToolbox::AnswerJson(output, answer);
    }
  }


  void ListUserLayers(OrthancPluginRestOutput* output,
                      const char* url,
                      const OrthancPluginHttpRequest* request)
  {
    if (ProtectPostRequest(output, request))
    {
      AnnotationsCommandContext context(request);

      Json::Value answer;

      {
        AnnotationsWorkspace::UserReader reader(context.GetWorkspace(), context.GetUser().GetAnnotatingId());
        reader.ListLayers(answer);
      }

      ViewerToolbox::AnswerJson(output, answer);
    }
  }


  void CreateUserLayer(OrthancPluginRestOutput* output,
                       const char* url,
                       const OrthancPluginHttpRequest* request)
  {
    if (ProtectPostRequest(output, request))
    {
      AnnotationsCommandContext context(request);

      Json::Value answer;

      {
        AnnotationsWorkspace::UserWriter writer(context.GetWorkspace(), context.GetUser().GetAnnotatingId());
        writer.CreateUserLayer(answer);
      }

      ViewerToolbox::AnswerJson(output, answer);
    }
  }


  void SaveUserLayer(OrthancPluginRestOutput* output,
                     const char* url,
                     const OrthancPluginHttpRequest* request)
  {
    if (ProtectPostRequest(output, request))
    {
      AnnotationsCommandContext context(request);

      UserLayer updated(context.GetBodyField("layer"));

      {
        AnnotationsWorkspace::UserWriter writer(context.GetWorkspace(), context.GetUser().GetAnnotatingId());
        writer.UpdateUserLayer(updated);
      }

      ViewerToolbox::AnswerEmpty(output);
    }
  }


  void DeleteUserLayer(OrthancPluginRestOutput* output,
                       const char* url,
                       const OrthancPluginHttpRequest* request)
  {
    if (ProtectPostRequest(output, request))
    {
      AnnotationsCommandContext context(request);

      const std::string layerId = context.GetBodyString(KEY_LAYER_ID);

      {
        AnnotationsWorkspace::UserWriter writer(context.GetWorkspace(), context.GetUser().GetAnnotatingId());
        writer.DeleteUserLayer(layerId);
      }

      ViewerToolbox::AnswerEmpty(output);
    }
  }


  class UserFeatures : public Orthanc::IDynamicObject
  {
  private:
    Orthanc::ReaderWriterLock  mutex_;
    std::string                key_;
    Json::Value                content_;

    void Load()
    {
      content_ = Json::arrayValue;

      std::string compressed;
      if (LookupKeyValueStore(compressed, key_))
      {
        std::string uncompressed;
        Orthanc::GzipCompressor compressor;
        Orthanc::IBufferCompressor::Uncompress(uncompressed, compressor, compressed);

        Json::Value unserialized;

        if (!Orthanc::Toolbox::ReadJson(unserialized, uncompressed) ||
            !unserialized.isObject() ||
            !unserialized.isMember(KEY_FEATURES) ||
            !unserialized[KEY_FEATURES].isArray())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
        }

        const unsigned int version = Orthanc::SerializationToolbox::ReadUnsignedInteger(unserialized, KEY_VERSION);

        if (version == ORTHANC_WSI_ANNOTATIONS_VERSION)
        {
          content_ = unserialized[KEY_FEATURES];
        }
        else
        {
          switch (version)
          {
            // Implement version conversion here

          default:
            throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "Cannot load annotations from version: " +
                                            boost::lexical_cast<std::string>(version));
          }
        }
      }
    }

    void Save() const
    {
      assert(content_.isArray());

      Json::Value unserialized;
      unserialized[KEY_VERSION] = static_cast<unsigned int>(ORTHANC_WSI_ANNOTATIONS_VERSION);
      unserialized[KEY_FEATURES] = content_;

      std::string serialized;
      Orthanc::Toolbox::WriteFastJson(serialized, unserialized);

      std::string compressed;
      Orthanc::GzipCompressor compressor;
      Orthanc::IBufferCompressor::Compress(compressed, compressor, serialized);

      SetKeyValueStore(key_, compressed);
    }

  public:
    UserFeatures(const std::string& key) :
      key_(key)
    {
      Load();
    }

    void GetContent(Json::Value& target)
    {
      Orthanc::ReaderWriterLock::ReadLock lock(mutex_);

      assert(content_.isArray());
      target = content_;
    }

    void SetContent(const Json::Value& content)
    {
      if (!content.isArray())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      for (Json::Value::ArrayIndex i = 0; i < content.size(); i++)
      {
        if (!content[i].isObject() ||
            !content[i].isMember(KEY_LAYER_ID) ||
            !content[i].isMember(KEY_TYPE) ||
            !content[i][KEY_LAYER_ID].isString() ||
            !content[i][KEY_TYPE].isString())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
        }
      }

      {
        Orthanc::ReaderWriterLock::WriteLock lock(mutex_);
        content_ = content;
        Save();
      }
    }
  };


  class CachedUserFeatures : public boost::noncopyable
  {
  private:
    boost::shared_ptr<Orthanc::IDynamicObject>  cached_;

    static Orthanc::SharedObjectCache& GetCache()
    {
      static boost::mutex  mutex;
      static std::unique_ptr<Orthanc::SharedObjectCache>  cache;

      {
        boost::mutex::scoped_lock lock(mutex);

        if (cache.get() == NULL)
        {
          cache.reset(new Orthanc::SharedObjectCache(ViewerConfiguration::GetInstance().GetFeaturesCacheSize()));
        }

        return *cache;
      }
    }

  public:
    CachedUserFeatures(const AnnotationsWorkspaceId& id,
                       const UserId& user)
    {
      const std::string key = id.GetFeaturesKey(user);

      cached_ = GetCache().GetCachedValue(key);

      if (cached_.get() == NULL)
      {
        cached_.reset(new UserFeatures(key));
        GetCache().Store(key, cached_, 1);
      }
    }

    UserFeatures& GetFeatures() const
    {
      return dynamic_cast<UserFeatures&>(*cached_);
    }
  };


  void LoadUserFeatures(OrthancPluginRestOutput* output,
                        const char* url,
                        const OrthancPluginHttpRequest* request)
  {
    if (request->method != OrthancPluginHttpMethod_Post)
    {
      OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
    }
    else
    {
      AnnotationsCommandContext context(request);

      Json::Value answer;

      {
        CachedUserFeatures cached(context.GetWorkspaceId(), context.GetUser().GetAnnotatingId());
        cached.GetFeatures().GetContent(answer[KEY_FEATURES]);
      }

      ViewerToolbox::AnswerJson(output, answer);
    }
  }


  void SaveUserFeatures(OrthancPluginRestOutput* output,
                        const char* url,
                        const OrthancPluginHttpRequest* request)
  {
    if (request->method != OrthancPluginHttpMethod_Post)
    {
      OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
    }
    else
    {
      AnnotationsCommandContext context(request);

      {
        CachedUserFeatures cached(context.GetWorkspaceId(), context.GetUser().GetAnnotatingId());
        cached.GetFeatures().SetContent(context.GetBodyField(KEY_FEATURES));
      }

      ViewerToolbox::AnswerEmpty(output);
    }
  }


  void SearchActiveUsers(OrthancPluginRestOutput* output,
                         const char* url,
                         const OrthancPluginHttpRequest* request)
  {
    if (request->method != OrthancPluginHttpMethod_Post)
    {
      OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
    }
    else
    {
      AnnotationsCommandContext context(request);
      const UserId self = context.GetUser().GetAnnotatingId();

      const std::string query = context.GetBodyString("query");

      std::set<UserId> users;
      context.GetWorkspace().SearchActiveUsers(users, query);

      Json::Value answer = Json::arrayValue;

      for (std::set<UserId>::const_iterator it = users.begin(); it != users.end(); ++it)
      {
        assert(it->GetType() == UserId::Type_Root ||
               it->GetType() == UserId::Type_Standard);

        if (answer.size() >= 20)
        {
          break;  // Don't load too many users
        }
        else if (!self.Equals(*it)) // Don't add self
        {
          Json::Value item;
          it->Serialize(item);
          answer.append(item);
        }
      }

      ViewerToolbox::AnswerJson(output, answer);
    }
  }


  void ListUsersSharingLayers(OrthancPluginRestOutput* output,
                              const char* url,
                              const OrthancPluginHttpRequest* request)
  {
    if (request->method != OrthancPluginHttpMethod_Post)
    {
      OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
    }
    else
    {
      AnnotationsCommandContext context(request);

      std::set<UserId> users;
      context.GetWorkspace().ListUsersSharingLayerWith(users, context.GetUser().GetAnnotatingId());

      Json::Value answer = Json::arrayValue;

      for (std::set<UserId>::const_iterator it = users.begin(); it != users.end(); ++it)
      {
        Json::Value item;
        it->Serialize(item);
        answer.append(item);
      }

      ViewerToolbox::AnswerJson(output, answer);
    }
  }


  void ListLayersSharedByUser(OrthancPluginRestOutput* output,
                              const char* url,
                              const OrthancPluginHttpRequest* request)
  {
    if (request->method != OrthancPluginHttpMethod_Post)
    {
      OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
    }
    else
    {
      AnnotationsCommandContext context(request);

      const UserId user(context.GetBodyField("user"));
      Json::Value answer;

      {
        AnnotationsWorkspace::UserReader reader(context.GetWorkspace(), user);
        reader.ListLayersSharedWith(answer, context.GetUser().GetAnnotatingId());
      }

      ViewerToolbox::AnswerJson(output, answer);
    }
  }


  void ImportLayer(OrthancPluginRestOutput* output,
                   const char* url,
                   const OrthancPluginHttpRequest* request)
  {
    if (request->method != OrthancPluginHttpMethod_Post)
    {
      OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
    }
    else
    {
      AnnotationsCommandContext context(request);

      const UserId author(context.GetBodyField("author"));
      const std::string layerId = context.GetBodyString("layer");

      {
        AnnotationsWorkspace::UserWriter writer(context.GetWorkspace(), context.GetUser().GetAnnotatingId());
        writer.ImportLayer(author, layerId);
      }

      ViewerToolbox::AnswerEmpty(output);
    }
  }


  void RemoveImportedLayer(OrthancPluginRestOutput* output,
                           const char* url,
                           const OrthancPluginHttpRequest* request)
  {
    if (request->method != OrthancPluginHttpMethod_Post)
    {
      OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
    }
    else
    {
      AnnotationsCommandContext context(request);

      const std::string layerId = context.GetBodyString("layer");

      {
        AnnotationsWorkspace::UserWriter writer(context.GetWorkspace(), context.GetUser().GetAnnotatingId());
        writer.RemoveImportedLayer(layerId);
      }

      ViewerToolbox::AnswerEmpty(output);
    }
  }


  void SaveImportedLayer(OrthancPluginRestOutput* output,
                       const char* url,
                       const OrthancPluginHttpRequest* request)
  {
    if (ProtectPostRequest(output, request))
    {
      AnnotationsCommandContext context(request);

      ImportedLayer updated(context.GetBodyField("layer"));

      {
        AnnotationsWorkspace::UserWriter writer(context.GetWorkspace(), context.GetUser().GetAnnotatingId());
        writer.UpdateImportedLayer(updated);
      }

      ViewerToolbox::AnswerEmpty(output);
    }
  }


  void CreateStandardUser(OrthancPluginRestOutput* output,
                          const char* url,
                          const OrthancPluginHttpRequest* request)
  {
    if (ProtectPostRequest(output, request))
    {
      AnnotationsCommandContext context(request);

      UserId user(UserId::Type_Standard, context.GetBodyString("name"));

      Json::Value answer;
      user.Serialize(answer);

      ViewerToolbox::AnswerJson(output, answer);
    }
  }


  void LoadImportedFeatures(OrthancPluginRestOutput* output,
                            const char* url,
                            const OrthancPluginHttpRequest* request)
  {
    if (ProtectPostRequest(output, request))
    {
      AnnotationsCommandContext context(request);

      std::set<UserId> authors;
      std::set<std::string> layerIds;

      {
        AnnotationsWorkspace::UserReader reader(context.GetWorkspace(), context.GetUser().GetAnnotatingId());
        reader.ListImportedLayers(authors, layerIds);
      }

      Json::Value importedFeatures = Json::arrayValue;

      // Loop over the imported authors
      for (std::set<UserId>::const_iterator it = authors.begin(); it != authors.end(); ++it)
      {
        Json::Value authorFeatures;

        {
          CachedUserFeatures author(context.GetWorkspaceId(), *it);
          author.GetFeatures().GetContent(authorFeatures);
        }

        assert(authorFeatures.isArray());

        for (Json::Value::ArrayIndex i = 0; i < authorFeatures.size(); i++)
        {
          std::string layerId = Orthanc::SerializationToolbox::ReadString(authorFeatures[i], KEY_LAYER_ID);
          if (layerIds.find(layerId) != layerIds.end())
          {
            importedFeatures.append(authorFeatures[i]);
          }
        }
      }

      Json::Value answer;
      answer[KEY_FEATURES] = importedFeatures;
      ViewerToolbox::AnswerJson(output, answer);
    }
  }
}


void RegisterAnnotationsRestApi()
{
  OrthancPlugins::RegisterRestCallback<OrthancWSI::GetWorkspaceInfo>("/wsi/api/workspace-info", true);

  if (OrthancWSI::ViewerConfiguration::GetInstance().AreAnnotationsEnabled())
  {
    OrthancPlugins::RegisterRestCallback<OrthancWSI::CreateUserLayer>("/wsi/api/create-user-layer", true);
    OrthancPlugins::RegisterRestCallback<OrthancWSI::DeleteUserLayer>("/wsi/api/delete-user-layer", true);
    OrthancPlugins::RegisterRestCallback<OrthancWSI::ListUserLayers>("/wsi/api/list-user-layers", true);
    OrthancPlugins::RegisterRestCallback<OrthancWSI::SaveUserLayer>("/wsi/api/save-user-layer", true);

    OrthancPlugins::RegisterRestCallback<OrthancWSI::LoadUserFeatures>("/wsi/api/load-user-features", true);
    OrthancPlugins::RegisterRestCallback<OrthancWSI::SaveUserFeatures>("/wsi/api/save-user-features", true);

    if (OrthancWSI::ViewerConfiguration::GetInstance().IsAnnotationsSharingEnabled())
    {
      OrthancPlugins::RegisterRestCallback<OrthancWSI::CreateStandardUser>("/wsi/api/create-standard-user", true);
      OrthancPlugins::RegisterRestCallback<OrthancWSI::SearchActiveUsers>("/wsi/api/search-active-users", true);

      OrthancPlugins::RegisterRestCallback<OrthancWSI::ImportLayer>("/wsi/api/import-layer", true);
      OrthancPlugins::RegisterRestCallback<OrthancWSI::ListLayersSharedByUser>("/wsi/api/list-shared-layers", true);
      OrthancPlugins::RegisterRestCallback<OrthancWSI::ListUsersSharingLayers>("/wsi/api/list-sharing-users", true);
      OrthancPlugins::RegisterRestCallback<OrthancWSI::RemoveImportedLayer>("/wsi/api/remove-imported-layer", true);
      OrthancPlugins::RegisterRestCallback<OrthancWSI::SaveImportedLayer>("/wsi/api/save-imported-layer", true);

      OrthancPlugins::RegisterRestCallback<OrthancWSI::LoadImportedFeatures>("/wsi/api/load-imported-features", true);
    }
  }
}
