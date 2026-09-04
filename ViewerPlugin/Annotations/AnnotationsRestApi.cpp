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
#include "CachedAnnotationsWorkspace.h"
#include "CachedUserFeatures.h"
#include "IAuthenticatedUser.h"

#include <SerializationToolbox.h>
#include <Toolbox.h>

#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"


static const char* const KEY_FEATURES = "features";
static const char* const KEY_LAYER_ID = "layer-id";


namespace OrthancWSI
{
  class AnnotationsCommandContext : public boost::noncopyable
  {
  private:
    std::unique_ptr<IAuthenticatedUser>          user_;
    Json::Value                                  body_;
    std::unique_ptr<CachedAnnotationsWorkspace>  workspace_;
    ProjectRole                                  role_;

  public:
    explicit AnnotationsCommandContext(const OrthancPluginHttpRequest* request)
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

      if (role_ != ProjectRole_Instructor &&
          role_ != ProjectRole_Learner)
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

    ProjectRole GetRole() const
    {
      return role_;
    }

    AnnotationsWorkspace& GetWorkspace()
    {
      assert(workspace_.get() != NULL);
      return workspace_->GetContent();
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

    AnnotationsWorkspace::UserReader* CreateUserReader() const
    {
      return new AnnotationsWorkspace::UserReader(workspace_->GetContent(), GetUser().GetAnnotatingId(), role_);
    }

    AnnotationsWorkspace::UserWriter* CreateUserWriter()
    {
      return new AnnotationsWorkspace::UserWriter(workspace_->GetContent(), GetUser().GetAnnotatingId(), role_);
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
      answer["project"] = context.GetWorkspace().GetId().GetProjectId();
      answer["enabled"] = ViewerConfiguration::GetInstance().AreAnnotationsEnabled();
      answer["sharing"] = (ViewerConfiguration::GetInstance().AreAnnotationsEnabled() &&
                           ViewerConfiguration::GetInstance().IsAnnotationsSharingEnabled());
      answer["user"] = context.GetUser().Format();

      std::string role;
      switch (context.GetRole())
      {
        case ProjectRole_Learner:
          role = "learner";
          break;

        case ProjectRole_Instructor:
          role = "instructor";
          break;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }

      answer["role"] = role;

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
        std::unique_ptr<AnnotationsWorkspace::UserReader> reader(context.CreateUserReader());
        reader->ListLayers(answer);
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
        std::unique_ptr<AnnotationsWorkspace::UserWriter> writer(context.CreateUserWriter());
        writer->CreateUserLayer(answer);
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
        std::unique_ptr<AnnotationsWorkspace::UserWriter> writer(context.CreateUserWriter());
        writer->UpdateUserLayer(updated);
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
        std::unique_ptr<AnnotationsWorkspace::UserWriter> writer(context.CreateUserWriter());
        writer->DeleteUserLayer(layerId);
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


  void ListUsersSharingLayersWithMe(OrthancPluginRestOutput* output,
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

      {
        std::unique_ptr<AnnotationsWorkspace::UserReader> reader(context.CreateUserReader());
        reader->ListUsersSharingLayersWithMe(users);
      }

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


  void ListLayersSharedWithMe(OrthancPluginRestOutput* output,
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
      Json::Value answer;

      {
        std::unique_ptr<AnnotationsWorkspace::UserReader> reader(context.CreateUserReader());
        reader->ListLayersSharedWithMe(answer, author);
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
        std::unique_ptr<AnnotationsWorkspace::UserWriter> writer(context.CreateUserWriter());
        writer->ImportLayer(author, layerId);
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
        std::unique_ptr<AnnotationsWorkspace::UserWriter> writer(context.CreateUserWriter());
        writer->RemoveImportedLayer(layerId);
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
        std::unique_ptr<AnnotationsWorkspace::UserWriter> writer(context.CreateUserWriter());
        writer->UpdateImportedLayer(updated);
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
        std::unique_ptr<AnnotationsWorkspace::UserReader> reader(context.CreateUserReader());
        reader->ListImportedLayers(authors, layerIds);
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
      OrthancPlugins::RegisterRestCallback<OrthancWSI::ListLayersSharedWithMe>("/wsi/api/list-shared-layers", true);
      OrthancPlugins::RegisterRestCallback<OrthancWSI::ListUsersSharingLayersWithMe>("/wsi/api/list-sharing-users", true);
      OrthancPlugins::RegisterRestCallback<OrthancWSI::RemoveImportedLayer>("/wsi/api/remove-imported-layer", true);
      OrthancPlugins::RegisterRestCallback<OrthancWSI::SaveImportedLayer>("/wsi/api/save-imported-layer", true);

      OrthancPlugins::RegisterRestCallback<OrthancWSI::LoadImportedFeatures>("/wsi/api/load-imported-features", true);
    }
  }
}
