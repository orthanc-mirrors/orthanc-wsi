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


#include "../Framework/PrecompiledHeadersWSI.h"
#include "ViewerConfiguration.h"

#include <Toolbox.h>


namespace OrthancWSI
{
  ViewerConfiguration::ViewerConfiguration()
  {
    mainConfiguration_.GetSection(wsiConfiguration_, "WholeSlideImaging");
  }


  const ViewerConfiguration& ViewerConfiguration::GetInstance()
  {
    static ViewerConfiguration configuration;
    return configuration;
  }


  bool ViewerConfiguration::IsIIIFEnabled() const
  {
    return wsiConfiguration_.GetBooleanValue("EnableIIIF", true);
  }


  std::string ViewerConfiguration::GetOrthancPublicUrl() const
  {
    std::string base;

    if (wsiConfiguration_.LookupStringValue(base, "OrthancPublicURL"))
    {
      return base;
    }
    else
    {
      unsigned int port = mainConfiguration_.GetUnsignedIntegerValue("HttpPort", 8042);
      return "http://localhost:" + boost::lexical_cast<std::string>(port);
    }
  }


  std::string ViewerConfiguration::GetIIIFPublicUrl() const
  {
    if (IsIIIFEnabled())
    {
      return Orthanc::Toolbox::JoinUri(GetOrthancPublicUrl(), "wsi/iiif/");
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
  }


  bool ViewerConfiguration::IsServeMirador() const
  {
    if (IsIIIFEnabled())
    {
      return wsiConfiguration_.GetBooleanValue("ServeMirador", false);
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
  }


  bool ViewerConfiguration::IsServeOpenSeadragon() const
  {
    if (IsIIIFEnabled())
    {
      return wsiConfiguration_.GetBooleanValue("ServeOpenSeadragon", false);
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
  }


  bool ViewerConfiguration::LookupForcePowersOfTwoScaleFactors(bool& value) const
  {
    if (IsIIIFEnabled())
    {
      return wsiConfiguration_.LookupBooleanValue(value, "ForcePowersOfTwoScaleFactors");
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
  }


  bool ViewerConfiguration::AreAnnotationsEnabled() const
  {
    return wsiConfiguration_.GetBooleanValue("EnableAnnotations", true);
  }
}
