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
#include "IIIF.h"

#include "DicomPyramidCache.h"
#include "RawTile.h"
#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Images/Image.h>
#include <Images/ImageProcessing.h>
#include <Logging.h>

#include <boost/regex.hpp>
#include <boost/math/special_functions/round.hpp>


static std::string  iiifPublicUrl_;


static void ServeIIIFImageInfo(OrthancPluginRestOutput* output,
                               const char* url,
                               const OrthancPluginHttpRequest* request)
{
  std::string seriesId(request->groups[0]);

  LOG(INFO) << "IIIF: Image API call to whole-slide pyramid of series " << seriesId;

  OrthancWSI::DicomPyramidCache::Locker locker(seriesId);

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
  Json::Value scaleFactors = Json::arrayValue;

  for (unsigned int i = locker.GetPyramid().GetLevelCount(); i > 0; i--)
  {
    /**
     * Openseadragon seems to have difficulties in rendering
     * non-integer scale factors. Consequently, we only keep the
     * levels with an integer scale factor.
     **/
    if (locker.GetPyramid().GetLevelWidth(0) % locker.GetPyramid().GetLevelWidth(i - 1) == 0 &&
        locker.GetPyramid().GetLevelHeight(0) % locker.GetPyramid().GetLevelHeight(i - 1) == 0)
    {
      Json::Value level;
      level["width"] = locker.GetPyramid().GetLevelWidth(i - 1);
      level["height"] = locker.GetPyramid().GetLevelHeight(i - 1);
      sizes.append(level);

      scaleFactors.append(static_cast<float>(locker.GetPyramid().GetLevelWidth(0)) /
                          static_cast<float>(locker.GetPyramid().GetLevelWidth(i - 1)));
    }
  }

  Json::Value tiles;
  tiles["width"] = locker.GetPyramid().GetTileWidth(0);
  tiles["height"] = locker.GetPyramid().GetTileHeight(0);
  tiles["scaleFactors"] = scaleFactors;

  Json::Value result;
  result["@context"] = "http://iiif.io/api/image/2/context.json";
  result["@id"] = iiifPublicUrl_ + seriesId;
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


static void ServeIIIFImageTile(OrthancPluginRestOutput* output,
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
    OrthancWSI::DicomPyramidCache::Locker locker(seriesId);

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
    OrthancWSI::RawTile::Encode(encoded, full, Orthanc::MimeType_Jpeg);

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

    std::unique_ptr<OrthancWSI::RawTile> rawTile;
    std::unique_ptr<Orthanc::ImageAccessor> toCrop;

    {
      OrthancWSI::DicomPyramidCache::Locker locker(seriesId);

      OrthancWSI::ITiledPyramid& pyramid = locker.GetPyramid();

      unsigned int level;
      for (level = 0; level < pyramid.GetLevelCount(); level++)
      {
        const unsigned int physicalTileWidth = GetPhysicalTileWidth(pyramid, level);
        const unsigned int physicalTileHeight = GetPhysicalTileHeight(pyramid, level);

        if (regionX % physicalTileWidth == 0 &&
            regionY % physicalTileHeight == 0 &&
            regionWidth <= physicalTileWidth &&
            regionHeight <= physicalTileHeight &&
            regionX + regionWidth <= pyramid.GetLevelWidth(0) &&
            regionY + regionHeight <= pyramid.GetLevelHeight(0))
        {
          break;
        }
      }

      if (level == pyramid.GetLevelCount())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadRequest, "IIIF - Cannot locate the level of interest");
      }
      else if (cropWidth > pyramid.GetTileWidth(level))
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadRequest, "IIIF - Request for a cropping that is too large for the tile size");
      }
      else
      {
        rawTile.reset(new OrthancWSI::RawTile(locker.GetPyramid(), level,
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
      OrthancWSI::RawTile::Encode(encoded, cropped, Orthanc::MimeType_Jpeg);

      OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, encoded.c_str(),
                                encoded.size(), Orthanc::EnumerationToString(Orthanc::MimeType_Jpeg));
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
  }
}


static void ServeIIIFManifest(OrthancPluginRestOutput* output,
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
    OrthancWSI::DicomPyramidCache::Locker locker(seriesId);
    width = locker.GetPyramid().GetLevelWidth(0);
    height = locker.GetPyramid().GetLevelHeight(0);
  }

  const std::string base = iiifPublicUrl_ + seriesId;

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


void InitializeIIIF(const std::string& iiifPublicUrl)
{
  iiifPublicUrl_ = iiifPublicUrl;

  OrthancPlugins::RegisterRestCallback<ServeIIIFImageInfo>("/wsi/iiif/([0-9a-f-]+)/info.json", true);
  OrthancPlugins::RegisterRestCallback<ServeIIIFImageTile>("/wsi/iiif/([0-9a-f-]+)/([0-9a-z,:]+)/([0-9a-z,!:]+)/([0-9,!]+)/([a-z]+)\\.([a-z]+)", true);
  OrthancPlugins::RegisterRestCallback<ServeIIIFManifest>("/wsi/iiif/([0-9a-f-]+)/manifest.json", true);
}
