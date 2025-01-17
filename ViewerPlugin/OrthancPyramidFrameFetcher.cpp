/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2025 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2025 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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
#include "OrthancPyramidFrameFetcher.h"

#include "../Framework/Inputs/OnTheFlyPyramid.h"

#include <Images/Image.h>
#include <Images/ImageProcessing.h>

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"


namespace OrthancWSI
{
  void OrthancPyramidFrameFetcher::RenderGrayscale(Orthanc::ImageAccessor& target,
                                                   const Orthanc::ImageAccessor& source)
  {
    Orthanc::Image converted(Orthanc::PixelFormat_Float32, source.GetWidth(), source.GetHeight(), false);
    Orthanc::ImageProcessing::Convert(converted, source);

    float minValue, maxValue;
    Orthanc::ImageProcessing::GetMinMaxFloatValue(minValue, maxValue, converted);

    assert(minValue <= maxValue);
    if (std::abs(maxValue - minValue) < 0.0001)
    {
      Orthanc::ImageProcessing::Set(target, 0);
    }
    else
    {
      const float scaling = 255.0f / (maxValue - minValue);
      const float offset = -minValue;

      Orthanc::Image rescaled(Orthanc::PixelFormat_Grayscale8, source.GetWidth(), source.GetHeight(), false);
      Orthanc::ImageProcessing::ShiftScale(rescaled, converted, static_cast<float>(offset), static_cast<float>(scaling), false);
      Orthanc::ImageProcessing::Convert(target, rescaled);
    }
  }


  OrthancPyramidFrameFetcher::OrthancPyramidFrameFetcher(OrthancStone::IOrthancConnection* orthanc,
                                                         bool smooth) :
    orthanc_(orthanc),
    smooth_(smooth),
    tileWidth_(512),
    tileHeight_(512),
    paddingX_(0),
    paddingY_(0),
    defaultBackgroundRed_(0),
    defaultBackgroundGreen_(0),
    defaultBackgroundBlue_(0)
  {
    if (orthanc == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }
  }


  void OrthancPyramidFrameFetcher::SetTileWidth(unsigned int tileWidth)
  {
    if (tileWidth <= 2)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    else
    {
      tileWidth_ = tileWidth;
    }
  }


  void OrthancPyramidFrameFetcher::SetTileHeight(unsigned int tileHeight)
  {
    if (tileHeight <= 2)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    else
    {
      tileHeight_ = tileHeight;
    }
  }


  void OrthancPyramidFrameFetcher::SetDefaultBackgroundColor(uint8_t red,
                                                             uint8_t green,
                                                             uint8_t blue)
  {
    defaultBackgroundRed_ = red;
    defaultBackgroundGreen_ = green;
    defaultBackgroundBlue_ = blue;
  }


  DecodedTiledPyramid* OrthancPyramidFrameFetcher::Fetch(const std::string &instanceId,
                                                         unsigned frameNumber)
  {
    OrthancPlugins::MemoryBuffer buffer;
    buffer.GetDicomInstance(instanceId.c_str());

    OrthancPlugins::DicomInstance dicom(buffer.GetData(), buffer.GetSize());

    uint8_t backgroundRed = defaultBackgroundRed_;
    uint8_t backgroundGreen = defaultBackgroundGreen_;
    uint8_t backgroundBlue = defaultBackgroundBlue_;

    Json::Value tags;
    dicom.GetSimplifiedJson(tags);

    static const char* const PHOTOMETRIC_INTERPRETATION = "PhotometricInterpretation";

    if (tags.isMember(PHOTOMETRIC_INTERPRETATION) &&
        tags[PHOTOMETRIC_INTERPRETATION].type() == Json::stringValue)
    {
      std::string p = tags[PHOTOMETRIC_INTERPRETATION].asString();
      if (p == "MONOCHROME1")
      {
        backgroundRed = 255;
        backgroundGreen = 255;
        backgroundBlue = 255;
      }
      else if (p == "MONOCHROME2")
      {
        backgroundRed = 0;
        backgroundGreen = 0;
        backgroundBlue = 0;
      }
    }

    std::unique_ptr<OrthancPlugins::OrthancImage> frame(dicom.GetDecodedFrame(frameNumber));

    Orthanc::PixelFormat format;
    switch (frame->GetPixelFormat())
    {
    case OrthancPluginPixelFormat_RGB24:
      format = Orthanc::PixelFormat_RGB24;
      break;

    case OrthancPluginPixelFormat_Grayscale8:
      format = Orthanc::PixelFormat_Grayscale8;
      break;

    case OrthancPluginPixelFormat_Grayscale16:
      format = Orthanc::PixelFormat_Grayscale16;
      break;

    default:
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
    }

    Orthanc::ImageAccessor source;
    source.AssignReadOnly(format, frame->GetWidth(), frame->GetHeight(), frame->GetPitch(), frame->GetBuffer());

    unsigned int paddedWidth, paddedHeight;

    if (paddingX_ >= 2)
    {
      paddedWidth = OrthancWSI::CeilingDivision(source.GetWidth(), paddingX_) * paddingX_;
    }
    else
    {
      paddedWidth = source.GetWidth();
    }

    if (paddingY_ >= 2)
    {
      paddedHeight = OrthancWSI::CeilingDivision(source.GetHeight(), paddingY_) * paddingY_;
    }
    else
    {
      paddedHeight = source.GetHeight();
    }

    std::unique_ptr<Orthanc::ImageAccessor> rendered(new Orthanc::Image(Orthanc::PixelFormat_RGB24, paddedWidth, paddedHeight, false));

    if (paddedWidth != source.GetWidth() ||
        paddedHeight != source.GetHeight())
    {
      Orthanc::ImageProcessing::Set(*rendered, backgroundRed, backgroundGreen, backgroundBlue, 255 /* alpha */);
    }

    Orthanc::ImageAccessor region;
    rendered->GetRegion(region, 0, 0, source.GetWidth(), source.GetHeight());

    switch (format)
    {
    case Orthanc::PixelFormat_RGB24:
      Orthanc::ImageProcessing::Copy(region, source);
      break;

    case Orthanc::PixelFormat_Grayscale8:
      Orthanc::ImageProcessing::Convert(region, source);
      break;

    case Orthanc::PixelFormat_Grayscale16:
      RenderGrayscale(region, source);
      break;

    default:
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
    }

    std::unique_ptr<DecodedTiledPyramid> result(new OnTheFlyPyramid(rendered.release(), tileWidth_, tileHeight_, smooth_));
    result->SetBackgroundColor(backgroundRed, backgroundGreen, backgroundBlue);

    return result.release();
  }
}
