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


namespace OrthancWSI
{
  class AnnotationsId
  {
  private:
    std::string            projectId_;
    Orthanc::ResourceType  level_;
    std::string            resourceId_;
    unsigned int           frameNumber_;

  public:
    AnnotationsId(const std::string& projectId,
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

    std::string GetKey() const
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
  };


  static const char* const KEY_AUTHOR = "author";
  static const char* const KEY_COLOR = "color";
  static const char* const KEY_FEATURES = "features";
  static const char* const KEY_ID = "id";
  static const char* const KEY_LAYERS = "layers";
  static const char* const KEY_NAME = "name";
  static const char* const KEY_PUBLIC = "public";
  static const char* const KEY_SHARED_WITH = "shared_with";
  static const char* const KEY_VERSION = "version";
  static const char* const KEY_VISIBLE = "visible";

  static const char* const KEY_VALUE_STORE = "wsi";


/**

   Content of a layer in the DB:

   - visible             (editable)
   - color               (editable)
   - author type and ID  (read-only)
   - layer ID            (read-only)
   - name of the layer   (editable)
   - shared_with         (editable, can be disabled for learners by the instructors)
   - public              (editable, can be disabled for learners by the instructors)

   Content of an imported shared layer in the DB:

   - visible             (editable, set to "true" on import)
   - color               (editable, can be different from original layer)
   - author type and ID  (read-only)
   - layer ID            (read-only)
   - name of the layer   (read-only)

   Finding shared layers:

   map<UserId, std::list<UserLayer>>

   map<UserId, std::list<std::pair<UserId, const UserLayer*>>>

   API must return: color, author, layer ID, and name (visible is set to true once loaded)

**/


  class ISerializable : public boost::noncopyable
  {
  public:
    virtual ~ISerializable()
    {
    }

    virtual void Serialize(Json::Value& target) const = 0;

    static void Serialize(std::string& target,
                          const ISerializable& obj)
    {
      Json::Value value;
      obj.Serialize(value);
      Orthanc::Toolbox::WriteFastJson(target, value);
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

    virtual void Serialize(Json::Value& target) const ORTHANC_OVERRIDE
    {
      target = Json::arrayValue;

      for (Content::const_iterator it = content_.begin(); it != content_.end(); ++it)
      {
        assert(*it != NULL);

        Json::Value item;
        (*it)->Serialize(item);
        target.append(item);
      }
    }
  };


  static void SetKeyValueStore(const std::string& key,
                               const std::string& value)
  {
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
    OrthancPlugins::KeyValueStore store(KEY_VALUE_STORE);
    store.Store(key, value);
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


  static std::string GetInfoKey(const AnnotationsId& annotations)
  {
    return annotations.GetKey() + "|info";
  }


  static std::string GetLayersKey(const AnnotationsId& annotations,
                                  const UserId& user)
  {
    return annotations.GetKey() + "|layers|" + user.GetKey();
  }


  static std::string GetFeaturesKey(const AnnotationsId& annotations,
                                    const UserId& user)
  {
    return annotations.GetKey() + "|features|" + user.GetKey();
  }



  static const char* const KEY_ACTIVE_USERS = "active-users";
  static const char* const KEY_PROJECT_NAME = "project-name";
  static const char* const KEY_PROJECT_DESCRIPTION = "project-description";
  static const char* const KEY_USER_LAYERS = "user-layers";
  static const char* const KEY_SHARED_LAYERS = "shared-layers";


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

    UserLayer(const Json::Value& source)
    {
      if (!source.isObject() ||
          !source.isMember(KEY_SHARED_WITH))
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      isVisible_ = Orthanc::SerializationToolbox::ReadBoolean(source, KEY_VISIBLE);
      color_ = BackgroundColor::FromHexadecimalString(Orthanc::SerializationToolbox::ReadString(source, KEY_COLOR));
      id_ = Orthanc::SerializationToolbox::ReadString(source, KEY_ID);
      name_ = Orthanc::SerializationToolbox::ReadString(source, KEY_NAME);
      isPublic_ = Orthanc::SerializationToolbox::ReadBoolean(source, KEY_PUBLIC);

      const Json::Value& sharedWith = source[KEY_SHARED_WITH];

      if (!sharedWith.isArray())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
      }

      for (Json::Value::ArrayIndex i = 0; i < sharedWith.size(); i++)
      {
        sharedWith_.insert(UserId(sharedWith[i]));
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
      return (isPublic_ ||
              sharedWith_.find(user) != sharedWith_.end());
    }

    void Assign(const UserLayer& other)
    {
      isVisible_ = other.isVisible_;
      color_ = other.color_;
      name_ = other.name_;
      sharedWith_ = other.sharedWith_;
      isPublic_ = other.isPublic_;
    }

    virtual void Serialize(Json::Value& target) const ORTHANC_OVERRIDE
    {
      Json::Value sharedWith = Json::arrayValue;
      for (std::set<UserId>::const_iterator it = sharedWith_.begin(); it != sharedWith_.end(); ++it)
      {
        Json::Value item;
        it->Serialize(item);
        sharedWith.append(item);
      }

      target = Json::objectValue;
      target[KEY_VISIBLE] = isVisible_;
      target[KEY_COLOR] = color_.ToHexadecimalString();
      target[KEY_ID] = id_;
      target[KEY_NAME] = name_;
      target[KEY_PUBLIC] = isPublic_;
      target[KEY_SHARED_WITH] = sharedWith;
    }
  };


