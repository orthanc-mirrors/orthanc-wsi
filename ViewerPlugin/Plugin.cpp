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

#include "../Framework/ImageToolbox.h"
#include "../Framework/Jpeg2000Reader.h"
#include "DicomPyramidCache.h"
#include "OrthancPluginConnection.h"

#include <Compatibility.h>  // For std::unique_ptr
#include <Logging.h>
#include <Images/ImageProcessing.h>
#include <Images/JpegReader.h>
#include <Images/JpegWriter.h>
#include <Images/PngWriter.h>
#include <MultiThreading/Semaphore.h>
#include <OrthancException.h>
#include <SystemToolbox.h>

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <EmbeddedResources.h>

#include <cassert>
#include <boost/regex.hpp>
#include <boost/math/special_functions/round.hpp>

std::unique_ptr<OrthancWSI::OrthancPluginConnection>  orthanc_;
std::unique_ptr<OrthancWSI::DicomPyramidCache>        cache_;
std::unique_ptr<Orthanc::Semaphore>                   transcoderSemaphore_;
static std::string                                    publicIIIFUrl_;

static void AnswerSparseTile(OrthancPluginRestOutput* output,
                             unsigned int tileWidth,
                             unsigned int tileHeight)
{
  Orthanc::Image tile(Orthanc::PixelFormat_RGB24, tileWidth, tileHeight, false);

  // Black (TODO parameter)
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  Orthanc::ImageProcessing::Set(tile, red, green, blue, 255);

  // TODO Cache the tile
  OrthancPluginCompressAndAnswerPngImage(OrthancPlugins::GetGlobalContext(),
                                         output, OrthancPluginPixelFormat_RGB24, 
                                         tile.GetWidth(), tile.GetHeight(), 
                                         tile.GetPitch(), tile.GetBuffer());
}


static bool DisplayPerformanceWarning()
{
  (void) DisplayPerformanceWarning;   // Disable warning about unused function
  OrthancPluginLogWarning(OrthancPlugins::GetGlobalContext(), "Performance warning in whole-slide imaging: "
                          "Non-release build, runtime debug assertions are turned on");
  return true;
}


void ServePyramid(OrthancPluginRestOutput* output,
                  const char* url,
                  const OrthancPluginHttpRequest* request)
{
  std::string seriesId(request->groups[0]);

  char tmp[1024];
  sprintf(tmp, "Accessing whole-slide pyramid of series %s", seriesId.c_str());
  OrthancPluginLogInfo(OrthancPlugins::GetGlobalContext(), tmp);
  

  OrthancWSI::DicomPyramidCache::Locker locker(*cache_, seriesId);

  unsigned int totalWidth = locker.GetPyramid().GetLevelWidth(0);
  unsigned int totalHeight = locker.GetPyramid().GetLevelHeight(0);

  Json::Value sizes = Json::arrayValue;
  Json::Value resolutions = Json::arrayValue;
  Json::Value tilesCount = Json::arrayValue;
  Json::Value tilesSizes = Json::arrayValue;
  for (unsigned int i = 0; i < locker.GetPyramid().GetLevelCount(); i++)
  {
    const unsigned int levelWidth = locker.GetPyramid().GetLevelWidth(i);
    const unsigned int levelHeight = locker.GetPyramid().GetLevelHeight(i);
    const unsigned int tileWidth = locker.GetPyramid().GetTileWidth(i);
    const unsigned int tileHeight = locker.GetPyramid().GetTileHeight(i);
    
    resolutions.append(static_cast<float>(totalWidth) / static_cast<float>(levelWidth));
    
    Json::Value s = Json::arrayValue;
    s.append(levelWidth);
    s.append(levelHeight);
    sizes.append(s);

    s = Json::arrayValue;
    s.append(OrthancWSI::CeilingDivision(levelWidth, tileWidth));
    s.append(OrthancWSI::CeilingDivision(levelHeight, tileHeight));
    tilesCount.append(s);

    s = Json::arrayValue;
    s.append(tileWidth);
    s.append(tileHeight);
    tilesSizes.append(s);
  }

  Json::Value result;
  result["ID"] = seriesId;
  result["Resolutions"] = resolutions;
  result["Sizes"] = sizes;
  result["TilesCount"] = tilesCount;
  result["TilesSizes"] = tilesSizes;
  result["TotalHeight"] = totalHeight;
  result["TotalWidth"] = totalWidth;

  std::string s = result.toStyledString();
  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, s.c_str(), s.size(), "application/json");
}


