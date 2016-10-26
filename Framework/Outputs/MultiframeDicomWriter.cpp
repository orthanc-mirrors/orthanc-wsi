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


#include "MultiframeDicomWriter.h"

#include "../Orthanc/Core/OrthancException.h"
#include "../Orthanc/Core/Logging.h"
#include "../DicomToolbox.h"

#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcostrmb.h>
#include <dcmtk/dcmdata/dcpxitem.h>
#include <dcmtk/dcmdata/dcpixel.h>
#include <dcmtk/dcmdata/dcvrat.h>

#include <boost/lexical_cast.hpp>

namespace OrthancWSI
{
  static void SaveDicomToMemory(std::string& target,
                                DcmFileFormat& dicom,
                                E_TransferSyntax transferSyntax)
  {
    dicom.validateMetaInfo(transferSyntax);
    dicom.removeInvalidGroups();

    E_EncodingType encodingType = /*opt_sequenceType*/ EET_ExplicitLength;

    // Create a memory buffer with the proper size
    {
      const uint32_t estimatedSize = dicom.calcElementLength(transferSyntax, encodingType);  // (*)
      target.resize(estimatedSize);
    }

    DcmOutputBufferStream ob(&target[0], target.size());

    // Fill the memory buffer with the meta-header and the dataset
    dicom.transferInit();
    OFCondition c = dicom.write(ob, transferSyntax, encodingType, NULL,
                                /*opt_groupLength*/ EGL_recalcGL,
                                /*opt_paddingType*/ EPD_withoutPadding);
    dicom.transferEnd();

    if (!c.good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }

    // The DICOM file is successfully written, truncate the target
    // buffer if its size was overestimated by (*)
    ob.flush();

    size_t effectiveSize = static_cast<size_t>(ob.tell());
    if (effectiveSize < target.size())
    {
      target.resize(effectiveSize);
    }
  }


  void MultiframeDicomWriter::ResetImage()
  {
    perFrameFunctionalGroups_.reset(new DcmSequenceOfItems(DCM_PerFrameFunctionalGroupsSequence));

    if (compression_ != ImageCompression_None)
    {
      compressedPixelSequence_.reset(new DcmPixelSequence(DcmTag(DCM_PixelData, EVR_OB)));

      offsetTable_ = new DcmPixelItem(DCM_Item, EVR_OB);
      compressedPixelSequence_->insert(offsetTable_);
      offsetList_.reset(new DcmOffsetList);
    }

    writtenSize_ = 0;
    framesCount_ = 0;
  }


