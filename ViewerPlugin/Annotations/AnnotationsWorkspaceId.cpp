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
#include "AnnotationsWorkspaceId.h"

#include <OrthancException.h>
#include <Toolbox.h>

#include <boost/lexical_cast.hpp>


namespace OrthancWSI
{
  std::string AnnotationsWorkspaceId::GetKeyPrefix() const
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


  AnnotationsWorkspaceId::AnnotationsWorkspaceId(const std::string& projectId,
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


  unsigned int AnnotationsWorkspaceId::GetFrameNumber() const
  {
    if (level_ == Orthanc::ResourceType_Instance)
    {
      return frameNumber_;
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
  }


  std::string AnnotationsWorkspaceId::GetInfoKey() const
  {
    return GetKeyPrefix() + "|info";
  }


  std::string AnnotationsWorkspaceId::GetSettingsKey(const UserId& user) const
  {
    return GetKeyPrefix() + "|settings|" + user.GetKey();
  }


  std::string AnnotationsWorkspaceId::GetFeaturesKey(const UserId& user) const
  {
    return GetKeyPrefix() + "|features|" + user.GetKey();
  }
}
