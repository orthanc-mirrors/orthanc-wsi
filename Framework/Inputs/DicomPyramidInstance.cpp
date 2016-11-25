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
#include "../../Resources/Orthanc/Plugins/Samples/Common/DicomDatasetReader.h"
#include "../../Resources/Orthanc/Plugins/Samples/Common/FullOrthancDataset.h"
#include "../DicomToolbox.h"

#include <cassert>
#include <json/writer.h>

namespace OrthancWSI
{
  static ImageCompression DetectImageCompression(OrthancPlugins::IOrthancConnection& orthanc,
                                                 const std::string& instanceId)
  {
    using namespace OrthancPlugins;

    DicomDatasetReader header(new FullOrthancDataset
                              (orthanc, "/instances/" + instanceId + "/header"));

    std::string s = Orthanc::Toolbox::StripSpaces
      (header.GetMandatoryStringValue(DICOM_TAG_TRANSFER_SYNTAX_UID));

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


  static Orthanc::PixelFormat DetectPixelFormat(OrthancPlugins::DicomDatasetReader& reader)
  {
    using namespace OrthancPlugins;

    std::string photometric = Orthanc::Toolbox::StripSpaces
      (reader.GetMandatoryStringValue(DICOM_TAG_PHOTOMETRIC_INTERPRETATION));

    if (photometric == "PALETTE")
    {
      LOG(ERROR) << "Unsupported photometric interpretation: " << photometric;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
    }

    unsigned int bitsStored = reader.GetUnsignedIntegerValue(DICOM_TAG_BITS_STORED);
    unsigned int samplesPerPixel = reader.GetUnsignedIntegerValue(DICOM_TAG_SAMPLES_PER_PIXEL);
    bool isSigned = (reader.GetUnsignedIntegerValue(DICOM_TAG_PIXEL_REPRESENTATION) != 0);

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


  ImageCompression  DicomPyramidInstance::GetImageCompression(OrthancPlugins::IOrthancConnection& orthanc)
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
      compression_ = DetectImageCompression(orthanc, instanceId_);
      hasCompression_ = true;
    }

    return compression_;
  }

  
  DicomPyramidInstance::DicomPyramidInstance(OrthancPlugins::IOrthancConnection&  orthanc,
                                             const std::string& instanceId) :
    instanceId_(instanceId),
    hasCompression_(false)
  {
    using namespace OrthancPlugins;

    DicomDatasetReader reader(new FullOrthancDataset(orthanc, "/instances/" + instanceId + "/tags"));

    if (reader.GetMandatoryStringValue(DICOM_TAG_SOP_CLASS_UID) != "1.2.840.10008.5.1.4.1.1.77.1.6" ||
        reader.GetMandatoryStringValue(DICOM_TAG_MODALITY) != "SM")
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    format_ = DetectPixelFormat(reader);
    tileWidth_ = reader.GetUnsignedIntegerValue(DICOM_TAG_COLUMNS);
    tileHeight_ = reader.GetUnsignedIntegerValue(DICOM_TAG_ROWS);
    totalWidth_ = reader.GetUnsignedIntegerValue(DICOM_TAG_TOTAL_PIXEL_MATRIX_COLUMNS);
    totalHeight_ = reader.GetUnsignedIntegerValue(DICOM_TAG_TOTAL_PIXEL_MATRIX_ROWS);

    size_t countFrames;
    if (!reader.GetDataset().GetSequenceSize(countFrames, DICOM_TAG_PER_FRAME_FUNCTIONAL_GROUPS_SEQUENCE))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    if (countFrames != reader.GetUnsignedIntegerValue(DICOM_TAG_NUMBER_OF_FRAMES))
    {
      LOG(ERROR) << "Mismatch between the number of frames in instance: " << instanceId;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    frames_.resize(countFrames);

    for (size_t i = 0; i < countFrames; i++)
    {
      int xx = reader.GetIntegerValue(DicomPath(DICOM_TAG_PER_FRAME_FUNCTIONAL_GROUPS_SEQUENCE, i,
                                                DICOM_TAG_PLANE_POSITION_SLIDE_SEQUENCE, 0,
                                                DICOM_TAG_COLUMN_POSITION_IN_TOTAL_IMAGE_PIXEL_MATRIX));

      int yy = reader.GetIntegerValue(DicomPath(DICOM_TAG_PER_FRAME_FUNCTIONAL_GROUPS_SEQUENCE, i,
                                                DICOM_TAG_PLANE_POSITION_SLIDE_SEQUENCE, 0,
                                                DICOM_TAG_ROW_POSITION_IN_TOTAL_IMAGE_PIXEL_MATRIX));

      // "-1", because coordinates are shifted by 1 in DICOM
      xx -= 1;
      yy -= 1;

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

  
  void DicomPyramidInstance::Serialize(std::string& result) const
  {
    Json::Value frames = Json::arrayValue;
    for (size_t i = 0; i < frames_.size(); i++)
    {
      Json::Value frame = Json::arrayValue;
      frame.append(frames_[i].first);
      frame.append(frames_[i].second);

      frames.append(frame);
    }

    Json::Value value;
    value["PixelFormat"] = Orthanc::EnumerationToString(format_);
    value["TileHeight"] = tileHeight_;
    value["TileWidth"] = tileWidth_;
    value["TotalHeight"] = totalHeight_;
    value["TotalWidth"] = totalWidth_;    
    value["Frames"] = frames;

    Json::FastWriter writer;
    result = writer.write(value);
  }
}
