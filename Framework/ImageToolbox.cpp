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


#include "PrecompiledHeadersWSI.h"
#include "ImageToolbox.h"

#include "Jpeg2000Reader.h"
#include "Jpeg2000Writer.h"

#include <Compatibility.h>  // For std::unique_ptr
#include <OrthancException.h>
#include <Images/ImageProcessing.h>
#include <Images/PngReader.h>
#include <Images/PngWriter.h>
#include <Images/JpegReader.h>
#include <Images/JpegWriter.h>
#include <Logging.h>

#include <string.h>
#include <memory>


namespace OrthancWSI
{
  namespace ImageToolbox
  {
    Orthanc::ImageAccessor* Allocate(Orthanc::PixelFormat format,
                                     unsigned int width,
                                     unsigned int height)
    {
      return new Orthanc::Image(format, width, height, false);
    }


    void Embed(Orthanc::ImageAccessor& target,
               const Orthanc::ImageAccessor& source,
               unsigned int x,
               unsigned int y)
    {
      if (target.GetFormat() != source.GetFormat())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageFormat);
      }

      if (x >= target.GetWidth() ||
          y >= target.GetHeight())
      {
        return;
      }

      unsigned int h = std::min(source.GetHeight(), target.GetHeight() - y);
      unsigned int w = std::min(source.GetWidth(), target.GetWidth() - x);

      Orthanc::ImageAccessor targetRegion, sourceRegion;
      target.GetRegion(targetRegion, x, y, w, h);
      source.GetRegion(sourceRegion, 0, 0, w, h);
      