class RawTile : public boost::noncopyable
{
private:
  Orthanc::PixelFormat               format_;
  unsigned int                       tileWidth_;
  unsigned int                       tileHeight_;
  Orthanc::PhotometricInterpretation photometric_;
  std::string                        tile_;
  OrthancWSI::ImageCompression       compression_;

  Orthanc::ImageAccessor* DecodeInternal()
  {
    switch (compression_)
    {
      case OrthancWSI::ImageCompression_Jpeg:
      {
        std::unique_ptr<Orthanc::JpegReader> decoded(new Orthanc::JpegReader);
        decoded->ReadFromMemory(tile_);
        return decoded.release();
      }

      case OrthancWSI::ImageCompression_Jpeg2000:
      {
        std::unique_ptr<OrthancWSI::Jpeg2000Reader> decoded(new OrthancWSI::Jpeg2000Reader);
        decoded->ReadFromMemory(tile_);

        if (photometric_ == Orthanc::PhotometricInterpretation_YBR_ICT)
        {
          OrthancWSI::ImageToolbox::ConvertJpegYCbCrToRgb(*decoded);
        }

        return decoded.release();
      }

      case OrthancWSI::ImageCompression_None:
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

  static void EncodeInternal(std::string& encoded,
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


public:
  RawTile(OrthancWSI::ITiledPyramid& pyramid,
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

  void Answer(OrthancPluginRestOutput* output,
              Orthanc::MimeType transcodingType)
  {
    if (compression_ == OrthancWSI::ImageCompression_Jpeg)
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

  Orthanc::ImageAccessor* Decode()
  {
    Orthanc::Semaphore::Locker locker(*transcoderSemaphore_);
    return DecodeInternal();
  }

  static void Encode(std::string& encoded,
                     const Orthanc::ImageAccessor& decoded,
                     Orthanc::MimeType transcodingType)
  {
    Orthanc::Semaphore::Locker locker(*transcoderSemaphore_);
    EncodeInternal(encoded, decoded, transcodingType);
  }
};


void ServeTile(OrthancPluginRestOutput* output,
               const char* url,
               const OrthancPluginHttpRequest* request)
{
  std::string seriesId(request->groups[0]);
  int level = boost::lexical_cast<int>(request->groups[1]);
  int tileY = boost::lexical_cast<int>(request->groups[3]);
  int tileX = boost::lexical_cast<int>(request->groups[2]);

  char tmp[1024];
  sprintf(tmp, "Accessing tile in series %s: (%d,%d) at level %d", seriesId.c_str(), tileX, tileY, level);
  OrthancPluginLogInfo(OrthancPlugins::GetGlobalContext(), tmp);
  
  if (level < 0 ||
      tileX < 0 ||
      tileY < 0)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
  }

  // Retrieve the raw tile from the WSI pyramid
  std::unique_ptr<RawTile> rawTile;

  {
    OrthancWSI::DicomPyramidCache::Locker locker(*cache_, seriesId);
    rawTile.reset(new RawTile(locker.GetPyramid(),
                              static_cast<unsigned int>(level),
                              static_cast<unsigned int>(tileX),
                              static_cast<unsigned int>(tileY)));
  }

  /**
   * In the case the DICOM file doesn't use the JPEG transfer syntax,
   * transfer the tile (which is presumably lossless) as a PNG image
   * so as to prevent lossy compression. Don't call "rawTile" while
   * the Locker is around, as "Answer()" can be a costly operation.
   **/
  rawTile->Answer(output, Orthanc::MimeType_Png);
}


OrthancPluginErrorCode OnChangeCallback(OrthancPluginChangeType changeType, 
                                        OrthancPluginResourceType resourceType, 
                                        const char *resourceId)
{
  if (resourceType == OrthancPluginResourceType_Series &&
      changeType == OrthancPluginChangeType_NewChildInstance)
  { 
    char tmp[1024];
    sprintf(tmp, "New instance has been added to series %s, invalidating it", resourceId);
    OrthancPluginLogInfo(OrthancPlugins::GetGlobalContext(), tmp);

    cache_->Invalidate(resourceId);
  }

  return OrthancPluginErrorCode_Success;
}



void ServeFile(OrthancPluginRestOutput* output,
               const char* url,
               const OrthancPluginHttpRequest* request)
{
  Orthanc::EmbeddedResources::FileResourceId resource;

  std::string f(request->groups[0]);
  std::string mime;

  if (f == "viewer.html")
  {
    resource = Orthanc::EmbeddedResources::VIEWER_HTML;
    mime = "text/html";
  }
  else if (f == "viewer.js")
  {
    resource = Orthanc::EmbeddedResources::VIEWER_JS;
    mime = "application/javascript";
  }
  else if (f == "ol.js")
  {
    resource = Orthanc::EmbeddedResources::OPENLAYERS_JS;
    mime = "application/javascript";
  }
  else if (f == "ol.css")
  {
    resource = Orthanc::EmbeddedResources::OPENLAYERS_CSS;
    mime = "text/css";
  }
  else
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
  }

  std::string content;
  Orthanc::EmbeddedResources::GetFileResource(content, resource);

  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, content.c_str(), content.size(), mime.c_str());
}



void ServeIIIFImageInfo(OrthancPluginRestOutput* output,
                        const char* url,
                        const OrthancPluginHttpRequest* request)
{
  std::string seriesId(request->groups[0]);

  LOG(INFO) << "IIIF: Image API call to whole-slide pyramid of series " << seriesId;

  OrthancWSI::DicomPyramidCache::Locker locker(*cache_, seriesId);

  if (locker.GetPyramid().GetLevelCount() == 0)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
  }

  if (locker.GetPyramid().GetTileWidth(0) != locker.GetPyramid().GetTileHeight(0))
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageFormat,
                                    "IIIF doesn't support non-isotropic tile sizes");
  }

  for (unsigned int i = 1; i < locker.GetPyramid().GetLevelCount(); i++)
  {
    if (locker.GetPyramid().GetTileWidth(i) != locker.GetPyramid().GetTileWidth(0) ||
        locker.GetPyramid().GetTileHeight(i) != locker.GetPyramid().GetTileHeight(0))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageFormat,
                                      "IIIF doesn't support levels with varying tile sizes");
    }
  }

  Json::Value sizes = Json::arrayValue;
  for (unsigned int i = locker.GetPyramid().GetLevelCount(); i > 0; i--)
  {
    Json::Value level;
    level["width"] = locker.GetPyramid().GetLevelWidth(i - 1);
    level["height"] = locker.GetPyramid().GetLevelHeight(i - 1);
    sizes.append(level);
  }

  Json::Value scaleFactors = Json::arrayValue;
  for (unsigned int i = locker.GetPyramid().GetLevelCount(); i > 0; i--)
  {
    scaleFactors.append(static_cast<float>(locker.GetPyramid().GetLevelWidth(0)) /
                        static_cast<float>(locker.GetPyramid().GetLevelWidth(i - 1)));
  }

  Json::Value tiles;
  tiles["width"] = locker.GetPyramid().GetTileWidth(0);
  tiles["height"] = locker.GetPyramid().GetTileHeight(0);
  tiles["scaleFactors"] = scaleFactors;

  Json::Value result;
  result["@context"] = "http://iiif.io/api/image/2/context.json";
  result["@id"] = publicIIIFUrl_ + seriesId;
  result["profile"] = "http://iiif.io/api/image/2/level0.json";
  result["protocol"] = "http://iiif.io/api/image";
  result["width"] = locker.GetPyramid().GetLevelWidth(0);
  result["height"] = locker.GetPyramid().GetLevelHeight(0);
  result["sizes"] = sizes;

  result["tiles"] = Json::arrayValue;
  result["tiles"].append(tiles);

  std::string s = result.toStyledString();
  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, s.c_str(), s.size(), "application/json");
}


