/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
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


#include "../PrecompiledHeadersWSI.h"
#include "DicomPyramidInstance.h"

#include "../../Resources/Orthanc/Core/Logging.h"
#include "../../Resources/Orthanc/Core/OrthancException.h"
#include "../../Resources/Orthanc/Core/Toolbox.h"
#include "../DicomToolbox.h"

#include <cassert>

namespace OrthancWSI
{
  static ImageCompression DetectImageCompression(const Json::Value& header)
  {
    std::string s = Orthanc::Toolbox::StripSpaces
      (DicomToolbox::GetMandatoryStringTag(header, "TransferSyntaxUID"));

    if (s == "1.2.840.10008.1.2" ||
        s == "1.2.840.10008.1.2.1")
    {
      return ImageCompression_None;
    }
    else if (s == "1.2.840.10008.1.2.4.50")
    {
      return ImageCompression_Jpeg;
    }
    else if (s == "1.2.840.10008.1.2.4.90" ||
             s == "1.2.840.10008.1.2.4.91")
    {
      return ImageCompression_Jpeg2000;
    }
    else
    {
      LOG(ERROR) << "Unsupported transfer syntax: " << s;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
    }
  }


  static Orthanc::PixelFormat DetectPixelFormat(const Json::Value& dicom)
  {
    std::string photometric = Orthanc::Toolbox::StripSpaces
      (DicomToolbox::GetMandatoryStringTag(dicom, "PhotometricInterpretation"));

    if (photometric == "PALETTE")
    {
      LOG(ERROR) << "Unsupported photometric interpretation: " << photometric;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
    }

    unsigned int bitsStored = DicomToolbox::GetUnsignedIntegerTag(dicom, "BitsStored");
    unsigned int samplesPerPixel = DicomToolbox::GetUnsignedIntegerTag(dicom, "SamplesPerPixel");
    bool isSigned = (DicomToolbox::GetUnsignedIntegerTag(dicom, "PixelRepresentation") != 0);

    if (bitsStored == 8 &&
        samplesPerPixel == 1 &&
        !isSigned)
    {
      return Orthanc::PixelFormat_Grayscale8;
    }
    else if (bitsStored == 8 &&
             samplesPerPixel == 3 &&
             !isSigned)
    {
      return Orthanc::PixelFormat_RGB24;
    }
    else
    {
      LOG(ERROR) << "Unsupported pixel format";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
    }
  }


  ImageCompression  DicomPyramidInstance::GetImageCompression(IOrthancConnection& orthanc)
  {
    /**
     * Lazy detection of the image compression using the transfer
     * syntax stored inside the DICOM header. Given the fact that
     * reading the header is a time-consuming operation (it implies
     * the decoding of the DICOM image by Orthanc, whereas the "/tags"
     * endpoint only reads the "DICOM-as-JSON" attachment), the
     * "/header" REST call is delayed until it is really required.
     **/

    if (!hasCompression_)
    {
      Json::Value header;
      IOrthancConnection::RestApiGet(header, orthanc, "/instances/" + instanceId_ + "/header?simplify");

      hasCompression_ = true;
      compression_ = DetectImageCompression(header);
    }

    return compression_;
  }

  
  DicomPyramidInstance::DicomPyramidInstance(IOrthancConnection&  orthanc,
                                             const std::string& instanceId) :
    instanceId_(instanceId),
    hasCompression_(false)
  {
    Json::Value dicom;
    IOrthancConnection::RestApiGet(dicom, orthanc, "/instances/" + instanceId + "/tags?simplify");

    if (DicomToolbox::GetMandatoryStringTag(dicom, "SOPClassUID") != "1.2.840.10008.5.1.4.1.1.77.1.6" ||
        DicomToolbox::GetMandatoryStringTag(dicom, "Modality") != "SM")
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    format_ = DetectPixelFormat(dicom);
    tileWidth_ = DicomToolbox::GetUnsignedIntegerTag(dicom, "Columns");
    tileHeight_ = DicomToolbox::GetUnsignedIntegerTag(dicom, "Rows");
    totalWidth_ = DicomToolbox::GetUnsignedIntegerTag(dicom, "TotalPixelMatrixColumns");
    totalHeight_ = DicomToolbox::GetUnsignedIntegerTag(dicom, "TotalPixelMatrixRows");

    const Json::Value& frames = DicomToolbox::GetSequenceTag(dicom, "PerFrameFunctionalGroupsSequence");

    if (frames.size() != DicomToolbox::GetUnsignedIntegerTag(dicom, "NumberOfFrames"))
    {
      LOG(ERROR) << "Mismatch between the number of frames in instance: " << instanceId;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    frames_.resize(frames.size());

    for (Json::Value::ArrayIndex i = 0; i < frames.size(); i++)
    {
      const Json::Value& frame = DicomToolbox::GetSequenceTag(frames[i], "PlanePositionSlideSequence");
      if (frame.size() != 1)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      // "-1", because coordinates are shifted by 1 in DICOM
      int xx = DicomToolbox::GetIntegerTag(frame[0], "ColumnPositionInTotalImagePixelMatrix") - 1;
      int yy = DicomToolbox::GetIntegerTag(frame[0], "RowPositionInTotalImagePixelMatrix") - 1;

      unsigned int x = static_cast<unsigned int>(xx);
      unsigned int y = static_cast<unsigned int>(yy);

      if (xx < 0 || 
          yy < 0 ||
          x % tileWidth_ != 0 ||
          y % tileHeight_ != 0 ||
          x >= totalWidth_ ||
          y >= totalHeight_)
      {
        LOG(ERROR) << "Frame " << i << " with unexpected tile location (" 
                   << x << "," << y << ") in instance: " << instanceId;
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      frames_[i].first = x / tileWidth_;
      frames_[i].second = y / tileHeight_;
    }
  }


  unsigned int DicomPyramidInstance::GetFrameLocationX(size_t frame) const
  {
    assert(frame < frames_.size());
    return frames_[frame].first;
  }


  unsigned int DicomPyramidInstance::GetFrameLocationY(size_t frame) const
  {
    assert(frame < frames_.size());
    return frames_[frame].second;
  }
}
