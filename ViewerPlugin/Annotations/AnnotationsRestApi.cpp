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

#include "../ViewerConfiguration.h"
#include "../ViewerToolbox.h"
#include "AnnotationsWorkspace.h"
#include "IAuthenticatedUser.h"

#include <Cache/SharedObjectCache.h>
#include <Compression/GzipCompressor.h>
#include <Logging.h>
#include <SerializationToolbox.h>

#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"


namespace OrthancWSI
{
  static const char* const KEY_FEATURES = "features";
  static const char* const KEY_ID = "id";
  static const char* const KEY_LAYERS = "layers";
  static const char* const KEY_LAYER_ID = "layer-id";
  static const char* const KEY_NAME = "name";
  static const char* const KEY_PUBLIC = "public";
  static const char* const KEY_TYPE = "type";
  static const char* const KEY_VERSION = "version";
  static const char* const KEY_PROJECT_NAME = "project-name";
  static const char* const KEY_PROJECT_DESCRIPTION = "project-description";


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
    std::unique_ptr<CachedAnnotationsWorkspace>  workspace_;
    IAuthenticatedUser::ProjectRole              role_;

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
      const std::string levelString = Orthanc::SerializationToolbox::ReadString(body_, "level");
      const std::string resourceId = Orthanc::SerializationToolbox::ReadString(body_, "resource");
      unsigned int frameNumber = Orthanc::SerializationToolbox::ReadUnsignedInteger(body_, "frame", 0 /* default frame */);

      role_ = user_->GetRoleInProject(projectId);

      if (role_ != IAuthenticatedUser::ProjectRole_Instructor &&
          role_ != IAuthenticatedUser::ProjectRole_Learner)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess, "User \"" + user_->Format() +
                                        "\" is not instructor or learner of project \"" + projectId + "\"");
      }

      Orthanc::ResourceType level = Orthanc::StringToResourceType(levelString.c_str());
      AnnotationsWorkspaceId workspaceId(projectId, level, resourceId, frameNumber);
      workspace_.reset(new CachedAnnotationsWorkspace(workspaceId));
    }

    const IAuthenticatedUser& GetUser() const
    {
      assert(user_.get() != NULL);
      return *user_;
    }

    AnnotationsWorkspace& GetWorkspace()
    {
      assert(workspace_.get() != NULL);
      return workspace_->GetContent();
    }

    IAuthenticatedUser::ProjectRole GetUserRoleInProject() const
    {
      return role_;
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

      answer["name"] = context.GetWorkspace().GetProjectName();
      answer["description"] = context.GetWorkspace().GetProjectDescription();
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
      if (ViewerToolbox::LookupKeyValueStore(compressed, key_))
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

      ViewerToolbox::SetKeyValueStore(key_, compressed);
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
        CachedUserFeatures cached(context.GetWorkspace().GetId(), context.GetUser().GetAnnotatingId());
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
        CachedUserFeatures cached(context.GetWorkspace().GetId(), context.GetUser().GetAnnotatingId());
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
          CachedUserFeatures author(context.GetWorkspace().GetId(), *it);
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
