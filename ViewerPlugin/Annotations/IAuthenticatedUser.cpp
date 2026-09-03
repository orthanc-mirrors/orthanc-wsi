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
#include "IAuthenticatedUser.h"

#include "../ViewerConfiguration.h"

#include <Compatibility.h>
#include <OrthancException.h>
#include <SerializationToolbox.h>
#include <Toolbox.h>

#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <cassert>
#include <json/reader.h>


namespace OrthancWSI
{
  namespace
  {
    // If Orthanc runs without an authentication plugin
    class RootUser : public IAuthenticatedUser
    {
    public:
      virtual UserId GetAnnotatingId() const ORTHANC_OVERRIDE
      {
        return UserId(UserId::Type_Root);
      }

      virtual std::string Format() const ORTHANC_OVERRIDE
      {
        return "(root)";
      }

      virtual ProjectRole GetRoleInProject(const std::string& projectId) const ORTHANC_OVERRIDE
      {
        return ProjectRole_Instructor;
      }
    };


    class GuestUser : public IAuthenticatedUser
    {
    public:
      virtual UserId GetAnnotatingId() const ORTHANC_OVERRIDE
      {
        // Anonymous users cannot save annotations
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess);
      }

      virtual std::string Format() const ORTHANC_OVERRIDE
      {
        return "(guest)";
      }

      virtual ProjectRole GetRoleInProject(const std::string& projectId) const ORTHANC_OVERRIDE
      {
        return ProjectRole_Guest;
      }
    };


    class EducationPluginUser : public IAuthenticatedUser
    {
    public:
      enum EducationRole
      {
        EducationRole_Administrator,
        EducationRole_Standard,
        EducationRole_Guest
      };

    private:
      std::string            id_;
      EducationRole          role_;
      std::set<std::string>  instructorOfProjects_;
      std::set<std::string>  learnerOfProjects_;

    public:
      explicit EducationPluginUser(const Json::Value& authentication)
      {
        assert(Orthanc::SerializationToolbox::ReadString(authentication, "source") == "orthanc-education");

        id_ = Orthanc::SerializationToolbox::ReadString(authentication, "id");

        const std::string role = Orthanc::SerializationToolbox::ReadString(authentication, "role");

        if (role == "admin")
        {
          role_ = EducationRole_Administrator;
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

      virtual UserId GetAnnotatingId() const ORTHANC_OVERRIDE
      {
        switch (role_)
        {
          case EducationRole_Administrator:
            return UserId(UserId::Type_Root);

          case EducationRole_Standard:
            return UserId(UserId::Type_Standard, id_);

          case EducationRole_Guest:
            // Anonymous users cannot save annotations
            throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess);

          default:
            throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
        }
      }

      virtual std::string Format() const ORTHANC_OVERRIDE
      {
        return id_;
      }

      virtual ProjectRole GetRoleInProject(const std::string& projectId) const ORTHANC_OVERRIDE
      {
        switch (role_)
        {
          case EducationRole_Administrator:
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
    };


    class GenericStandardUser : public IAuthenticatedUser
    {
    private:
      ProjectRole  role_;
      std::string  username_;

    public:
      GenericStandardUser(ProjectRole role,
                          const std::string& username) :
        role_(role),
        username_(username)
      {
      }

      virtual UserId GetAnnotatingId() const ORTHANC_OVERRIDE
      {
        return UserId(UserId::Type_Standard, username_);
      }

      virtual std::string Format() const ORTHANC_OVERRIDE
      {
        return username_;
      }

      virtual ProjectRole GetRoleInProject(const std::string& projectId) const ORTHANC_OVERRIDE
      {
        return role_;
      }
    };
  }


  static IAuthenticatedUser* FromRegisteredUsers(const OrthancPluginHttpRequest* request)
  {
    for (uint32_t i = 0; i < request->headersCount; i++)
    {
      if (std::string(request->headersKeys[i]) == "authorization")
      {
        const std::string value(request->headersValues[i]);

        std::vector<std::string> tokens;
        Orthanc::Toolbox::TokenizeString(tokens, value, ' ');

        if (tokens.size() == 2 &&
            tokens[0] == "Basic")
        {
          std::string decoded;
          Orthanc::Toolbox::DecodeBase64(decoded, tokens[1]);

          Orthanc::Toolbox::TokenizeString(tokens, decoded, ':');
          if (!tokens.empty() &&
              !tokens[0].empty())
          {
            //return new GenericStandardUser(IAuthenticatedUser::ProjectRole_Instructor, tokens[0]);
            return new GenericStandardUser(IAuthenticatedUser::ProjectRole_Learner, tokens[0]);
          }
        }
      }
    }

    throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess,
                                    "Forbidden access, HTTP basic authentication is missing");
  }


  static IAuthenticatedUser* FromHttpHeader(const OrthancPluginHttpRequest* request)
  {
    const std::string& header = ViewerConfiguration::GetInstance().GetAuthenticationHttpHeader();

    for (uint32_t i = 0; i < request->headersCount; i++)
    {
      if (std::string(request->headersKeys[i]) == header)
      {
        const std::string user(request->headersValues[i]);

        if (user.empty())
        {
          return new GuestUser;
        }
        else
        {
          return new GenericStandardUser(IAuthenticatedUser::ProjectRole_Instructor, request->headersValues[i]);
        }
      }
    }

    throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess,
                                    "Forbidden access, as HTTP header \"" + header + "\" is not set by your proxy");
  }


#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 9)
  static IAuthenticatedUser* FromPlugin(const OrthancPluginHttpRequest* request)
  {
    if (request->authenticationPayloadSize == 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError, "No authentication plugin is properly installed");
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
        const std::string source = Orthanc::SerializationToolbox::ReadString(authentication, "source", "(none)");

        if (source == "orthanc-education")
        {
          return new EducationPluginUser(authentication);
        }
        else
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "Unknown authentication plugin: " + source);
        }
      }

      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "Unknown authentication plugin");
    }
  }
#endif



  IAuthenticatedUser* IAuthenticatedUser::FromHttpRequest(const OrthancPluginHttpRequest* request)
  {
    switch (ViewerConfiguration::GetInstance().GetAuthenticationSource())
    {
      case AuthenticationSource_None:
        // No authentication is available, use the root user of Orthanc
        return new RootUser;

      case AuthenticationSource_RegisteredUsers:
        return FromRegisteredUsers(request);

      case AuthenticationSource_HttpHeader:
        return FromHttpHeader(request);

      case AuthenticationSource_Plugin:
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 9)
        return FromPlugin(request);
#else
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
#endif

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }
}