  class SharedLayer : public ILayer
  {
  private:
    bool         isVisible_;
    BackgroundColor     color_;
    UserId       author_;
    std::string  id_;
    std::string  name_;

  public:
    SharedLayer(const UserId& author,
                const UserLayer& layer) :
      isVisible_(true),
      color_(layer.GetColor()),
      author_(author),
      id_(layer.GetId()),
      name_(layer.GetName())
    {
    }

    SharedLayer(const Json::Value& source)
    {
      if (!source.isObject() ||
          !source.isMember(KEY_AUTHOR))
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      isVisible_ = Orthanc::SerializationToolbox::ReadBoolean(source, KEY_VISIBLE);
      author_ = UserId(source[KEY_AUTHOR]);
      id_ = Orthanc::SerializationToolbox::ReadString(source, KEY_ID);
      name_ = Orthanc::SerializationToolbox::ReadString(source, KEY_NAME);
      color_ = BackgroundColor::FromHexadecimalString(Orthanc::SerializationToolbox::ReadString(source, KEY_COLOR));
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

    virtual void Serialize(Json::Value& target) const ORTHANC_OVERRIDE
    {
      target = Json::objectValue;
      target[KEY_VISIBLE] = isVisible_;
      target[KEY_COLOR] = color_.ToHexadecimalString();
      target[KEY_ID] = id_;
      target[KEY_NAME] = name_;

      author_.Serialize(target[KEY_AUTHOR]);
    }
  };


  class UserData : public ISerializable
  {
  private:
    LayersCollection    userLayers_;

  public:
    UserData()
    {
    }

