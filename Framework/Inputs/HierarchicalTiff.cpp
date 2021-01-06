/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2021 Osimis S.A., Belgium
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
#include "HierarchicalTiff.h"

#include <Logging.h>
#include <OrthancException.h>

#include <iostream>
#include <algorithm>
#include <cassert>
#include <string.h>

namespace OrthancWSI
{
  HierarchicalTiff::Level::Level(TIFF* tiff,
                                 tdir_t    directory,
                                 unsigned int  width,
                                 unsigned int  height) :
    directory_(directory),
    width_(width),
    height_(height)
  {
    // Read the JPEG headers shared at that level, if any
    uint8_t *tables = NULL;
    uint32_t size;
    if (TIFFGetField(tiff, TIFFTAG_JPEGTABLES, &size, &tables) &&
        size > 0 && 
        tables != NULL)
    {
      // Look for the EOI (end-of-image) tag == FF D9
      // https://en.wikipedia.org/wiki/JPEG_File_Interchange_Format

      bool found = false;

      for (size_t i = 0; i + 1 < size; i++)
      {
        if (tables[i] == 0xff &&
            tables[i + 1] == 0xd9)
        {
          headers_.assign(reinterpret_cast<const char*>(tables), i);
          found = true;
        }
      }

      if (!found)
      {
        headers_.assign(reinterpret_cast<const char*>(tables), size);
      }
    }
  }

  struct HierarchicalTiff::Comparator
  {
    bool operator() (const HierarchicalTiff::Level& a,
                     const HierarchicalTiff::Level& b) const
    {
      return a.width_ > b.width_;
    }
  };


  void HierarchicalTiff::Finalize()
  {
    if (tiff_)
    {
      TIFFClose(tiff_);
      tiff_ = NULL;
    }
  }


  void HierarchicalTiff::CheckLevel(unsigned int level) const
  {
    if (level >= levels_.size())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  bool HierarchicalTiff::GetCurrentCompression(ImageCompression& compression)
  {
    uint16_t c;
    if (!TIFFGetField(tiff_, TIFFTAG_COMPRESSION, &c))
    {
      return false;
    }

    switch (c)
    {
      case COMPRESSION_NONE:
        compression = ImageCompression_None;
        return true;

      case COMPRESSION_JPEG:
        compression = ImageCompression_Jpeg;
        return true;

      default:
        return false;
    }
  }


  bool HierarchicalTiff::GetCurrentPixelFormat(Orthanc::PixelFormat& pixelFormat,
                                               Orthanc::PhotometricInterpretation& photometric,
                                               ImageCompression compression)
  {
    // http://www.awaresystems.be/imaging/tiff/tifftags/baseline.html

    uint16_t channels, photometricTiff, bpp, planar;
    if (!TIFFGetField(tiff_, TIFFTAG_SAMPLESPERPIXEL, &channels) ||
        channels == 0 ||
        !TIFFGetField(tiff_, TIFFTAG_PHOTOMETRIC, &photometricTiff) ||
        !TIFFGetField(tiff_, TIFFTAG_BITSPERSAMPLE, &bpp) ||
        !TIFFGetField(tiff_, TIFFTAG_PLANARCONFIG, &planar))
    {
      return false;
    }

    if (compression == ImageCompression_Jpeg &&
        channels == 3 &&     // This is a color image
        bpp == 8 &&
        planar == PLANARCONFIG_CONTIG)  // This is interleaved RGB
    {
      pixelFormat = Orthanc::PixelFormat_RGB24;

      switch (photometricTiff)
      {
        case PHOTOMETRIC_YCBCR:
          photometric = Orthanc::PhotometricInterpretation_YBRFull422;
          return true;
          
        case PHOTOMETRIC_RGB:
          photometric = Orthanc::PhotometricInterpretation_RGB;
          return true;

        default:
          LOG(ERROR) << "Unknown photometric interpretation in TIFF: " << photometricTiff;
          return false;
      }
    }
    else if (compression == ImageCompression_Jpeg &&
             channels == 1 &&     // This is a grayscale image
             bpp == 8)
    {
      pixelFormat = Orthanc::PixelFormat_Grayscale8;
      photometric = Orthanc::PhotometricInterpretation_Monochrome2;
    }
    else
    {
      return false;
    }

    return true;
  }


  bool  HierarchicalTiff::Initialize()
  {
    bool first = true;
    tdir_t pos = 0;

    do
    {
      uint32_t w, h, tw, th;
      ImageCompression compression;
      Orthanc::PixelFormat pixelFormat;
      Orthanc::PhotometricInterpretation photometric;

      if (TIFFSetDirectory(tiff_, pos) &&
          TIFFGetField(tiff_, TIFFTAG_IMAGEWIDTH, &w) &&
          TIFFGetField(tiff_, TIFFTAG_IMAGELENGTH, &h) &&
          TIFFGetField(tiff_, TIFFTAG_TILEWIDTH, &tw) &&
          TIFFGetField(tiff_, TIFFTAG_TILELENGTH, &th) &&
          w > 0 &&
          h > 0 &&
          tw > 0 &&
          th > 0 &&
          GetCurrentCompression(compression) &&
          GetCurrentPixelFormat(pixelFormat, photometric, compression))
      {
        if (first)
        {
          tileWidth_ = tw;
          tileHeight_ = th;
          compression_ = compression;
          pixelFormat_ = pixelFormat;
          photometric_ = photometric;
          first = false;
        }
        else if (tw != tileWidth_ ||
                 th != tileHeight_ ||
                 compression_ != compression ||
                 pixelFormat_ != pixelFormat ||
                 photometric_ != photometric)
        {
          LOG(ERROR) << "The tile size or compression of the TIFF file varies along levels, this is not supported";
          return false;
        }

        levels_.push_back(Level(tiff_, pos, w, h));
      }

      pos++;
    }
    while (TIFFReadDirectory(tiff_));

    if (levels_.size() == 0)
    {
      LOG(ERROR) << "This is not a tiled TIFF image";
      return false;
    }

    std::sort(levels_.begin(), levels_.end(), Comparator());
    return true;
  }


  HierarchicalTiff::HierarchicalTiff(const std::string& path) :
    tileWidth_(0),
    tileHeight_(0)
  {
    tiff_ = TIFFOpen(path.c_str(), "r");
    if (tiff_ == NULL)
    {
      LOG(ERROR) << "libtiff cannot open: " << path;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InexistentFile);
    }

    if (!Initialize())
    {
      Finalize();
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }
  }