      Orthanc::ImageProcessing::Copy(targetRegion, sourceRegion);
    }



    void Set(Orthanc::ImageAccessor& image,
             uint8_t r,
             uint8_t g,
             uint8_t b)
    {
      if (image.GetWidth() == 0 ||
          image.GetHeight() == 0)
      {
        return;
      }

      uint8_t grayscale = (2126 * static_cast<uint16_t>(r) + 
                           7152 * static_cast<uint16_t>(g) +
                           0722 * static_cast<uint16_t>(b)) / 10000;

      const unsigned int width = image.GetWidth();
      const unsigned int height = image.GetHeight();

      switch (image.GetFormat())
      {
        case Orthanc::PixelFormat_Grayscale8:
        {
          for (unsigned int y = 0; y < height; y++)
          {
            memset(image.GetRow(y), grayscale, width);
          }

          break;
        }

        case Orthanc::PixelFormat_RGB24:
        {
          for (unsigned int y = 0; y < height; y++)
          {
            uint8_t* p = reinterpret_cast<uint8_t*>(image.GetRow(y));
            for (unsigned int x = 0; x < width; x++, p += 3)
            {
              p[0] = r;
              p[1] = g;
              p[2] = b;
            }
          }

          break;
        }

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
      }
    }


    Orthanc::ImageAccessor* DecodeTile(const std::string& source,
                                       ImageCompression compression)
    {
      switch (compression)
      {
        case ImageCompression_Png:
        {
          std::unique_ptr<Orthanc::PngReader> reader(new Orthanc::PngReader);
          reader->ReadFromMemory(source);
          return reader.release();
        }

        case ImageCompression_Jpeg:
        {
          std::unique_ptr<Orthanc::JpegReader> reader(new Orthanc::JpegReader);
          reader->ReadFromMemory(source);
          return reader.release();
        }

        case ImageCompression_Jpeg2000:
        {
          std::unique_ptr<Jpeg2000Reader> reader(new Jpeg2000Reader);
          reader->ReadFromMemory(source);
          return reader.release();
        }

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
    }


    Orthanc::ImageAccessor* DecodeRawTile(const std::string& source,
                                          Orthanc::PixelFormat format,
                                          unsigned int width,
                                          unsigned int height)
    {
      unsigned int bpp = GetBytesPerPixel(format);

      if (bpp * width * height != source.size())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageSize);
      }

      Orthanc::ImageAccessor accessor;
      accessor.AssignReadOnly(format, width, height, bpp * width, source.empty() ? NULL : source.c_str());

      return Orthanc::Image::Clone(accessor);
    }


    void EncodeTile(std::string& target,
                    const Orthanc::ImageAccessor& source,
                    ImageCompression compression,
                    uint8_t quality)
    {
      if (compression == ImageCompression_None)
      {
        unsigned int pitch = GetBytesPerPixel(source.GetFormat()) * source.GetWidth();
        target.resize(pitch * source.GetHeight());

        const unsigned int height = source.GetHeight();
        for (unsigned int i = 0; i < height; i++)
        {
          memcpy(&target[i * pitch], source.GetConstRow(i), pitch);
        }
      }
      else
      {
        std::unique_ptr<Orthanc::IImageWriter> writer;

        switch (compression)
        {
          case ImageCompression_Png:
            writer.reset(new Orthanc::PngWriter);
            break;

          case ImageCompression_Jpeg:
            writer.reset(new Orthanc::JpegWriter);
            dynamic_cast<Orthanc::JpegWriter&>(*writer).SetQuality(quality);
            break;

          case ImageCompression_Jpeg2000:
            writer.reset(new Jpeg2000Writer);
            break;

          default:
            throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
        }

        Orthanc::IImageWriter::WriteToMemory(*writer, target, source);
      }
    }


    void ChangeTileCompression(std::string& target,
                               const std::string& source,
                               ImageCompression sourceCompression,
                               ImageCompression targetCompression,
                               uint8_t quality)
    {
      if (sourceCompression == targetCompression)
      {
        target = source;
      }
      else
      {
        std::unique_ptr<Orthanc::ImageAccessor> decoded(DecodeTile(source, sourceCompression));
        EncodeTile(target, *decoded, targetCompression, quality);
      }
    }


    static uint8_t GetPixelValue(const Orthanc::ImageAccessor& source,
                                 unsigned int x,
                                 unsigned int y,
                                 unsigned int channel,
                                 int offsetX,
                                 int offsetY,
                                 unsigned int bytesPerPixel)
    {
      assert(bytesPerPixel == source.GetBytesPerPixel());
      assert(channel < bytesPerPixel);
      assert(source.GetFormat() == Orthanc::PixelFormat_Grayscale8 ||
             source.GetFormat() == Orthanc::PixelFormat_RGB24 ||
             source.GetFormat() == Orthanc::PixelFormat_RGBA32);  // 16bpp is unsupported

      if (static_cast<int>(x) + offsetX < 0)
      {
        x = 0;
      }
      else
      {
        x += offsetX;
        if (x >= source.GetWidth())
        {
          x = source.GetWidth() - 1;
        }
      }

      if (static_cast<int>(y) + offsetY < 0)
      {
        y = 0;
      }
      else
      {
        y += offsetY;
        if (y >= source.GetHeight())
        {
          y = source.GetHeight() - 1;
        }
      }

      return *(reinterpret_cast<const uint8_t*>(source.GetConstBuffer()) +
               y * source.GetPitch() + x * bytesPerPixel + channel);
    }


    static uint8_t SmoothPixelValue(const Orthanc::ImageAccessor& source,
                                    unsigned int x,
                                    unsigned int y,
                                    unsigned int channel,
                                    unsigned int bytesPerPixel)
    {
      static const uint32_t kernel[5] = { 1, 4, 6, 4, 1 };
      static const uint32_t normalization = 2 * (1 + 4 + 6 + 4 + 1);

      uint32_t accumulator = 0;

      // Horizontal smoothing
      for (int offset = -2; offset <= 2; offset++)
      {
        accumulator += kernel[offset + 2] * GetPixelValue(source, x, y, channel, offset, 0, bytesPerPixel);
      }

      // Vertical smoothing
      for (int offset = -2; offset <= 2; offset++)
      {
        accumulator += kernel[offset + 2] * GetPixelValue(source, x, y, channel, 0, offset, bytesPerPixel);
      }

      return static_cast<uint8_t>(accumulator / normalization);
    }


    Orthanc::ImageAccessor* Halve(const Orthanc::ImageAccessor& source,
                                  bool smooth)
    {
      if (source.GetWidth() % 2 == 1 ||
          source.GetHeight() % 2 == 1)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageSize);
      }

      if (source.GetFormat() != Orthanc::PixelFormat_Grayscale8 &&
          source.GetFormat() != Orthanc::PixelFormat_RGB24 &&
          source.GetFormat() != Orthanc::PixelFormat_RGBA32)  // 16bpp is not supported (*)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
      }

      const unsigned int bytesPerPixel = source.GetBytesPerPixel();  // Corresponds to the number of channels tx (*)

      std::unique_ptr<Orthanc::ImageAccessor> target(Allocate(source.GetFormat(), 
                                                              source.GetWidth() / 2, 
                                                              source.GetHeight() / 2));

      const unsigned int width = target->GetWidth();
      const unsigned int height = target->GetHeight();

      for (unsigned int y = 0; y < height; y++)
      {
        uint8_t* q = reinterpret_cast<uint8_t*>(target->GetRow(y));

        for (unsigned int x = 0; x < width; x++, q += bytesPerPixel)
        {
          for (unsigned int c = 0; c < bytesPerPixel; c++)
          {
            if (smooth)
            {
              q[c] = SmoothPixelValue(source, 2 * x, 2 * y, c, bytesPerPixel);
            }
            else
            {
              q[c] = GetPixelValue(source, 2 * x, 2 * y, c, 0, 0, bytesPerPixel);
            }
          }
        }
      }

      return target.release();
    }


    Orthanc::ImageAccessor* Clone(const Orthanc::ImageAccessor& accessor)
    {
      std::unique_ptr<Orthanc::ImageAccessor> result(Allocate(accessor.GetFormat(),
                                                              accessor.GetWidth(),
                                                              accessor.GetHeight()));
      Embed(*result, accessor, 0, 0);

      return result.release();
    }


    Orthanc::ImageAccessor* Render(ITiledPyramid& pyramid,
                                   unsigned int level)
    {
      std::unique_ptr<Orthanc::ImageAccessor> result(Allocate(pyramid.GetPixelFormat(), 
                                                              pyramid.GetLevelWidth(level),
                                                              pyramid.GetLevelHeight(level)));

      LOG(INFO) << "Rendering a tiled image of size "
                << result->GetWidth() << "x" << result->GetHeight();

      const unsigned int width = result->GetWidth();
      const unsigned int height = result->GetHeight();
      
      for (unsigned int y = 0; y < height; y += pyramid.GetTileHeight())
      {
        for (unsigned int x = 0; x < width; x += pyramid.GetTileWidth())
        {
          std::unique_ptr<Orthanc::ImageAccessor> tile(pyramid.DecodeTile(level,
                                                                        x / pyramid.GetTileWidth(),
                                                                        y / pyramid.GetTileHeight()));
          Embed(*result, *tile, x, y);
        }
      }

      return result.release();
    }
  }
}
