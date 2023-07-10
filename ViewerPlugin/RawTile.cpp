/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2021-2023 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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
#include "RawTile.h"

#include "../Framework/ImageToolbox.h"
#include "../Framework/Jpeg2000Reader.h"
#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Compatibility.h>  // For std::unique_ptr
#include <Images/JpegReader.h>
#include <Images/JpegWriter.h>
#include <Images/PngWriter.h>
#include <MultiThreading/Semaphore.h>
#include <OrthancException.h>


static std::unique_ptr<Orthanc::Semaphore>  transcoderSemaphore_;

namespace OrthancWSI
{
  Orthanc::ImageAccessor* RawTile::DecodeInternal()
  {
    switch (compression_)
    {
      case ImageCompression_Jpeg:
      {
        std::unique_ptr<Orthanc::JpegReader> decoded(new Orthanc::JpegReader);
        decoded->ReadFromMemory(tile_);
        return decoded.release();
      }

      case ImageCompression_Jpeg2000:
      {
        std::unique_ptr<Jpeg2000Reader> decoded(new Jpeg2000Reader);
        decoded->ReadFromMemory(tile_);

        if (photometric_ == Orthanc::PhotometricInterpretation_YBR_ICT)
        {
          ImageToolbox::ConvertJpegYCbCrToRgb(*decoded);
        }

        return decoded.release();
      }

      case ImageCompression_None:
      {
        unsigned int bpp = Orthanc::GetBytesPerPixel(format_);
        if (bpp * tileWidth_ * tileHeight_ != tile_.size())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
        }

        std::unique_ptr<Orthanc::ImageAccessor> decoded(new Orthanc::ImageAccessor);
        decoded->AssignReadOnly(format_, tileWidth_, tileHeight_, bpp * tileWidth_, tile_.c_str());

        return decoded.release();
      }

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
    }
  }


  void RawTile::EncodeInternal(std::string& encoded,
                               const Orthanc::ImageAccessor& decoded,
                               Orthanc::MimeType transcodingType)
  {
    switch (transcodingType)
    {
      case Orthanc::MimeType_Png:
      {
        Orthanc::PngWriter writer;
        Orthanc::IImageWriter::WriteToMemory(writer, encoded, decoded);
        break;
      }

      case Orthanc::MimeType_Jpeg:
      {
        Orthanc::JpegWriter writer;
        Orthanc::IImageWriter::WriteToMemory(writer, encoded, decoded);
        break;
      }

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
    }
  }


  RawTile::RawTile(ITiledPyramid& pyramid,
                   unsigned int level,
                   unsigned int tileX,
                   unsigned int tileY) :
    format_(pyramid.GetPixelFormat()),
    tileWidth_(pyramid.GetTileWidth(level)),
    tileHeight_(pyramid.GetTileHeight(level)),
    photometric_(pyramid.GetPhotometricInterpretation())
  {
    if (!pyramid.ReadRawTile(tile_, compression_, level, tileX, tileY))
    {
      // Handling of missing tile (for sparse tiling): TODO parameter?
      // AnswerSparseTile(output, tileWidth, tileHeight); return;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }
  }


  void RawTile::Answer(OrthancPluginRestOutput* output,
                       Orthanc::MimeType transcodingType)
  {
    if (compression_ == ImageCompression_Jpeg)
    {
      // The tile is already a JPEG image. In such a case, we can
      // serve it as such, because any Web browser can handle JPEG.
      OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, tile_.c_str(),
                                tile_.size(), Orthanc::EnumerationToString(Orthanc::MimeType_Jpeg));
    }
    else
    {
      // This is a lossless frame (coming from a JPEG2000 or an
      // uncompressed DICOM instance), which is not a DICOM-JPEG
      // instance. We need to decompress the raw tile, then transcode
      // it to the PNG/JPEG, depending on the "transcodingType".

      std::string transcoded;

      {
        // The semaphore is used to throttle the number of simultaneous computations
        Orthanc::Semaphore::Locker locker(*transcoderSemaphore_);

        std::unique_ptr<Orthanc::ImageAccessor> decoded(DecodeInternal());
        EncodeInternal(transcoded, *decoded, transcodingType);
      }

      OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, transcoded.c_str(),
                                transcoded.size(), Orthanc::EnumerationToString(transcodingType));
    }
  }


  Orthanc::ImageAccessor* RawTile::Decode()
  {
    Orthanc::Semaphore::Locker locker(*transcoderSemaphore_);
    return DecodeInternal();
  }


  void RawTile::Encode(std::string& encoded,
                       const Orthanc::ImageAccessor& decoded,
                       Orthanc::MimeType transcodingType)
  {
    Orthanc::Semaphore::Locker locker(*transcoderSemaphore_);
    EncodeInternal(encoded, decoded, transcodingType);
  }


  void RawTile::InitializeTranscoderSemaphore(unsigned int maxThreads)
  {
    transcoderSemaphore_.reset(new Orthanc::Semaphore(maxThreads));
  }


  void RawTile::FinalizeTranscoderSemaphore()
  {
    transcoderSemaphore_.reset(NULL);
  }
}
