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


#include "IAuthenticatedUser.h"

#include <Compatibility.h>
#include <OrthancException.h>
#include <SerializationToolbox.h>
#include <Toolbox.h>

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <cassert>
#include <json/reader.h>

namespace
{
  // If Orthanc runs without an authentication plugin
  class RootUser : public IAuthenticatedUser
  {
  public:
    virtual std::string GetId() const ORTHANC_OVERRIDE
    {
      return "";
    }

    virtual ProjectRole GetRoleInProject(const std::string& projectId) const ORTHANC_OVERRIDE
    {
      return ProjectRole_Instructor;
    }

    virtual std::string GetAnnotationKey(const std::string& projectId,
                                         const std::string& level,
                                         const std::string& resourceId) const ORTHANC_OVERRIDE
    {
      return "root|" + projectId + "|" + level + "|" + resourceId;
    }
  };


  class AnonymousUser : public IAuthenticatedUser
  {
  public:
    virtual std::string GetId() const ORTHANC_OVERRIDE
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }

    virtual ProjectRole GetRoleInProject(const std::string& projectId) const ORTHANC_OVERRIDE
    {
      return ProjectRole_Guest;
    }

    virtual std::string GetAnnotationKey(const std::string& projectId,
                                         const std::string& level,
                                         const std::string& resourceId) const ORTHANC_OVERRIDE
    {
      // Anonymous users cannot save annotations
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
  };


  class EducationPluginAuthenticatedUser : public IAuthenticatedUser
  {
  private:
    enum EducationRole
    {
      EducationRole_Admin,
      EducationRole_Standard,
      EducationRole_Guest
    };

    std::string            id_;
    EducationRole          role_;
    std::set<std::string>  instructorOfProjects_;
    std::set<std::string>  learnerOfProjects_;

  public:
    explicit EducationPluginAuthenticatedUser(const Json::Value& authentication)
    {
      assert(Orthanc::SerializationToolbox::ReadString(authentication, "source") == "orthanc-education");

      id_ = Orthanc::SerializationToolbox::ReadString(authentication, "id");

      const std::string role = Orthanc::SerializationToolbox::ReadString(authentication, "role");

      if (role == "admin")
      {
        role_ = EducationRole_Admin;
      }
      else if (role == "standard")
      {
        role_ = EducationRole_Standard;
      }
      else if (role == "guest")
      {
        role_ = EducationRole_Guest;
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
      }

      Orthanc::SerializationToolbox::ReadSetOfStrings(instructorOfProjects_, authentication, "instructor_of");
      Orthanc::SerializationToolbox::ReadSetOfStrings(learnerOfProjects_, authentication, "learner_of");
    }

    virtual std::string GetId() const ORTHANC_OVERRIDE
    {
      return id_;
    }

    virtual ProjectRole GetRoleInProject(const std::string& projectId) const ORTHANC_OVERRIDE
    {
      switch (role_)
      {
      case EducationRole_Admin:
        return ProjectRole_Instructor;

      case EducationRole_Standard:
        if (instructorOfProjects_.find(projectId) != instructorOfProjects_.end())
        {
          return ProjectRole_Instructor;
        }
        else if (learnerOfProjects_.find(projectId) != learnerOfProjects_.end())
        {
          return ProjectRole_Learner;
        }
        else
        {
          return ProjectRole_Guest;
        }

      case EducationRole_Guest:
        return ProjectRole_Guest;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }
    }

    virtual std::string GetAnnotationKey(const std::string& projectId,
                                         const std::string& level,
                                         const std::string& resourceId) const ORTHANC_OVERRIDE
    {
      switch (role_)
      {
      case EducationRole_Admin:
        return RootUser().GetAnnotationKey(projectId, level, resourceId);

      case EducationRole_Standard:
      {
        // The pipe character "|" is not part of Base64, so we can safely use it to separate components
        std::string s;
        Orthanc::Toolbox::EncodeBase64(s, id_);
        return ("user_" + s + "|" + projectId + "|" + level + "|" + resourceId);
      }

      case EducationRole_Guest:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess);

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }
    }
  };
}


IAuthenticatedUser* IAuthenticatedUser::FromHttpRequest(const OrthancPluginHttpRequest* request)
{
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 9)

  if (request->authenticationPayloadSize == 0)
  {
    // No authentication plugin is installed
    return new RootUser;
  }
  else
  {
    const char* payload = reinterpret_cast<const char*>(request->authenticationPayload);

    // We use "Json::Reader" as "Orthanc::ReadJson()" would write an
    // error log if the authentication payload is not a JSON string
    Json::Reader reader;

    Json::Value authentication;
    if (reader.parse(payload, payload + request->authenticationPayloadSize, authentication, false))
    {
      const std::string source = Orthanc::SerializationToolbox::ReadString(authentication, "source", "");

      if (source == "orthanc-education")
      {
        return new EducationPluginAuthenticatedUser(authentication);
      }
    }

    return new AnonymousUser;
  }

#else

  // SDK is too old to support per-user authentication
  return new RootUser;

#endif
}