static unsigned int GetPhysicalTileWidth(const OrthancWSI::ITiledPyramid& pyramid,
                                         unsigned int level)
{
  return static_cast<unsigned int>(boost::math::iround(
                                     static_cast<float>(pyramid.GetTileWidth(level)) *
                                     static_cast<float>(pyramid.GetLevelWidth(0)) /
                                     static_cast<float>(pyramid.GetLevelWidth(level))));
}


static unsigned int GetPhysicalTileHeight(const OrthancWSI::ITiledPyramid& pyramid,
                                          unsigned int level)
{
  return static_cast<unsigned int>(boost::math::iround(
                                     static_cast<float>(pyramid.GetTileHeight(level)) *
                                     static_cast<float>(pyramid.GetLevelHeight(0)) /
                                     static_cast<float>(pyramid.GetLevelHeight(level))));
}


void ServeIIIFImageTile(OrthancPluginRestOutput* output,
                        const char* url,
                        const OrthancPluginHttpRequest* request)
{
  std::string seriesId(request->groups[0]);
  std::string region(request->groups[1]);
  std::string size(request->groups[2]);
  std::string rotation(request->groups[3]);
  std::string quality(request->groups[4]);
  std::string format(request->groups[5]);

  LOG(INFO) << "IIIF: Image API call to tile of series " << seriesId << ": "
            << "region=" << region << "; size=" << size << "; rotation="
            << rotation << "; quality=" << quality << "; format=" << format;

  if (rotation != "0")
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "IIIF - Unsupported rotation: " + rotation);
  }

  if (quality != "default")
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "IIIF - Unsupported quality: " + quality);
  }

  if (format != "jpg")
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "IIIF - Unsupported format: " + format);
  }

  if (region == "full")
  {
    OrthancWSI::DicomPyramidCache::Locker locker(*cache_, seriesId);

    OrthancWSI::ITiledPyramid& pyramid = locker.GetPyramid();
    const unsigned int level = pyramid.GetLevelCount() - 1;

    Orthanc::Image full(Orthanc::PixelFormat_RGB24, pyramid.GetLevelWidth(level), pyramid.GetLevelHeight(level), false);
    Orthanc::ImageProcessing::Set(full, 255, 255, 255, 0);

    const unsigned int nx = OrthancWSI::CeilingDivision(pyramid.GetLevelWidth(level), pyramid.GetTileWidth(level));
    const unsigned int ny = OrthancWSI::CeilingDivision(pyramid.GetLevelHeight(level), pyramid.GetTileHeight(level));
    for (unsigned int ty = 0; ty < ny; ty++)
    {
      const unsigned int y = ty * pyramid.GetTileHeight(level);
      const unsigned int height = std::min(pyramid.GetTileHeight(level), full.GetHeight() - y);

      for (unsigned int tx = 0; tx < nx; tx++)
      {
        const unsigned int x = tx * pyramid.GetTileWidth(level);
        std::unique_ptr<Orthanc::ImageAccessor> tile(pyramid.DecodeTile(level, tx, ty));

        const unsigned int width = std::min(pyramid.GetTileWidth(level), full.GetWidth() - x);

        Orthanc::ImageAccessor source, target;
        tile->GetRegion(source, 0, 0, width, height);
        full.GetRegion(target, x, y, width, height);

        Orthanc::ImageProcessing::Copy(target, source);
      }
    }

    std::string encoded;
    RawTile::Encode(encoded, full, Orthanc::MimeType_Jpeg);

    OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, encoded.c_str(),
                              encoded.size(), Orthanc::EnumerationToString(Orthanc::MimeType_Jpeg));
  }
  else
  {
    int regionX, regionY, regionWidth, regionHeight;

    bool ok = false;
    boost::regex regionPattern("([0-9]+),([0-9]+),([0-9]+),([0-9]+)");
    boost::cmatch regionWhat;
    if (regex_match(region.c_str(), regionWhat, regionPattern))
    {
      try
      {
        regionX = boost::lexical_cast<int>(regionWhat[1]);
        regionY = boost::lexical_cast<int>(regionWhat[2]);
        regionWidth = boost::lexical_cast<int>(regionWhat[3]);
        regionHeight = boost::lexical_cast<int>(regionWhat[4]);
        ok = (regionX >= 0 &&
              regionY >= 0 &&
              regionWidth > 0 &&
              regionHeight > 0);
      }
      catch (boost::bad_lexical_cast&)
      {
      }
    }

    if (!ok)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "IIIF - Not a (x,y,width,height) region: " + region);
    }

    int cropWidth;
    boost::regex sizePattern("([0-9]+),");
    boost::cmatch sizeWhat;
    if (regex_match(size.c_str(), sizeWhat, sizePattern))
    {
      try
      {
        cropWidth = boost::lexical_cast<int>(sizeWhat[1]);
        ok = (cropWidth > 0);
      }
      catch (boost::bad_lexical_cast&)
      {
      }
    }

    if (!ok)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "IIIF - Not a (width,) size: " + size);
    }

    std::unique_ptr<RawTile> rawTile;
    std::unique_ptr<Orthanc::ImageAccessor> toCrop;

    {
      OrthancWSI::DicomPyramidCache::Locker locker(*cache_, seriesId);

      OrthancWSI::ITiledPyramid& pyramid = locker.GetPyramid();

      unsigned int level;
      for (level = 0; level < pyramid.GetLevelCount(); level++)
      {
        const unsigned int physicalTileWidth = GetPhysicalTileWidth(pyramid, level);
        const unsigned int physicalTileHeight = GetPhysicalTileHeight(pyramid, level);

        if (regionX % physicalTileWidth == 0 &&
            regionY % physicalTileHeight == 0 &&
            regionWidth <= physicalTileWidth &&
            regionHeight <= physicalTileHeight)
        {
          break;
        }
      }

      if (cropWidth > pyramid.GetTileWidth(level))
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadRequest, "IIIF - Request for a cropping that is too large for the tile size");
      }

      if (level == pyramid.GetLevelCount())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadRequest, "IIIF - Cannot locate the level of interest");
      }
      else
      {
        rawTile.reset(new RawTile(locker.GetPyramid(), level,
                                  regionX / GetPhysicalTileWidth(pyramid, level),
                                  regionY / GetPhysicalTileHeight(pyramid, level)));

        if (cropWidth < pyramid.GetTileWidth(level))
        {
          toCrop.reset(rawTile->Decode());
          rawTile.reset(NULL);
        }
      }
    }

    if (rawTile.get() != NULL)
    {
      assert(toCrop.get() == NULL);

      // Level 0 Compliance of IIIF expects JPEG files
      rawTile->Answer(output, Orthanc::MimeType_Jpeg);
    }
    else if (toCrop.get() != NULL)
    {
      assert(rawTile.get() == NULL);
      assert(cropWidth < toCrop->GetWidth());

      Orthanc::ImageAccessor cropped;
      toCrop->GetRegion(cropped, 0, 0, cropWidth, toCrop->GetHeight());

      std::string encoded;
      RawTile::Encode(encoded, cropped, Orthanc::MimeType_Jpeg);

      OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, encoded.c_str(),
                                encoded.size(), Orthanc::EnumerationToString(Orthanc::MimeType_Jpeg));
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
  }
}


