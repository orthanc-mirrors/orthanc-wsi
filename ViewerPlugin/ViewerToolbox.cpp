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
#include "ViewerToolbox.h"

#include <OrthancException.h>
#include <Toolbox.h>

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"


static unsigned int GetHex(char c)
{
  if (c >= '0' && c <= '9')
  {
    return c - '0';
  }
  else if (c >= 'a' && c <= 'f')
  {
    return c - 'a' + 10;
  }
  else if (c >= 'A' && c <= 'F')
  {
    return c - 'A' + 10;
  }
  else
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
  }
}


namespace OrthancWSI
{
  namespace ViewerToolbox
  {
    void AnswerJson(OrthancPluginRestOutput* output,
                    const Json::Value& value)
    {
      std::string s;
      Orthanc::Toolbox::WriteFastJson(s, value);
      OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, s.c_str(), s.size(), "application/json");
    }


    void AnswerEmpty(OrthancPluginRestOutput* output)
    {
      Json::Value answer = Json::objectValue;
      AnswerJson(output, answer);
    }


    RGBColor ParseColor(const std::string& color)
    {
      if (color.size() != 7 ||
          color[0] != '#')
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
      }
      else
      {
        unsigned int r = GetHex(color[1]) * 16 + GetHex(color[2]);
        unsigned int g = GetHex(color[3]) * 16 + GetHex(color[4]);
        unsigned int b = GetHex(color[5]) * 16 + GetHex(color[6]);
        return RGBColor(r, g, b);
      }
    }


    std::string SerializeColor(const RGBColor& color)
    {
      char buf[16];
      sprintf(buf, "#%02x%02x%02x", color.GetR(), color.GetG(), color.GetB());
      return buf;
    }
  }
}
