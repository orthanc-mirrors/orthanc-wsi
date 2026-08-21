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


#include "BackgroundColor.h"

#include "ImageToolbox.h"

#include <OrthancException.h>

#include <boost/lexical_cast.hpp>


namespace OrthancWSI
{
  void BackgroundColor::Clear()
  {
    present_ = false;
    red_ = 0;
    green_ = 0;
    blue_ = 0;
  }


  void BackgroundColor::SetValue(uint8_t red,
                                 uint8_t green,
                                 uint8_t blue)
  {
    present_ = true;
    red_ = red;
    green_ = green;
    blue_ = blue;
  }


  uint8_t BackgroundColor::GetRed() const
  {
    if (present_)
    {
      return red_;
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
  }


  uint8_t BackgroundColor::GetGreen() const
  {
    if (present_)
    {
      return green_;
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
  }


  uint8_t BackgroundColor::GetBlue() const
  {
    if (present_)
    {
      return blue_;
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
  }


  std::string BackgroundColor::Format() const
  {
    if (present_)
    {
      return ("(" + boost::lexical_cast<std::string>(static_cast<unsigned int>(red_)) + "," +
              boost::lexical_cast<std::string>(static_cast<unsigned int>(green_)) + "," +
              boost::lexical_cast<std::string>(static_cast<unsigned int>(blue_)) + ")");
    }
    else
    {
      return "None";
    }
  }


  void BackgroundColor::Fill(Orthanc::ImageAccessor& region,
                             uint8_t defaultRed,
                             uint8_t defaultGreen,
                             uint8_t defaultBlue) const
  {
    if (present_)
    {
      ImageToolbox::Set(region, red_, green_, blue_);
    }
    else
    {
      ImageToolbox::Set(region, defaultRed, defaultGreen, defaultBlue);
    }
  }
}