void ServeIIIFManifest(OrthancPluginRestOutput* output,
                       const char* url,
                       const OrthancPluginHttpRequest* request)
{
  /**
   * This is based on IIIF cookbook: "Support Deep Viewing with Basic
   * Use of a IIIF Image Service."
   * https://iiif.io/api/cookbook/recipe/0005-image-service/
   **/

  std::string seriesId(request->groups[0]);

  LOG(INFO) << "IIIF: Presentation API call to whole-slide pyramid of series " << seriesId;

  Json::Value study, series;
  if (!OrthancPlugins::RestApiGet(series, "/series/" + seriesId, false) ||
      !OrthancPlugins::RestApiGet(study, "/series/" + seriesId + "/study", false))
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
  }

  unsigned int width, height;

  {
    OrthancWSI::DicomPyramidCache::Locker locker(*cache_, seriesId);
    width = locker.GetPyramid().GetLevelWidth(0);
    height = locker.GetPyramid().GetLevelHeight(0);
  }

  const std::string base = publicIIIFUrl_ + seriesId;

  Json::Value service;
  service["id"] = base;
  service["profile"] = "level0";
  service["type"] = "ImageService3";

  Json::Value body;
  body["id"] = base + "/full/max/0/default.jpg";
  body["type"] = "Image";
  body["format"] = Orthanc::EnumerationToString(Orthanc::MimeType_Jpeg);
  body["height"] = height;
  body["width"] = width;
  body["service"].append(service);

  Json::Value annotation;
  annotation["id"] = base + "/annotation/p0001-image";
  annotation["type"] = "Annotation";
  annotation["motivation"] = "painting";
  annotation["body"] = body;
  annotation["target"] = base + "/canvas/p1";

  Json::Value annotationPage;
  annotationPage["id"] = base + "/page/p1/1";
  annotationPage["type"] = "AnnotationPage";
  annotationPage["items"].append(annotation);

  Json::Value canvas;
  canvas["id"] = annotation["target"];
  canvas["type"] = "Canvas";
  canvas["width"] = width;
  canvas["height"] = height;

  Json::Value labels = Json::arrayValue;
  labels.append(series["MainDicomTags"]["SeriesDate"].asString() + " - " +
                series["MainDicomTags"]["SeriesDescription"].asString());
  canvas["label"]["en"] = labels;

  canvas["items"].append(annotationPage);

  Json::Value manifest;
  manifest["@context"] = "http://iiif.io/api/presentation/3/context.json";
  manifest["id"] = base + "/manifest.json";
  manifest["type"] = "Manifest";

  labels = Json::arrayValue;
  labels.append(study["MainDicomTags"]["StudyDate"].asString() + " - " +
                study["MainDicomTags"]["StudyDescription"].asString());
  manifest["label"]["en"] = labels;

  manifest["items"].append(canvas);

  std::string s = manifest.toStyledString();
  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, s.c_str(), s.size(), "application/json");
}