    UserData(const Json::Value& source)
    {
      if (!source.isArray())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }
      else
      {
        for (Json::Value::ArrayIndex i = 0; i < source.size(); i++)
        {
          userLayers_.AddLayer(new UserLayer(source[i]));
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

    void DeleteUserLayer(const std::string& layerId)
    {
      userLayers_.DeleteLayer(layerId);
    }

    virtual void Serialize(Json::Value& target) const ORTHANC_OVERRIDE
    {
      userLayers_.Serialize(target);
    }
  };


  class AnnotationsInfo : public ISerializable
  {
  private:
    std::string        projectName_;
    std::string        projectDescription_;
    std::set<UserId>   activeUsers_;

  public:
    AnnotationsInfo()
    {
    }

    AnnotationsInfo(const Json::Value& source)
    {
      projectName_ = Orthanc::SerializationToolbox::ReadString(source, KEY_PROJECT_NAME);
      projectDescription_ = Orthanc::SerializationToolbox::ReadString(source, KEY_PROJECT_DESCRIPTION);

      const Json::Value& users = source[KEY_ACTIVE_USERS];

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

    virtual void Serialize(Json::Value& target) const ORTHANC_OVERRIDE
    {
      Json::Value users = Json::arrayValue;
      for (std::set<UserId>::const_iterator it = activeUsers_.begin(); it != activeUsers_.end(); ++it)
      {
        Json::Value user;
        it->Serialize(user);
        users.append(user);
      }

      target = Json::objectValue;
      target[KEY_PROJECT_NAME] = projectName_;
      target[KEY_PROJECT_DESCRIPTION] = projectDescription_;
      target[KEY_ACTIVE_USERS] = users;
    }
  };


  class CachedAnnotations : public Orthanc::IDynamicObject
  {
  private:
    void LoadUserData(const UserId& user)
    {
      const std::string key = id_.GetKey() + "|layers|" + user.GetKey();

      Json::Value layers;
      if (LookupKeyValueStore(layers, key))
      {
        std::unique_ptr<UserData> item(new UserData(layers));

        if (content_.find(user) == content_.end())  // Should never be false
        {
          content_[user] = item.release();
        }
      }
    }

    typedef std::map<UserId, UserData*>   Content;

    Orthanc::ReaderWriterLock         mutex_;
    std::unique_ptr<AnnotationsInfo>  info_;
    AnnotationsId                     id_;
    Content                           content_;

  public:
    CachedAnnotations(const AnnotationsId& id) :
      id_(id)
    {
      const std::string key = GetInfoKey(id);

      Json::Value info;

      if (LookupKeyValueStore(info, key))
      {
        info_.reset(new AnnotationsInfo(info));

        for (std::set<UserId>::const_iterator it = info_->GetActiveUsers().begin();
             it != info_->GetActiveUsers().end(); ++it)
        {
          LoadUserData(*it);
        }
      }
      else
      {
        info_.reset(new AnnotationsInfo);

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

    ~CachedAnnotations()
    {
      for (Content::iterator it = content_.begin(); it != content_.end(); ++it)
      {
        assert(it->second != NULL);
        delete it->second;
      }
    }

    class UserReader : public boost::noncopyable
    {
    private:
      Orthanc::ReaderWriterLock::ReadLock lock_;
      const AnnotationsInfo&              info_;
      const UserData*                     userData_;

    public:
      UserReader(CachedAnnotations& that,
                 const IAuthenticatedUser& user) :
        lock_(that.mutex_),
        info_(*that.info_)
      {
        Content::const_iterator found = that.content_.find(user.GetAnnotatingId());

        if (found == that.content_.end())
        {
          userData_ = NULL;
        }
        else
        {
          assert(found->second != NULL);
          userData_ = found->second;
        }
      }

      bool IsValid() const
      {
        return userData_ != NULL;
      }

      const AnnotationsInfo& GetAnnotationsInfo() const
      {
        return info_;
      }

      void ListLayers(Json::Value& target) const
      {
        target = Json::objectValue;

        if (IsValid())
        {
          userData_->Serialize(target[KEY_USER_LAYERS]);
          target[KEY_SHARED_LAYERS] = Json::arrayValue;  // TODO
        }
        else
        {
          target[KEY_USER_LAYERS] = Json::arrayValue;
          target[KEY_SHARED_LAYERS] = Json::arrayValue;
        }
      }
    };


    class UserWriter : public boost::noncopyable
    {
    private:
      Orthanc::ReaderWriterLock::WriteLock  lock_;
      CachedAnnotations&                    that_;
      UserId                                userId_;
      UserData*                             userData_;

      void Commit()
      {
        SetKeyValueStore(GetLayersKey(that_.id_, userId_), *userData_);
      }

    public:
      UserWriter(CachedAnnotations& that,
                 const IAuthenticatedUser& user) :
        lock_(that.mutex_),
        that_(that),
        userId_(user.GetAnnotatingId())
      {
        const UserId id = user.GetAnnotatingId();

        if (that.info_->AddActiveUser(userId_))
        {
          // Only update the key-value store if this is the first time we meet this user
          SetKeyValueStore(GetInfoKey(that.id_), *that.info_);
        }

        Content::iterator found = that.content_.find(userId_);

        if (found == that.content_.end())
        {
          std::unique_ptr<UserData> layers(new UserData);
          userData_ = layers.get();
          that.content_[userId_] = layers.release();
          Commit();
        }
        else
        {
          assert(found->second != NULL);
          userData_ = found->second;
        }
      }

      void CreateUserLayer(Json::Value& answer)
      {
        assert(userData_ != NULL);

        const std::string layerId = userData_->CreateUserLayer();
        Commit();

        UserLayer& layer = userData_->GetUserLayer(layerId);
        layer.Serialize(answer);
      }

      void UpdateUserLayer(const UserLayer& updated)
      {
        assert(userData_ != NULL);

        UserLayer& layer = userData_->GetUserLayer(updated.GetId());
        layer.Assign(updated);
        Commit();
      }

      void DeleteUserLayer(const std::string& layerId)
      {
        assert(userData_ != NULL);
        userData_->DeleteUserLayer(layerId);
        Commit();
      }
    };


#if 0
    void GetSharedLayers(Json::Value& layers,
                         const IAuthenticatedUser& user)
    {
      Orthanc::ReaderWriterLock::ReadLock lock(mutex_);

      layers = Json::arrayValue;   // TODO
    }

    bool HasAccessToLayer(const IAuthenticatedUser& user,
                          const UserId& author,
                          const std::string& layerId)
    {
      Orthanc::ReaderWriterLock::ReadLock lock(mutex_);

      return false;  // TODO
    }

    void CreateUserLayer(const IAuthenticatedUser& user)
    {
      // TODO
    }

    void AnswerLayers(OrthancPluginRestOutput* output,
                      const IAuthenticatedUser& user)
    {

      ViewerToolbox::AnswerJson(output, answer);
    }
#endif
  };


  class AnnotationsCommandContext : public boost::noncopyable
  {
  private:
    std::unique_ptr<IAuthenticatedUser>         user_;
    Json::Value                                 body_;
    std::unique_ptr<AnnotationsId>              annotationsId_;
    boost::shared_ptr<Orthanc::IDynamicObject>  cachedAnnotations_;

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

      annotationsId_.reset(new AnnotationsId(projectId, Orthanc::StringToResourceType(level.c_str()), resourceId, frameNumber));

      IAuthenticatedUser::ProjectRole role = user_->GetRoleInProject(annotationsId_->GetProjectId());

      if (role != IAuthenticatedUser::ProjectRole_Instructor &&
          role != IAuthenticatedUser::ProjectRole_Learner)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess, "User \"" + user_->Format() +
                                        "\" is not instructor or learner of project \"" + annotationsId_->GetProjectId() + "\"");
      }

      const std::string cacheKey = annotationsId_->GetKey();

      static Orthanc::SharedObjectCache annotationsCache_(100);  // TODO - PARAMETER

      cachedAnnotations_ = annotationsCache_.GetCachedValue(cacheKey);

      if (cachedAnnotations_.get() == NULL)
      {
        cachedAnnotations_.reset(new CachedAnnotations(*annotationsId_));
        annotationsCache_.Store(cacheKey, cachedAnnotations_, 1);
      }
    }

    const IAuthenticatedUser& GetUser() const
    {
      assert(user_.get() != NULL);
      return *user_;
    }

    const AnnotationsId& GetAnnotationsId() const
    {
      assert(annotationsId_.get() != NULL);
      return *annotationsId_;
    }

    std::string GetFeaturesKey() const
    {
      return ::OrthancWSI::GetFeaturesKey(GetAnnotationsId(), GetUser().GetAnnotatingId());
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

    CachedAnnotations& GetCachedAnnotations()
    {
      return dynamic_cast<CachedAnnotations&>(*cachedAnnotations_);
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




  void GetAnnotationsInfo(OrthancPluginRestOutput* output,
                          const char* url,
                          const OrthancPluginHttpRequest* request)
  {
    if (ProtectPostRequest(output, request))
    {
      AnnotationsCommandContext context(request);

      CachedAnnotations::UserReader reader(context.GetCachedAnnotations(), context.GetUser());

      Json::Value answer;
      answer["enabled"] = ViewerConfiguration::GetInstance().AreAnnotationsEnabled();
      answer["project-name"] = reader.GetAnnotationsInfo().GetProjectName();
      answer["project-description"] = reader.GetAnnotationsInfo().GetProjectDescription();
      answer["user"] = context.GetUser().Format();

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
      answer["persistent-annotations"] = true;
#else
      answer["persistent-annotations"] = false;
#endif

      ViewerToolbox::AnswerJson(output, answer);
    }
  }


  void ListLayers(OrthancPluginRestOutput* output,
                  const char* url,
                  const OrthancPluginHttpRequest* request)
  {
    if (ProtectPostRequest(output, request))
    {
      AnnotationsCommandContext context(request);

      CachedAnnotations::UserReader reader(context.GetCachedAnnotations(), context.GetUser());

      Json::Value answer;
      reader.ListLayers(answer);

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

      CachedAnnotations::UserWriter writer(context.GetCachedAnnotations(), context.GetUser());

      Json::Value answer;
      writer.CreateUserLayer(answer);

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
        CachedAnnotations::UserWriter writer(context.GetCachedAnnotations(), context.GetUser());
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

      const std::string layerId = context.GetBodyString("layer-id");

      {
        CachedAnnotations::UserWriter writer(context.GetCachedAnnotations(), context.GetUser());
        writer.DeleteUserLayer(layerId);
      }

      ViewerToolbox::AnswerEmpty(output);
    }
  }


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
      answer[KEY_FEATURES] = Json::arrayValue;

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
      std::string compressed;
      if (LookupKeyValueStore(compressed, context.GetFeaturesKey()))
      {
        std::string uncompressed;
        Orthanc::GzipCompressor compressor;
        Orthanc::IBufferCompressor::Uncompress(uncompressed, compressor, compressed);

        Json::Value unserialized;

        if (!Orthanc::Toolbox::ReadJson(unserialized, uncompressed) ||
            !unserialized.isObject() ||
            !unserialized.isMember(KEY_FEATURES))
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
        }

        const unsigned int version = Orthanc::SerializationToolbox::ReadUnsignedInteger(unserialized, KEY_VERSION);

        if (version == ORTHANC_WSI_ANNOTATIONS_VERSION)
        {
          answer[KEY_FEATURES] = unserialized[KEY_FEATURES];
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
#else
      LOG(WARNING) << "Your Orthanc SDK is too old to load annotations";
#endif

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

      Json::Value content;
      content[KEY_VERSION] = static_cast<unsigned int>(ORTHANC_WSI_ANNOTATIONS_VERSION);
      content[KEY_FEATURES] = context.GetBodyField(KEY_FEATURES);

      std::string serialized;
      Orthanc::Toolbox::WriteFastJson(serialized, content);

      std::string compressed;
      Orthanc::GzipCompressor compressor;
      Orthanc::IBufferCompressor::Compress(compressed, compressor, serialized);

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
      SetKeyValueStore(context.GetFeaturesKey(), compressed);
#else
      LOG(WARNING) << "Your Orthanc SDK is too old to save annotations";
#endif

      ViewerToolbox::AnswerEmpty(output);
    }
  }


  void ListSharedLayers(OrthancPluginRestOutput* output,
                        const char* url,
                        const OrthancPluginHttpRequest* request)
  {
    if (request->method != OrthancPluginHttpMethod_Post)
    {
      OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
    }
    else
    {
      std::unique_ptr<IAuthenticatedUser> user(IAuthenticatedUser::FromHttpRequest(request));

      Json::Value body;
      if (!Orthanc::Toolbox::ReadJson(body, request->body, request->bodySize) ||
          !body.isObject())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
      }

      Json::Value answer;

      for (size_t i = 0; i < 10; i++)
      {
        const std::string userId = "User " + boost::lexical_cast<std::string>(i);

        Json::Value layers = Json::arrayValue;
        for (size_t j = 0; j < 5; j++) {
          Json::Value layer;
          layer["id"] = Orthanc::Toolbox::GenerateUuid();
          layer["name"] = "Layer " + boost::lexical_cast<std::string>(j);
          layers.append(layer);
        }

        Json::Value user;
        user["layers"] = layers;
        answer[userId] = user;
      }

      ViewerToolbox::AnswerJson(output, answer);
    }
  }
}


void RegisterAnnotationsRestApi()
{
  OrthancPlugins::RegisterRestCallback<OrthancWSI::GetAnnotationsInfo>("/wsi/api/annotations-info", true);

  if (OrthancWSI::ViewerConfiguration::GetInstance().AreAnnotationsEnabled())
  {
    OrthancPlugins::RegisterRestCallback<OrthancWSI::CreateUserLayer>("/wsi/api/create-user-layer", true);
    OrthancPlugins::RegisterRestCallback<OrthancWSI::DeleteUserLayer>("/wsi/api/delete-user-layer", true);
    OrthancPlugins::RegisterRestCallback<OrthancWSI::ListLayers>("/wsi/api/list-layers", true);
    OrthancPlugins::RegisterRestCallback<OrthancWSI::SaveUserLayer>("/wsi/api/save-user-layer", true);
    OrthancPlugins::RegisterRestCallback<OrthancWSI::LoadUserFeatures>("/wsi/api/load-user-features", true);
    OrthancPlugins::RegisterRestCallback<OrthancWSI::SaveUserFeatures>("/wsi/api/save-user-features", true);

    // TODO
    OrthancPlugins::RegisterRestCallback<OrthancWSI::ListSharedLayers>("/wsi/api/shared-layers", true);
  }
}
