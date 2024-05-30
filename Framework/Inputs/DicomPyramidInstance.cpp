/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2024 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2024 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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

#include "../DicomToolbox.h"
#include "../../Resources/Orthanc/Stone/DicomDatasetReader.h"
#include "../../Resources/Orthanc/Stone/FullOrthancDataset.h"

#include <Logging.h>
#include <OrthancException.h>
#include <SerializationToolbox.h>
#include <Toolbox.h>

#include <cassert>

#if !ORTHANC_FRAMEWORK_VERSION_IS_ABOVE(1, 9, 0)
#  include <json/writer.h>
#endif

#define SERIALIZED_METADATA  "4201"   // Was "4200" if versions <= 0.7 of this plugin


namespace OrthancWSI
{
  static const Orthanc::DicomTag DICOM_TAG_COLUMN_POSITION_IN_TOTAL_IMAGE_PIXEL_MATRIX(0x0048, 0x021e);
  static const Orthanc::DicomTag DICOM_TAG_PER_FRAME_FUNCTIONAL_GROUPS_SEQUENCE(0x5200, 0x9230);
  static const Orthanc::DicomTag DICOM_TAG_PLANE_POSITION_SLIDE_SEQUENCE(0x0048, 0x021a);
  static const Orthanc::DicomTag DICOM_TAG_ROW_POSITION_IN_TOTAL_IMAGE_PIXEL_MATRIX(0x0048, 0x021f);
  static const Orthanc::DicomTag DICOM_TAG_TOTAL_PIXEL_MATRIX_COLUMNS(0x0048, 0x0006);
  static const Orthanc::DicomTag DICOM_TAG_TOTAL_PIXEL_MATRIX_ROWS(0x0048, 0x0007);
  static const Orthanc::DicomTag DICOM_TAG_IMAGE_TYPE(0x0008, 0x0008);

  static ImageCompression DetectImageCompression(OrthancStone::IOrthancConnection& orthanc,
                                                 const std::string& instanceId)
  {
    using namespace OrthancStone;

    FullOrthancDataset dataset(orthanc, "/instances/" + instanceId + "/header");
    DicomDatasetReader header(dataset);

    std::string s = Orthanc::Toolbox::StripSpaces
      (header.GetMandatoryStringValue(Orthanc::DicomPath(Orthanc::DICOM_TAG_TRANSFER_SYNTAX_UID)));

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


  static void DetectPixelFormat(Orthanc::PixelFormat& format,
                                Orthanc::PhotometricInterpretation& photometric,
                                const OrthancStone::DicomDatasetReader& reader)
  {
    using namespace OrthancStone;

    std::string p = Orthanc::Toolbox::StripSpaces
      (reader.GetMandatoryStringValue(Orthanc::DicomPath(Orthanc::DICOM_TAG_PHOTOMETRIC_INTERPRETATION)));

    photometric = Orthanc::StringToPhotometricInterpretation(p.c_str());

    if (photometric == Orthanc::PhotometricInterpretation_Palette)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented,
                                      "Unsupported photometric interpretation: " + p);
    }

    unsigned int bitsStored, samplesPerPixel, tmp;

