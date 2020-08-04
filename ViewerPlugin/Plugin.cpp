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


#include "../Framework/PrecompiledHeadersWSI.h"

#include "../Framework/Jpeg2000Reader.h"
#include "DicomPyramidCache.h"
#include "OrthancPluginConnection.h"

#include <Compatibility.h>  // For std::unique_ptr
#include <Logging.h>
#include <Images/ImageProcessing.h>
#include <Images/PngWriter.h>
#include <MultiThreading/Semaphore.h>
#include <OrthancException.h>
#include <SystemToolbox.h>

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <EmbeddedResources.h>

#include <cassert>

std::unique_ptr<OrthancWSI::OrthancPluginConnection>  orthanc_;
std::unique_ptr<OrthancWSI::DicomPyramidCache>        cache_;
std::unique_ptr<Orthanc::Semaphore>                   transcoderSemaphore_;


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

  unsigned int tileWidth = locker.GetPyramid().GetTileWidth();
  unsigned int tileHeight = locker.GetPyramid().GetTileHeight();
  unsigned int totalWidth = locker.GetPyramid().GetLevelWidth(0);
  unsigned int totalHeight = locker.GetPyramid().GetLevelHeight(0);

  Json::Value sizes = Json::arrayValue;
  Json::Value resolutions = Json::arrayValue;
  Json::Value tilesCount = Json::arrayValue;
  for (unsigned int i = 0; i < locker.GetPyramid().GetLevelCount(); i++)
  {
    unsigned int levelWidth = locker.GetPyramid().GetLevelWidth(i);
    unsigned int levelHeight = locker.GetPyramid().GetLevelHeight(i);

    resolutions.append(static_cast<float>(totalWidth) / static_cast<float>(levelWidth));
    
    Json::Value s = Json::arrayValue;
    s.append(levelWidth);
    s.append(levelHeight);
    sizes.append(s);

    s = Json::arrayValue;
    s.append(OrthancWSI::CeilingDivision(levelWidth, tileWidth));
    s.append(OrthancWSI::CeilingDivision(levelHeight, tileHeight));
    tilesCount.append(s);
  }

  Json::Value result;
  result["ID"] = seriesId;
  result["Resolutions"] = resolutions;
  result["Sizes"] = sizes;
  result["TileHeight"] = tileHeight;
  result["TileWidth"] = tileWidth;
  result["TilesCount"] = tilesCount;
  result["TotalHeight"] = totalHeight;
  result["TotalWidth"] = totalWidth;

  std::string s = result.toStyledString();
  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, s.c_str(), s.size(), "application/json");
}


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
  OrthancWSI::ImageCompression compression;
  Orthanc::PixelFormat format;
  std::string tile;
  unsigned int tileWidth, tileHeight;

  {
    OrthancWSI::DicomPyramidCache::Locker locker(*cache_, seriesId);

    format = locker.GetPyramid().GetPixelFormat();
    tileWidth = locker.GetPyramid().GetTileWidth();
    tileHeight = locker.GetPyramid().GetTileHeight();

    if (!locker.GetPyramid().ReadRawTile(tile, compression, 
                                         static_cast<unsigned int>(level),
                                         static_cast<unsigned int>(tileX),
                                         static_cast<unsigned int>(tileY)))
    {
      // Handling of missing tile (for sparse tiling): TODO parameter?
      // AnswerSparseTile(output, tileWidth, tileHeight); return;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }
  }

  
  // Test whether the tile is a JPEG image. In such a case, we can
  // serve it as such, because any Web browser can handle JPEG

  if (compression == OrthancWSI::ImageCompression_Jpeg)
  {
    OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, tile.c_str(), tile.size(), "image/jpeg");
    return;   // We're done
  }


  // The tile does not come from a DICOM-JPEG instance, we need to
  // decompress the raw tile
  std::unique_ptr<Orthanc::ImageAccessor> decoded;

  Orthanc::Semaphore::Locker locker(*transcoderSemaphore_);

  switch (compression)
  {
    case OrthancWSI::ImageCompression_Jpeg2000:
      decoded.reset(new OrthancWSI::Jpeg2000Reader);
      dynamic_cast<OrthancWSI::Jpeg2000Reader&>(*decoded).ReadFromMemory(tile);
      break;

    case OrthancWSI::ImageCompression_None:
    {
      unsigned int bpp = Orthanc::GetBytesPerPixel(format);
      if (bpp * tileWidth * tileHeight != tile.size())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      decoded.reset(new Orthanc::ImageAccessor);
      decoded->AssignReadOnly(format, tileWidth, tileHeight, bpp * tileWidth, tile.c_str());
      break;
    }

    default:
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
  }


  // This is a lossless frame (coming from a JPEG2000 or uncompressed
  // DICOM instance), serve it as a PNG image so as to prevent lossy
  // compression

  std::string png;
  Orthanc::PngWriter writer;
  writer.WriteToMemory(png, *decoded);

  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, png.c_str(), png.size(), "image/png");
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

    OrthancPluginRegisterOnChangeCallback(OrthancPlugins::GetGlobalContext(), OnChangeCallback);

    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(ol.css)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(ol.js)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(viewer.html)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(viewer.js)", true);
    OrthancPlugins::RegisterRestCallback<ServePyramid>("/wsi/pyramids/([0-9a-f-]+)", true);
    OrthancPlugins::RegisterRestCallback<ServeTile>("/wsi/tiles/([0-9a-f-]+)/([0-9-]+)/([0-9-]+)/([0-9-]+)", true);

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
