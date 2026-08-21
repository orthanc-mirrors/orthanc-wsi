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


#pragma once

#include <stdint.h>
#include <string>


namespace Orthanc
{
  class ImageAccessor;
}

namespace OrthancWSI
{
  class BackgroundColor
  {
  private:
    bool     present_;
    uint8_t  red_;
    uint8_t  green_;
    uint8_t  blue_;

  public:
    BackgroundColor()
    {
      Clear();
    }

    BackgroundColor(uint8_t red,
                    uint8_t green,
                    uint8_t blue)
    {
      SetValue(red, green, blue);
    }

    void Clear();

    void SetValue(uint8_t red,
                  uint8_t green,
                  uint8_t blue);

    uint8_t IsPresent() const
    {
      return present_;
    }

    uint8_t GetRed() const;

    uint8_t GetGreen() const;

    uint8_t GetBlue() const;

    std::string Format() const;

    void Fill(Orthanc::ImageAccessor& region,
              uint8_t defaultRed,
              uint8_t defaultGreen,
              uint8_t defaultBlue) const;
  };
}
