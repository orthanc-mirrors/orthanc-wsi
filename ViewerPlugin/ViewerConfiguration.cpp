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

#include <SerializationToolbox.h>
#include <Toolbox.h>


static const char* const KEY_AUTHENTICATION_SOURCE = "AuthenticationSource";
static const char* const KEY_AUTHENTICATION_HTTP_HEADER = "AuthenticationHttpHeader";
static const char* const KEY_AUTHENTICATION_ENABLED = "AuthenticationEnabled";


namespace OrthancWSI
{
  ViewerConfiguration::ViewerConfiguration()
  {
    mainConfiguration_.GetSection(wsiConfiguration_, "WholeSlideImaging");

    std::string value;
    if (wsiConfiguration_.LookupStringValue(value, KEY_AUTHENTICATION_SOURCE))
    {
      if (value == "None")
      {
        authenticationSource_ = AuthenticationSource_None;
      }
      else if (value == "HttpHeader")
      {
        authenticationSource_ = AuthenticationSource_HttpHeader;

        if (!wsiConfiguration_.LookupStringValue(authenticationHttpHeader_, KEY_AUTHENTICATION_HTTP_HEADER) ||
            authenticationHttpHeader_.empty())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange, "Configuration option \"" +
                                          std::string(KEY_AUTHENTICATION_HTTP_HEADER) + "\" must be defined and non-empty");
        }

        Orthanc::Toolbox::ToLowerCase(authenticationHttpHeader_);
      }
      else if (value == "RegisteredUsers")
      {
        if (mainConfiguration_.GetBooleanValue(KEY_AUTHENTICATION_ENABLED, false))
        {
          authenticationSource_ = AuthenticationSource_RegisteredUsers;
        }
        else
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange, "Configuration option \"" +
                                          std::string(KEY_AUTHENTICATION_ENABLED) + "\" must be set to \"true\" " +
                                          "to use the registered users as the authentication source");
        }
      }
      else if (value == "Plugin")
      {
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 9)
        authenticationSource_ = AuthenticationSource_Plugin;
#else
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "Your Orthanc SDK is too old to support "
                                        "authentication from plugins, check configuration option \"" +
                                        std::string(KEY_AUTHENTICATION_SOURCE) + "\"");
#endif
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                        "Unknown source of authentication: " + value);
      }
    }
    else
    {
      authenticationSource_ = AuthenticationSource_None;
    }
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


  const std::string& ViewerConfiguration::GetAuthenticationHttpHeader() const
  {
    if (authenticationSource_ != AuthenticationSource_HttpHeader)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      assert(!authenticationHttpHeader_.empty());
      return authenticationHttpHeader_;
    }
  }


  unsigned int ViewerConfiguration::GetAnnotationsCacheSize() const
  {
    return 100;   // TODO - Configuration option?
  }
}
