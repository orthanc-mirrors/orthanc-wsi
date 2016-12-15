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
#include "DicomPyramidWriter.h"

#include "../DicomToolbox.h"

#include "../../Resources/Orthanc/Core/Logging.h"
#include "../../Resources/Orthanc/Core/OrthancException.h"
#include "../../Resources/Orthanc/OrthancServer/FromDcmtkBridge.h"

#include <dcmtk/dcmdata/dcdeftag.h>
#include <boost/lexical_cast.hpp>

namespace OrthancWSI
{
  void DicomPyramidWriter::FlushInternal(MultiframeDicomWriter& writer,
                                         bool force)
  {
    if (writer.GetFramesCount() > 0 &&
        writer.GetSize() > 0 &&
        (force || (maxSize_ != 0 && writer.GetSize() >= maxSize_)))
    {
      countInstances_ += 1;

      std::string dicom;
      writer.Flush(dicom, countInstances_);
      target_.Write(dicom);
    }
  }


  DcmItem* DicomPyramidWriter::CreateFunctionalGroup(unsigned int frame,
                                                     unsigned int x, 
                                                     unsigned int y,
                                                     unsigned int totalWidth,
                                                     unsigned int totalHeight,
                                                     float physicalZ) const
  {
    float physicalX, physicalY;
    volume_.GetLocation(physicalX, physicalY, x, y, totalWidth, totalHeight);

    std::string tmpX = boost::lexical_cast<std::string>(physicalX);
    std::string tmpY = boost::lexical_cast<std::string>(physicalY);
    std::string tmpZ = boost::lexical_cast<std::string>(physicalZ);
    
    // NB: Method DcmItem::putAndInsertUint32Array() should be used at
    // this point, but it is missing in DCMTK 3.6.0
    std::string index = (boost::lexical_cast<std::string>(x / GetTileWidth() + 1) + "\\" + 
                         boost::lexical_cast<std::string>(y / GetTileHeight() + 1));

    std::auto_ptr<DcmItem> dimension(new DcmItem);
    if (!dimension->putAndInsertString(DCM_DimensionIndexValues, index.c_str()).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }

    // From Supp 145: The column position of the top left pixel of the
    // Total Pixel Matrix is 1. The row position of the top left pixel
    // of the Total Pixel Matrix is 1.
    std::auto_ptr<DcmItem> position(new DcmItem);
    if (!position->putAndInsertSint32(DCM_ColumnPositionInTotalImagePixelMatrix, x + 1).good() ||
        !position->putAndInsertSint32(DCM_RowPositionInTotalImagePixelMatrix, y + 1).good() ||
        !position->putAndInsertString(DCM_XOffsetInSlideCoordinateSystem, tmpX.c_str()).good() ||
        !position->putAndInsertString(DCM_YOffsetInSlideCoordinateSystem, tmpY.c_str()).good() ||
        !position->putAndInsertString(DCM_ZOffsetInSlideCoordinateSystem, tmpZ.c_str()).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }


    std::auto_ptr<DcmSequenceOfItems> sequencePosition(new DcmSequenceOfItems(DCM_PlanePositionSlideSequence));
    if (!sequencePosition->insert(position.release(), false, false).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }

    std::auto_ptr<DcmSequenceOfItems> sequenceDimension(new DcmSequenceOfItems(DCM_FrameContentSequence));
    if (!sequenceDimension->insert(dimension.release(), false, false).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }

    std::auto_ptr<DcmItem> item(new DcmItem);
    if (!item->insert(sequencePosition.release(), false, false).good() ||
        !item->insert(sequenceDimension.release(), false, false).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }  

    return item.release();
  }


  void DicomPyramidWriter::WriteRawTileInternal(const std::string& tile,
                                                const Level& level,
                                                unsigned int x,
                                                unsigned int y)
  {
    if (x >= level.countTilesX_ ||
        y >= level.countTilesY_)
    {
      LOG(ERROR) << "Tile index out of range: " << x << "," << y
                 << " (max: " << level.countTilesX_ << "," << level.countTilesY_ << ")";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    const unsigned int z = level.z_;

    {
      boost::mutex::scoped_lock lock(mutex_);
      
      if (z >= writers_.size())
      {
        writers_.resize(z + 1);
      }

      MultiframeDicomWriter* writer = writers_[z];

      if (writer == NULL)
      {
        writer = new MultiframeDicomWriter
          (dataset_, GetImageCompression(), GetPixelFormat(), level.width_, level.height_, 
           GetTileWidth(), GetTileHeight());
        writers_[z] = writer;
      }

      std::auto_ptr<DcmItem> functionalGroup(CreateFunctionalGroup(writer->GetFramesCount() + 1,
                                                                   x * GetTileWidth(), 
                                                                   y * GetTileHeight(), 
                                                                   writer->GetTotalWidth(),
                                                                   writer->GetTotalHeight(),
                                                                   0.0f /* TODO Z-plane */));

      writer->AddFrame(tile, functionalGroup.release());
      FlushInternal(*writer, false);

      countTiles_ ++;
    }
  }


  DicomPyramidWriter::DicomPyramidWriter(IFileTarget&  target,
                                         const DcmDataset& dataset,
                                         Orthanc::PixelFormat pixelFormat,
                                         ImageCompression compression,
                                         unsigned int tileWidth,
                                         unsigned int tileHeight,
                                         size_t maxSize,   // If "0", no automatic flushing
                                         const ImagedVolumeParameters&  volume) :
    PyramidWriterBase(pixelFormat, compression, tileWidth, tileHeight),
    target_(target),
    dataset_(dataset),
    maxSize_(maxSize),
    countTiles_(0),
    countInstances_(0),
    volume_(volume)
  {
  }


  DicomPyramidWriter::~DicomPyramidWriter()
  {
    LOG(WARNING) << "Closing the DICOM pyramid (" << countTiles_ << " tiles were written)";

    for (size_t i = 0; i < writers_.size(); i++)
    {
      if (writers_[i] != NULL)
      {
        try
        {
          FlushInternal(*writers_[i], true);
        }
        catch (Orthanc::OrthancException&)
        {
          LOG(ERROR) << "Cannot push the pending tiles to the DICOM pyramid while finalizing";
        }

        delete writers_[i];
      }
    }
  }


  void DicomPyramidWriter::Flush()
  {
    boost::mutex::scoped_lock lock(mutex_);
      
    for (size_t i = 0; i < writers_.size(); i++)
    {
      FlushInternal(*writers_[i], true);
    }
  }
}
