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


#include "../Framework/PrecompiledHeadersWSI.h"
#include "../Framework/Inputs/DicomPyramid.h"
#include "../Framework/Jpeg2000Reader.h"
#include "../Framework/Messaging/PluginOrthancConnection.h"

#include "../Resources/Orthanc/Core/Images/ImageProcessing.h"
#include "../Resources/Orthanc/Core/Images/PngWriter.h"
#include "../Resources/Orthanc/Core/MultiThreading/Semaphore.h"
#include "../Resources/Orthanc/Core/OrthancException.h"
#include "../Resources/Orthanc/Plugins/Samples/Common/OrthancPluginCppWrapper.h"

#include <EmbeddedResources.h>

#include <cassert>



namespace OrthancWSI
{
  // TODO Add LRU recycling policy
  class DicomPyramidCache : public boost::noncopyable
  {
  private:
    boost::mutex                  mutex_;
    IOrthancConnection&           orthanc_;
    std::auto_ptr<DicomPyramid>   pyramid_;

    DicomPyramid& GetPyramid(const std::string& seriesId)
    {
      // Mutex is assumed to be locked

      if (pyramid_.get() == NULL ||
          pyramid_->GetSeriesId() != seriesId)
      {
        pyramid_.reset(new DicomPyramid(orthanc_, seriesId));
      }

      if (pyramid_.get() == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }

      return *pyramid_;
    }

  public:
    DicomPyramidCache(IOrthancConnection& orthanc) :
      orthanc_(orthanc)
    {
    }

    void Invalidate(const std::string& seriesId)
    {
      boost::mutex::scoped_lock  lock(mutex_);

      if (pyramid_.get() != NULL &&
          pyramid_->GetSeriesId() == seriesId)
      {
        pyramid_.reset(NULL);
      }
    }

    class Locker : public boost::noncopyable
    {
    private:
      boost::mutex::scoped_lock  lock_;
      DicomPyramid&              pyramid_;

    public:
      Locker(DicomPyramidCache& cache,
             const std::string& seriesId) :
        lock_(cache.mutex_),
        pyramid_(cache.GetPyramid(seriesId))
      {
      }

      DicomPyramid& GetPyramid() const
      {
        return pyramid_;
      }
    };
  };
}


OrthancPluginContext* context_ = NULL;

std::auto_ptr<OrthancWSI::PluginOrthancConnection>  orthanc_;
std::auto_ptr<OrthancWSI::DicomPyramidCache>        cache_;
std::auto_ptr<Orthanc::Semaphore>                   transcoderSemaphore_;
std::string                                         sparseTile_;


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
  OrthancPluginCompressAndAnswerPngImage(context_, output, OrthancPluginPixelFormat_RGB24, 
                                         tile.GetWidth(), tile.GetHeight(), 
                                         tile.GetPitch(), tile.GetBuffer());
}


static bool DisplayPerformanceWarning()
{
  (void) DisplayPerformanceWarning;   // Disable warning about unused function
  OrthancPluginLogWarning(context_, "Performance warning in whole-slide imaging: "
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
  OrthancPluginLogInfo(context_, tmp);
  

  OrthancWSI::DicomPyramidCache::Locker locker(*cache_, seriesId);

  unsigned int totalWidth = locker.GetPyramid().GetLevelWidth(0);
  unsigned int totalHeight = locker.GetPyramid().GetLevelHeight(0);

  Json::Value resolutions = Json::arrayValue;
  for (unsigned int i = 0; i < locker.GetPyramid().GetLevelCount(); i++)
  {
    resolutions.append(static_cast<float>(totalWidth) /
                       static_cast<float>(locker.GetPyramid().GetLevelWidth(i)));
  }

  Json::Value result;
  result["ID"] = seriesId;
  result["TotalWidth"] = totalWidth;
  result["TotalHeight"] = totalHeight;
  result["TileWidth"] = locker.GetPyramid().GetTileWidth();
  result["TileHeight"] = locker.GetPyramid().GetTileHeight();
  result["Resolutions"] = resolutions;

  std::string s = result.toStyledString();
  OrthancPluginAnswerBuffer(context_, output, s.c_str(), s.size(), "application/json");
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
  OrthancPluginLogInfo(context_, tmp);
  
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
    OrthancPluginAnswerBuffer(context_, output, tile.c_str(), tile.size(), "image/jpeg");
    return;   // We're done
  }


  // The tile does not come from a DICOM-JPEG instance, we need to
  // decompress the raw tile
  std::auto_ptr<Orthanc::ImageAccessor> decoded;

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

  OrthancPluginAnswerBuffer(context_, output, png.c_str(), png.size(), "image/png");
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
    OrthancPluginLogInfo(context_, tmp);

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

  OrthancPluginAnswerBuffer(context_, output, content.c_str(), content.size(), mime.c_str());
}



extern "C"
{
  ORTHANC_PLUGINS_API int32_t OrthancPluginInitialize(OrthancPluginContext* context)
  {
    context_ = context;
    assert(DisplayPerformanceWarning());

    /* Check the version of the Orthanc core */
    if (OrthancPluginCheckVersion(context_) == 0)
    {
      char info[1024];
      sprintf(info, "Your version of Orthanc (%s) must be above %d.%d.%d to run this plugin",
              context_->orthancVersion,
              ORTHANC_PLUGINS_MINIMAL_MAJOR_NUMBER,
              ORTHANC_PLUGINS_MINIMAL_MINOR_NUMBER,
              ORTHANC_PLUGINS_MINIMAL_REVISION_NUMBER);
      OrthancPluginLogError(context_, info);
      return -1;
    }

    if (!OrthancPlugins::CheckMinimalOrthancVersion(context_, 1, 1, 0))
    {
      // We need the "/instances/.../frames/.../raw" URI that was introduced in Orthanc 1.1.0
      return -1;
    }

    // Limit the number of PNG transcoders to the number of available
    // hardware threads (e.g. number of CPUs or cores or
    // hyperthreading units)
    unsigned int threads = boost::thread::hardware_concurrency();
    
    if (threads <= 0)
    {
      threads = 1;
    }
    
    transcoderSemaphore_.reset(new Orthanc::Semaphore(threads));

    char info[1024];
    sprintf(info, "The whole-slide imaging plugin will use at most %d threads to transcode the tiles", threads);
    OrthancPluginLogWarning(context_, info);

    OrthancPluginSetDescription(context, "Provides a Web viewer of whole-slide microscopic images within Orthanc.");

    orthanc_.reset(new OrthancWSI::PluginOrthancConnection(context));
    cache_.reset(new OrthancWSI::DicomPyramidCache(*orthanc_));

    OrthancPluginRegisterOnChangeCallback(context_, OnChangeCallback);

    OrthancPlugins::RegisterRestCallback<ServeFile>(context, "/wsi/app/(ol.css)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>(context, "/wsi/app/(ol.js)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>(context, "/wsi/app/(viewer.html)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>(context, "/wsi/app/(viewer.js)", true);
    OrthancPlugins::RegisterRestCallback<ServePyramid>(context, "/wsi/pyramids/([0-9a-f-]+)", true);
    OrthancPlugins::RegisterRestCallback<ServeTile>(context, "/wsi/tiles/([0-9a-f-]+)/([0-9-]+)/([0-9-]+)/([0-9-]+)", true);

    // Extend the default Orthanc Explorer with custom JavaScript for WSI
    std::string explorer;
    Orthanc::EmbeddedResources::GetFileResource(explorer, Orthanc::EmbeddedResources::ORTHANC_EXPLORER);
    OrthancPluginExtendOrthancExplorer(context_, explorer.c_str());

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