extern "C"
{
  ORTHANC_PLUGINS_API int32_t OrthancPluginInitialize(OrthancPluginContext* context)
  {
    OrthancPlugins::SetGlobalContext(context);
    assert(DisplayPerformanceWarning());

    /* Check the version of the Orthanc core */
    if (OrthancPluginCheckVersion(OrthancPlugins::GetGlobalContext()) == 0)
    {
      char info[1024];
      sprintf(info, "Your version of Orthanc (%s) must be above %d.%d.%d to run this plugin",
              OrthancPlugins::GetGlobalContext()->orthancVersion,
              ORTHANC_PLUGINS_MINIMAL_MAJOR_NUMBER,
              ORTHANC_PLUGINS_MINIMAL_MINOR_NUMBER,
              ORTHANC_PLUGINS_MINIMAL_REVISION_NUMBER);
      OrthancPluginLogError(OrthancPlugins::GetGlobalContext(), info);
      return -1;
    }

    if (!OrthancPlugins::CheckMinimalOrthancVersion(1, 1, 0))
    {
      // We need the "/instances/.../frames/.../raw" URI that was introduced in Orthanc 1.1.0
      return -1;
    }

#if ORTHANC_FRAMEWORK_VERSION_IS_ABOVE(1, 7, 2)
    Orthanc::Logging::InitializePluginContext(context);
#else
    Orthanc::Logging::Initialize(context);
#endif

    // Limit the number of PNG transcoders to the number of available
    // hardware threads (e.g. number of CPUs or cores or
    // hyperthreading units)
    unsigned int threads = Orthanc::SystemToolbox::GetHardwareConcurrency();
    transcoderSemaphore_.reset(new Orthanc::Semaphore(threads));

    char info[1024];
    sprintf(info, "The whole-slide imaging plugin will use at most %u threads to transcode the tiles", threads);
    OrthancPluginLogWarning(OrthancPlugins::GetGlobalContext(), info);

    OrthancPluginSetDescription(context, "Provides a Web viewer of whole-slide microscopic images within Orthanc.");

    orthanc_.reset(new OrthancWSI::OrthancPluginConnection);
    cache_.reset(new OrthancWSI::DicomPyramidCache(*orthanc_, 10 /* Number of pyramids to be cached - TODO parameter */));

    {
      // TODO => CONFIG
      publicIIIFUrl_ = "http://localhost:8042/wsi/iiif";

      if (publicIIIFUrl_.empty() ||
          publicIIIFUrl_[publicIIIFUrl_.size() - 1] != '/')
      {
        publicIIIFUrl_ += "/";
      }
    }

    OrthancPluginRegisterOnChangeCallback(OrthancPlugins::GetGlobalContext(), OnChangeCallback);

    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(ol.css)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(ol.js)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(viewer.html)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(viewer.js)", true);
    OrthancPlugins::RegisterRestCallback<ServePyramid>("/wsi/pyramids/([0-9a-f-]+)", true);
    OrthancPlugins::RegisterRestCallback<ServeTile>("/wsi/tiles/([0-9a-f-]+)/([0-9-]+)/([0-9-]+)/([0-9-]+)", true);

    OrthancPlugins::RegisterRestCallback<ServeIIIFImageInfo>("/wsi/iiif/([0-9a-f-]+)/info.json", true);
    OrthancPlugins::RegisterRestCallback<ServeIIIFImageTile>("/wsi/iiif/([0-9a-f-]+)/([0-9a-z,:]+)/([0-9a-z,!:]+)/([0-9,!]+)/([a-z]+)\\.([a-z]+)", true);
    OrthancPlugins::RegisterRestCallback<ServeIIIFManifest>("/wsi/iiif/([0-9a-f-]+)/manifest.json", true);

    // Extend the default Orthanc Explorer with custom JavaScript for WSI
    std::string explorer;
    Orthanc::EmbeddedResources::GetFileResource(explorer, Orthanc::EmbeddedResources::ORTHANC_EXPLORER);
    OrthancPluginExtendOrthancExplorer(OrthancPlugins::GetGlobalContext(), explorer.c_str());

    return 0;
  }


  ORTHANC_PLUGINS_API void OrthancPluginFinalize()
  {
    cache_.reset(NULL);
    orthanc_.reset(NULL);
    transcoderSemaphore_.reset(NULL);
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetName()
  {
    return "wsi";
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetVersion()
  {
    return ORTHANC_WSI_VERSION;
  }
}