    if (!reader.GetUnsignedIntegerValue(bitsStored, Orthanc::DicomPath(Orthanc::DICOM_TAG_BITS_STORED)) ||
        !reader.GetUnsignedIntegerValue(samplesPerPixel, Orthanc::DicomPath(Orthanc::DICOM_TAG_SAMPLES_PER_PIXEL)) ||
        !reader.GetUnsignedIntegerValue(tmp, Orthanc::DicomPath(Orthanc::DICOM_TAG_PIXEL_REPRESENTATION)))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InexistentTag);
    }

    bool isSigned = (tmp != 0);

    if (bitsStored == 8 &&
        samplesPerPixel == 1 &&
        !isSigned)
    {
      format = Orthanc::PixelFormat_Grayscale8;
    }
    else if (bitsStored == 8 &&
             samplesPerPixel == 3 &&
             !isSigned)
    {
      format = Orthanc::PixelFormat_RGB24;
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "Unsupported pixel format");
    }
  }


  ImageCompression  DicomPyramidInstance::GetImageCompression(OrthancStone::IOrthancConnection& orthanc)
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

  
  void DicomPyramidInstance::Load(OrthancStone::IOrthancConnection&  orthanc,
                                  const std::string& instanceId)
  {
    using namespace OrthancStone;

    FullOrthancDataset dataset(orthanc, "/instances/" + instanceId + "/tags");
    DicomDatasetReader reader(dataset);

    if (reader.GetMandatoryStringValue(Orthanc::DicomPath(Orthanc::DICOM_TAG_SOP_CLASS_UID)) != VL_WHOLE_SLIDE_MICROSCOPY_IMAGE_STORAGE_IOD ||
        reader.GetMandatoryStringValue(Orthanc::DicomPath(Orthanc::DICOM_TAG_MODALITY)) != "SM")
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    hasCompression_ = false;
    DetectPixelFormat(format_, photometric_, reader);

    unsigned int tmp;
    if (!reader.GetUnsignedIntegerValue(tileWidth_, Orthanc::DicomPath(Orthanc::DICOM_TAG_COLUMNS)) ||
        !reader.GetUnsignedIntegerValue(tileHeight_, Orthanc::DicomPath(Orthanc::DICOM_TAG_ROWS)) ||
        !reader.GetUnsignedIntegerValue(totalWidth_, Orthanc::DicomPath(DICOM_TAG_TOTAL_PIXEL_MATRIX_COLUMNS)) ||
        !reader.GetUnsignedIntegerValue(totalHeight_, Orthanc::DicomPath(DICOM_TAG_TOTAL_PIXEL_MATRIX_ROWS)) ||
        !reader.GetUnsignedIntegerValue(tmp, Orthanc::DicomPath(Orthanc::DICOM_TAG_NUMBER_OF_FRAMES)))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    imageType_ = reader.GetStringValue(Orthanc::DicomPath(DICOM_TAG_IMAGE_TYPE), "");

    size_t countFrames;
    if (reader.GetDataset().GetSequenceSize(countFrames, Orthanc::DicomPath(DICOM_TAG_PER_FRAME_FUNCTIONAL_GROUPS_SEQUENCE)))
    {
      if (countFrames != tmp)
      {
        LOG(ERROR) << "Mismatch between the number of frames in instance: " << instanceId;
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      frames_.reserve(countFrames);

      for (size_t i = 0; i < countFrames; i++)
      {
        Orthanc::DicomPath pathX(DICOM_TAG_PER_FRAME_FUNCTIONAL_GROUPS_SEQUENCE, i,
                                 DICOM_TAG_PLANE_POSITION_SLIDE_SEQUENCE, 0,
                                 DICOM_TAG_COLUMN_POSITION_IN_TOTAL_IMAGE_PIXEL_MATRIX);

        Orthanc::DicomPath pathY(DICOM_TAG_PER_FRAME_FUNCTIONAL_GROUPS_SEQUENCE, i,
                                 DICOM_TAG_PLANE_POSITION_SLIDE_SEQUENCE, 0,
                                 DICOM_TAG_ROW_POSITION_IN_TOTAL_IMAGE_PIXEL_MATRIX);

        int xx, yy;
        if (!reader.GetIntegerValue(xx, pathX) ||
            !reader.GetIntegerValue(yy, pathY))
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_InexistentTag);
        }

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
        }
        else
        {
          frames_.push_back(std::make_pair(x / tileWidth_, y / tileHeight_));
        }
      }
    }
    else
    {
      // No "Per frame functional groups sequence" tag. Assume regular grid.
      frames_.resize(tmp);
      
      // Compute "ceiling(totalWidth_ / tileWidth_)
      unsigned int w = totalWidth_ / tileWidth_;
      if (totalWidth_ % tileWidth_ != 0)
      {
        w += 1;
      }

      unsigned int h = totalHeight_ / tileHeight_;
      if (totalHeight_ % tileHeight_ != 0)
      {
        h += 1;
      }

      if (w * h != tmp)
      {
        LOG(ERROR) << "Mismatch between the number of frames in instance: " << instanceId;
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      for (size_t i = 0; i < frames_.size(); i++)
      {
        frames_[i].first = i % w;
        frames_[i].second = i / w;
      }
    }
  }


  DicomPyramidInstance::DicomPyramidInstance(OrthancStone::IOrthancConnection&  orthanc,
                                             const std::string& instanceId,
                                             bool useCache) :
    instanceId_(instanceId),
    hasCompression_(false),
    compression_(ImageCompression_None)  // Dummy initialization for serialization
  {
    if (useCache)
    {
      try
      {
        // Try and deserialized the cached information about this instance
        std::string serialized;
        orthanc.RestApiGet(serialized, "/instances/" + instanceId + "/metadata/" + SERIALIZED_METADATA);
        Deserialize(serialized);
        return;  // Success
      }
      catch (Orthanc::OrthancException&)
      {
        // No cached information yet
      }
    }

    // Compute information about this instance from scratch
    Load(orthanc, instanceId);

    if (useCache)
    {
      // Serialize the computed information and cache it as a metadata
      std::string serialized, tmp;
      Serialize(serialized);
      orthanc.RestApiPut(tmp, "/instances/" + instanceId + "/metadata/" + SERIALIZED_METADATA, serialized);
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



  static const char* const HAS_COMPRESSION = "HasCompression";
  static const char* const IMAGE_COMPRESSION = "ImageCompression";
  static const char* const PIXEL_FORMAT = "PixelFormat";
  static const char* const FRAMES = "Frames";
  static const char* const TILE_WIDTH = "TileWidth";
  static const char* const TILE_HEIGHT = "TileHeight";
  static const char* const TOTAL_WIDTH = "TotalWidth";
  static const char* const TOTAL_HEIGHT = "TotalHeight";
  static const char* const PHOTOMETRIC_INTERPRETATION = "PhotometricInterpretation";
  static const char* const IMAGE_TYPE = "ImageType";
  
  
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

    Json::Value content = Json::objectValue;
    content[FRAMES] = frames;

    // "instanceId_" is set by the constructor
    
    content[HAS_COMPRESSION] = hasCompression_;
    content[IMAGE_COMPRESSION] = static_cast<int>(compression_);
    content[PIXEL_FORMAT] = static_cast<int>(format_);
    content[TILE_WIDTH] = tileWidth_;
    content[TILE_HEIGHT] = tileHeight_;
    content[TOTAL_WIDTH] = totalWidth_;
    content[TOTAL_HEIGHT] = totalHeight_;
    content[PHOTOMETRIC_INTERPRETATION] = Orthanc::EnumerationToString(photometric_);
    content[IMAGE_TYPE] = imageType_;

#if ORTHANC_FRAMEWORK_VERSION_IS_ABOVE(1, 9, 0)
    Orthanc::Toolbox::WriteFastJson(result, content);
#else
    Json::FastWriter writer;
    result = writer.write(content);
#endif
  }


  void DicomPyramidInstance::Deserialize(const std::string& s)
  {
    Json::Value content;
    OrthancStone::IOrthancConnection::ParseJson(content, s);

    if (content.type() != Json::objectValue ||
        !content.isMember(FRAMES) ||
        content[FRAMES].type() != Json::arrayValue)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    hasCompression_ = Orthanc::SerializationToolbox::ReadBoolean(content, HAS_COMPRESSION);
    compression_ = static_cast<ImageCompression>(Orthanc::SerializationToolbox::ReadInteger(content, IMAGE_COMPRESSION));
    format_ = static_cast<Orthanc::PixelFormat>(Orthanc::SerializationToolbox::ReadInteger(content, PIXEL_FORMAT));
    tileWidth_ = Orthanc::SerializationToolbox::ReadUnsignedInteger(content, TILE_WIDTH);
    tileHeight_ = Orthanc::SerializationToolbox::ReadUnsignedInteger(content, TILE_HEIGHT);
    totalWidth_ = Orthanc::SerializationToolbox::ReadUnsignedInteger(content, TOTAL_WIDTH);
    totalHeight_ = Orthanc::SerializationToolbox::ReadUnsignedInteger(content, TOTAL_HEIGHT);

    std::string p = Orthanc::SerializationToolbox::ReadString(content, PHOTOMETRIC_INTERPRETATION);
    photometric_ = Orthanc::StringToPhotometricInterpretation(p.c_str());

    imageType_ = Orthanc::SerializationToolbox::ReadString(content, IMAGE_TYPE);
    
    const Json::Value f = content[FRAMES];
    frames_.resize(f.size());

    for (Json::Value::ArrayIndex i = 0; i < f.size(); i++)
    {
      if (f[i].type() != Json::arrayValue ||
          f[i].size() != 2 ||
          f[i][0].type() != Json::intValue ||
          f[i][1].type() != Json::intValue ||
          f[i][0].asInt() < 0 ||
          f[i][1].asInt() < 0)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      frames_[i].first = f[i][0].asInt();
      frames_[i].second = f[i][1].asInt();
    }
  }
}