  unsigned int HierarchicalTiff::GetLevelWidth(unsigned int level) const
  {
    CheckLevel(level);
    return levels_[level].width_;
  }


  unsigned int HierarchicalTiff::GetLevelHeight(unsigned int level) const
  {
    CheckLevel(level);
    return levels_[level].height_;
  }


  bool HierarchicalTiff::ReadRawTile(std::string& tile,
                                     ImageCompression& compression,
                                     unsigned int level,
                                     unsigned int tileX,
                                     unsigned int tileY)
  {
    boost::mutex::scoped_lock lock(mutex_);

    CheckLevel(level);

    compression = compression_;

    // Make the TIFF context point to the level of interest
    if (!TIFFSetDirectory(tiff_, levels_[level].directory_))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_CorruptedFile);
    }

    // Get the index of the tile
    ttile_t index = TIFFComputeTile(tiff_, tileX * tileWidth_, tileY * tileHeight_, 0 /*z*/, 0 /*sample*/);

    // Read the raw tile
    toff_t *sizes;
    if (!TIFFGetField(tiff_, TIFFTAG_TILEBYTECOUNTS, &sizes))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_CorruptedFile);
    }

    std::string raw;
    raw.resize(sizes[index]);

    tsize_t read = TIFFReadRawTile(tiff_, index, &raw[0], raw.size());
    if (read != static_cast<tsize_t>(sizes[index]))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_CorruptedFile);
    }

    const std::string& headers = levels_[level].headers_;

    // Possibly prepend the raw tile with the shared JPEG headers
    if (headers.empty() ||
        compression_ != ImageCompression_Jpeg)
    {
      tile.swap(raw);   // Same as "tile.assign(raw)", but optimizes memory
    }
    else
    {
      assert(compression_ == ImageCompression_Jpeg);

      // Check that the raw JPEG tile starts with the SOI (start-of-image) tag == FF D8
      if (raw.size() < 2 ||
          static_cast<uint8_t>(raw[0]) != 0xff ||
          static_cast<uint8_t>(raw[1]) != 0xd8)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_CorruptedFile);
      }

      tile.resize(headers.size() + raw.size() - 2);
      memcpy(&tile[0], &headers[0], headers.size());
      memcpy(&tile[0] + headers.size(), &raw[2], raw.size() - 2);
    }

    return true;
  }
}
