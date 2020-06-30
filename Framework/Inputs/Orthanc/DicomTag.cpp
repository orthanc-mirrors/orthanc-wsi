/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2020 Osimis S.A., Belgium
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


#include "DicomTag.h"

#include <OrthancException.h>

namespace OrthancPlugins
{
  const char* DicomTag::GetName() const
  {
    if (*this == DICOM_TAG_BITS_STORED)
    {
      return "BitsStored";
    }
    else if (*this == DICOM_TAG_COLUMN_POSITION_IN_TOTAL_IMAGE_PIXEL_MATRIX)
    {
      return "ColumnPositionInTotalImagePixelMatrix";
    }
    else if (*this == DICOM_TAG_COLUMNS)
    {
      return "Columns";
    }
    else if (*this == DICOM_TAG_MODALITY)
    {
      return "Modality";
    }
    else if (*this == DICOM_TAG_NUMBER_OF_FRAMES)
    {
      return "NumberOfFrames";
    }
    else if (*this == DICOM_TAG_PER_FRAME_FUNCTIONAL_GROUPS_SEQUENCE)
    {
      return "PerFrameFunctionalGroupsSequence";
    }
    else if (*this == DICOM_TAG_PHOTOMETRIC_INTERPRETATION)
    {
      return "PhotometricInterpretation";
    }
    else if (*this == DICOM_TAG_PIXEL_REPRESENTATION)
    {
      return "PixelRepresentation";
    }
    else if (*this == DICOM_TAG_PLANE_POSITION_SLIDE_SEQUENCE)
    {
      return "PlanePositionSlideSequence";
    }
    else if (*this == DICOM_TAG_ROW_POSITION_IN_TOTAL_IMAGE_PIXEL_MATRIX)
    {
      return "RowPositionInTotalImagePixelMatrix";
    }
    else if (*this == DICOM_TAG_ROWS)
    {
      return "Rows";
    }
    else if (*this == DICOM_TAG_SOP_CLASS_UID)
    {
      return "SOPClassUID";
    }
    else if (*this == DICOM_TAG_SAMPLES_PER_PIXEL)
    {
      return "SamplesPerPixel";
    }
    else if (*this == DICOM_TAG_TOTAL_PIXEL_MATRIX_COLUMNS)
    {
      return "TotalPixelMatrixColumns";
    }
    else if (*this == DICOM_TAG_TOTAL_PIXEL_MATRIX_ROWS)
    {
      return "TotalPixelMatrixRows";
    }
    else if (*this == DICOM_TAG_TRANSFER_SYNTAX_UID)
    {
      return "TransferSyntaxUID";
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
    }
  }


  std::string DicomTag::FormatHexadecimal() const
  {
    char buf[16];
    sprintf(buf, "(%04x,%04x)", group_, element_);
    return buf;
  }
}
