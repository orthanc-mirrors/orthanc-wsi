/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2024 Osimis S.A., Belgium
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
  static ImageCompression Convert(Orthanc::MimeType type)
  {
    switch (type)
    {
      case Orthanc::MimeType_Png:
        return ImageCompression_Png;

      case Orthanc::MimeType_Jpeg:
        return ImageCompression_Jpeg;

      case Orthanc::MimeType_Jpeg2000:
        return ImageCompression_Jpeg2000;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


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
                               Orthanc::MimeType encoding)
  {
    ImageToolbox::EncodeTile(encoded, decoded, Convert(encoding), 90 /* only used for JPEG */);
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
                       Orthanc::MimeType encoding)
  {
    if ((compression_ == ImageCompression_Jpeg && encoding == Orthanc::MimeType_Jpeg) ||
        (compression_ == ImageCompression_Jpeg2000 && encoding == Orthanc::MimeType_Jpeg2000))
    {
      // No transcoding is needed, the tile can be served as such
      OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, tile_.c_str(),
                                tile_.size(), Orthanc::EnumerationToString(encoding));
    }
    else
    {
      std::string transcoded;

      {
        // The semaphore is used to throttle the number of simultaneous computations
        Orthanc::Semaphore::Locker locker(*transcoderSemaphore_);

        std::unique_ptr<Orthanc::ImageAccessor> decoded(DecodeInternal());
        EncodeInternal(transcoded, *decoded, encoding);
      }

      OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, transcoded.c_str(),
                                transcoded.size(), Orthanc::EnumerationToString(encoding));
    }
  }


  Orthanc::ImageAccessor* RawTile::Decode()
  {
    Orthanc::Semaphore::Locker locker(*transcoderSemaphore_);
    return DecodeInternal();
  }


  void RawTile::Encode(std::string& encoded,
                       const Orthanc::ImageAccessor& decoded,
                       Orthanc::MimeType encoding)
  {
    Orthanc::Semaphore::Locker locker(*transcoderSemaphore_);
    EncodeInternal(encoded, decoded, encoding);
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
