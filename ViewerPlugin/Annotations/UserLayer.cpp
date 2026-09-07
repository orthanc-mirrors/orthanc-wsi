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
#include "UserLayer.h"

#include "../ViewerConfiguration.h"

#include <OrthancException.h>
#include <SerializationToolbox.h>
#include <Toolbox.h>

#include <cassert>


static const char* const KEY_COLOR = "color";
static const char* const KEY_ID = "id";
static const char* const KEY_NAME = "name";
static const char* const KEY_PUBLIC = "public";
static const char* const KEY_SHARED_WITH = "shared_with";
static const char* const KEY_VISIBLE = "visible";


namespace OrthancWSI
{
  UserLayer::UserLayer(const BackgroundColor& color,
                       const std::string& name) :
    isVisible_(true),
    color_(color),
    id_(Orthanc::Toolbox::GenerateUuid()),
    name_(name),
    isPublic_(false)
  {
  }


  UserLayer::UserLayer(const Json::Value& serialized)
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


  bool UserLayer::IsSharedWith(ProjectRole authorRole,
                               const UserId& viewerId,
                               ProjectRole viewerRole) const
  {
    /**

       Instructors can see:

       - All layers tagged "publicly shared with instructors"
         (i.e. public), created by anyone (instructors or learners).

       - Any layer explicitly shared with them, by anyone.

       Learners can see:

       - All layers tagged public that were created by instructors
         (this is true "class-wide public" for instructor content).

       - Any instructor layer explicitly shared with them.

       - Any learner layer explicitly shared with them by name, only
         if learner-to-learner sharing is enabled (cf. configuration
         option "EnableLearnerToLearnerSharing").

       Note 1: Learner layers tagged "public" are visible only to
       instructors, never to other learners, regardless of the
       learner-to-learner sharing configuration. This is a deliberate
       asymmetry: for a learner, "public" means "submitted/visible to
       instructors," not "visible to the class." This prevents one
       learner's work from becoming broadcast to the whole cohort,
       while still allowing small, named-group collaboration (e.g.,
       project teams) through explicit sharing.

       Note 2: Learner-to-learner sharing (configuration option)
       governs only the explicit-share-list channel between
       learners. It has no effect on instructor visibility and no
       effect on the behavior of the "public" tag (public learner
       layers are never learner-visible whether this option is "true"
       or "false").

     **/

    if (viewerId.GetType() != UserId::Type_Standard)
    {
      return false;
    }

    const bool explicitlyShared = sharedWith_.find(viewerId) != sharedWith_.end();

    switch (authorRole)
    {
      case ProjectRole_Instructor:
        // Instructor layers: "public" truly means public to everyone,
        // and explicit sharing is unconditional
        return isPublic_ || explicitlyShared;

      case ProjectRole_Learner:
        switch (viewerRole)
        {
          case ProjectRole_Instructor:
            // Instructors see public learner layers, and anything shared with them
            return isPublic_ || explicitlyShared;

          case ProjectRole_Learner:
            // Learner viewing another learner's layer: "public" never applies,
            // explicit sharing is gated by the configuration switch.
            return explicitlyShared && ViewerConfiguration::GetInstance().IsLearnerToLearnerSharingEnabled();

          case ProjectRole_Guest:
            throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess);

          default:
            throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
        }

      case ProjectRole_Guest:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess);

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
  }


  void UserLayer::Assign(const UserLayer& other)
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


  void UserLayer::Serialize(Json::Value& serialized) const
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
}