  void MultiframeDicomWriter::InjectUncompressedPixelData(DcmFileFormat& dicom)
  {
    static const size_t GIGABYTE = 1024 * 1024 * 1024;

    std::string pixelData;
    uncompressedPixelData_.Flatten(pixelData);

    // Prevent the creation of too large DICOM files
    // (uncompressed DICOM files are limited to 2GB)
    if (pixelData.size() > GIGABYTE)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotEnoughMemory);
    }

    std::auto_ptr<DcmPixelData> pixels(new DcmPixelData(DCM_PixelData));

    uint8_t* target = NULL;
    if (!pixels->createUint8Array(pixelData.size(), target).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }

    if (pixelData.size() > 0)
    {
      if (target == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }

      // TODO Avoid this memcpy()
      memcpy(target, pixelData.c_str(), pixelData.size());
    }

    if (!dicom.getDataset()->insert(pixels.release(), false, false).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
  }


  MultiframeDicomWriter::MultiframeDicomWriter(const DcmDataset& dataset,
                                               ImageCompression compression,
                                               Orthanc::PixelFormat pixelFormat,
                                               unsigned int width,
                                               unsigned int height,
                                               unsigned int tileWidth,
                                               unsigned int tileHeight) :
    compression_(compression),
    width_(width),
    height_(height)
  {
    switch (compression)
    {
      case ImageCompression_None:
        transferSyntax_ = EXS_LittleEndianImplicit;
        break;

      case ImageCompression_Jpeg:
        // Default transfer syntax for lossy JPEG 8bit compression
        transferSyntax_ = EXS_JPEGProcess1TransferSyntax;
        break;

      case ImageCompression_Jpeg2000:
        // JPEG2000 compression (lossless only)
        transferSyntax_ = EXS_JPEG2000LosslessOnly;
        break;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    if (!sharedTags_.copyFrom(dataset).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }

    // We only take care of grayscale or RGB images in 8bpp
    DicomToolbox::SetUint32Tag(sharedTags_, DCM_TotalPixelMatrixColumns, width);
    DicomToolbox::SetUint32Tag(sharedTags_, DCM_TotalPixelMatrixRows, height);
    DicomToolbox::SetUint16Tag(sharedTags_, DCM_PlanarConfiguration, 0);  // Interleaved RGB values
    DicomToolbox::SetUint16Tag(sharedTags_, DCM_Columns, tileWidth);
    DicomToolbox::SetUint16Tag(sharedTags_, DCM_Rows, tileHeight);
    DicomToolbox::SetUint16Tag(sharedTags_, DCM_BitsAllocated, 8);
    DicomToolbox::SetUint16Tag(sharedTags_, DCM_BitsStored, 8);
    DicomToolbox::SetUint16Tag(sharedTags_, DCM_HighBit, 7);
    DicomToolbox::SetUint16Tag(sharedTags_, DCM_PixelRepresentation, 0);   // Unsigned values

    switch (pixelFormat)
    {
      case Orthanc::PixelFormat_RGB24:
        uncompressedFrameSize_ = 3 * tileWidth * tileHeight;
        DicomToolbox::SetUint16Tag(sharedTags_, DCM_SamplesPerPixel, 3);

        if (compression_ == ImageCompression_Jpeg)
        {
          DicomToolbox::SetStringTag(sharedTags_, DCM_PhotometricInterpretation, "YBR_FULL_422");
        }
        else
        {
          DicomToolbox::SetStringTag(sharedTags_, DCM_PhotometricInterpretation, "RGB");
        }

        break;

      case Orthanc::PixelFormat_Grayscale8:
        uncompressedFrameSize_ = tileWidth * tileHeight;
        DicomToolbox::SetUint16Tag(sharedTags_, DCM_SamplesPerPixel, 1);
        DicomToolbox::SetStringTag(sharedTags_, DCM_PhotometricInterpretation, "MONOCHROME2");
        break;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    ResetImage();
  }


  void MultiframeDicomWriter::AddFrame(const std::string& frame,
                                       DcmItem* functionalGroup)   // This takes the ownership
  {
    // Free the functional group on error
    std::auto_ptr<DcmItem> functionalGroupRaii(functionalGroup);

    if (compression_ == ImageCompression_None)
    {
      if (frame.size() != uncompressedFrameSize_)
      {
        LOG(ERROR) << "An uncompressed frame has not the proper size: " 
                   << frame.size() << " instead of " << uncompressedFrameSize_;
        throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageSize);
      }

      uncompressedPixelData_.AddChunk(frame);
    }
    else
    {
      uint8_t* bytes = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(frame.c_str()));

      compressedPixelSequence_->storeCompressedFrame(*offsetList_, bytes, frame.size(), 
                                                     0 /* unlimited fragment size */);
    }

    if (functionalGroup != NULL)
    {
      perFrameFunctionalGroups_->insert(functionalGroupRaii.release());
    }
    else
    {
      perFrameFunctionalGroups_->insert(new DcmItem);
    }

    writtenSize_ += frame.size();
    framesCount_ += 1;
  }


  void MultiframeDicomWriter::Flush(std::string& target,
                                    unsigned int instanceNumber)
  {
    if (instanceNumber <= 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    std::string tmp = boost::lexical_cast<std::string>(instanceNumber);

    std::auto_ptr<DcmFileFormat> dicom(new DcmFileFormat);

    char uid[100];
    dcmGenerateUniqueIdentifier(uid, SITE_INSTANCE_UID_ROOT);

    if (!dicom->getDataset()->copyFrom(sharedTags_).good() ||
        !dicom->getDataset()->insert(perFrameFunctionalGroups_.release(), false, false).good() ||
        !dicom->getDataset()->putAndInsertString(DCM_SOPInstanceUID, uid).good() ||
        !dicom->getDataset()->putAndInsertString(DCM_InstanceNumber, tmp.c_str()).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }

    DicomToolbox::SetStringTag(*dicom->getDataset(), DCM_NumberOfFrames, boost::lexical_cast<std::string>(framesCount_));

    switch (compression_)
    {
      case ImageCompression_None:
        InjectUncompressedPixelData(*dicom);
        break;

      case ImageCompression_Jpeg:
      case ImageCompression_Jpeg2000:
        offsetTable_->createOffsetTable(*offsetList_);
        dicom->getDataset()->insert(compressedPixelSequence_.release());
        break;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    ResetImage();

    SaveDicomToMemory(target, *dicom, transferSyntax_);
  }
}