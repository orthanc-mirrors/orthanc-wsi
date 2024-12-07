/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2024 Orthanc Team SRL, Belgium
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

#include "DicomPyramidCache.h"
#include "IIIF.h"
#include "RawTile.h"
#include "../Framework/Inputs/DecodedTiledPyramid.h"
#include "../Framework/Inputs/OnTheFlyPyramid.h"
#include "../Framework/Inputs/OnTheFlyPyramidsCache.h"
#include "../Framework/ImageToolbox.h"

#include <Compatibility.h>  // For std::unique_ptr
#include <Images/Image.h>
#include <Images/ImageProcessing.h>
#include <Logging.h>
#include <OrthancException.h>
#include <SystemToolbox.h>

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <EmbeddedResources.h>

#include <cassert>
#include <Images/PngReader.h>

#include "OrthancPluginConnection.h"


#define ORTHANC_PLUGIN_NAME "wsi"


namespace OrthancWSI
{
  class OrthancPyramidFrameFetcher : public OnTheFlyPyramidsCache::IPyramidFetcher
  {
  private:
    std::unique_ptr<OrthancStone::IOrthancConnection>  orthanc_;
    bool                                               smooth_;

  public:
    explicit OrthancPyramidFrameFetcher(OrthancStone::IOrthancConnection* orthanc,
                                        bool smooth) :
      orthanc_(orthanc),
      smooth_(smooth)
    {
      if (orthanc == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
      }
    }

    DecodedTiledPyramid * Fetch(const std::string &instanceId,
                                unsigned frameNumber) ORTHANC_OVERRIDE
    {
      OrthancPlugins::MemoryBuffer buffer;
      buffer.GetDicomInstance(instanceId.c_str());

      OrthancPlugins::DicomInstance dicom(buffer.GetData(), buffer.GetSize());

      std::unique_ptr<OrthancPlugins::OrthancImage> frame(dicom.GetDecodedFrame(frameNumber));

      Orthanc::PixelFormat format;
      switch (frame->GetPixelFormat())
      {
        case OrthancPluginPixelFormat_RGB24:
          format = Orthanc::PixelFormat_RGB24;
          break;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
      }

      Orthanc::ImageAccessor source;
      source.AssignReadOnly(format, frame->GetWidth(), frame->GetHeight(), frame->GetPitch(), frame->GetBuffer());

      std::unique_ptr<Orthanc::ImageAccessor> copy(new Orthanc::Image(Orthanc::PixelFormat_RGB24, source.GetWidth(), source.GetHeight(), false));
      Orthanc::ImageProcessing::Convert(*copy, source);

      return new OnTheFlyPyramid(copy.release(), 512, 512, smooth_);
    }
  };
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
  

  OrthancWSI::DicomPyramidCache::Locker locker(seriesId);

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

  {
    // New in WSI 2.1
    sprintf(tmp, "#%02x%02x%02x", locker.GetPyramid().GetBackgroundRed(),
            locker.GetPyramid().GetBackgroundGreen(),
            locker.GetPyramid().GetBackgroundBlue());
    result["BackgroundColor"] = tmp;
  }

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
  std::unique_ptr<OrthancWSI::RawTile> rawTile;

  {
    // NB: Don't call "rawTile" while the Locker is around, as
    // "Answer()" can be a costly operation.
    OrthancWSI::DicomPyramidCache::Locker locker(seriesId);

    rawTile.reset(new OrthancWSI::RawTile(locker.GetPyramid(),
                                          static_cast<unsigned int>(level),
                                          static_cast<unsigned int>(tileX),
                                          static_cast<unsigned int>(tileY)));
  }

  if (rawTile->IsEmpty())
  {
    OrthancWSI::RawTile::AnswerBackgroundTile(output, rawTile->GetTileWidth(), rawTile->GetTileHeight());
    return;
  }

  Orthanc::MimeType mime;

  if (rawTile->GetCompression() == OrthancWSI::ImageCompression_Jpeg)
  {
    // The tile is already a JPEG image. In such a case, we can
    // serve it as such, because any Web browser can handle JPEG.
    mime = Orthanc::MimeType_Jpeg;
  }
  else
  {
    // This is a lossless frame (coming from JPEG2000 or uncompressed
    // DICOM instance), not a DICOM-JPEG instance. Decompress the raw
    // tile, then transcode it to PNG to prevent lossy compression and
    // to avoid JPEG2000 that is not supported by all the browsers.
    mime = Orthanc::MimeType_Png;
  }

  // Lookup whether a "Accept" HTTP header is present, to overwrite
  // the default MIME type
  for (uint32_t i = 0; i < request->headersCount; i++)
  {
    std::string key(request->headersKeys[i]);
    Orthanc::Toolbox::ToLowerCase(key);

    if (key == "accept")
    {
      std::vector<std::string> tokens;
      Orthanc::Toolbox::TokenizeString(tokens, request->headersValues[i], ',');

      bool found = false;

      for (size_t j = 0; j < tokens.size(); j++)
      {
        std::string s = Orthanc::Toolbox::StripSpaces(tokens[j]);

        if (s == Orthanc::EnumerationToString(Orthanc::MimeType_Png))
        {
          mime = Orthanc::MimeType_Png;
          found = true;
        }
        else if (s == Orthanc::EnumerationToString(Orthanc::MimeType_Jpeg))
        {
          mime = Orthanc::MimeType_Jpeg;
          found = true;
        }
        else if (s == Orthanc::EnumerationToString(Orthanc::MimeType_Jpeg2000))
        {
          mime = Orthanc::MimeType_Jpeg2000;
          found = true;
        }
        else if (s == "*/*" ||
                 s == "image/*")
        {
          found = true;
        }
      }

      if (!found)
      {
        OrthancPluginSendHttpStatusCode(OrthancPlugins::GetGlobalContext(), output, 406 /* Not acceptable */);
        return;
      }
    }
  }

  rawTile->Answer(output, mime);
}


void ServeFramePyramid(OrthancPluginRestOutput* output,
                       const char* url,
                       const OrthancPluginHttpRequest* request)
{
  std::string instanceId(request->groups[0]);
  int frameNumber = boost::lexical_cast<int>(request->groups[1]);

  LOG(INFO) << "Accessing pyramid of frame " << frameNumber << " in instance " << instanceId;

  if (frameNumber < 0)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
  }

  OrthancWSI::OnTheFlyPyramidsCache::Accessor accessor(OrthancWSI::OnTheFlyPyramidsCache::GetInstance(), instanceId, frameNumber);

  unsigned int totalWidth = accessor.GetPyramid().GetLevelWidth(0);
  unsigned int totalHeight = accessor.GetPyramid().GetLevelHeight(0);

  Json::Value sizes = Json::arrayValue;
  Json::Value resolutions = Json::arrayValue;
  Json::Value tilesCount = Json::arrayValue;
  Json::Value tilesSizes = Json::arrayValue;
  for (unsigned int i = 0; i < accessor.GetPyramid().GetLevelCount(); i++)
  {
    const unsigned int levelWidth = accessor.GetPyramid().GetLevelWidth(i);
    const unsigned int levelHeight = accessor.GetPyramid().GetLevelHeight(i);
    const unsigned int tileWidth = accessor.GetPyramid().GetTileWidth(i);
    const unsigned int tileHeight = accessor.GetPyramid().GetTileHeight(i);

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
  result["ID"] = instanceId;
  result["FrameNumber"] = frameNumber;
  result["Resolutions"] = resolutions;
  result["Sizes"] = sizes;
  result["TilesCount"] = tilesCount;
  result["TilesSizes"] = tilesSizes;
  result["TotalHeight"] = totalHeight;
  result["TotalWidth"] = totalWidth;

  {
    uint8_t red, green, blue;
    accessor.GetPyramid().GetBackgroundColor(red, green, blue);  // TODO

    char tmp[64];
    sprintf(tmp, "#%02x%02x%02x", red, green, blue);
    result["BackgroundColor"] = tmp;
  }

  std::string s = result.toStyledString();
  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, s.c_str(), s.size(), "application/json");
}


void ServeFrameTile(OrthancPluginRestOutput* output,
                    const char* url,
                    const OrthancPluginHttpRequest* request)
{
  std::string instanceId(request->groups[0]);
  int frameNumber = boost::lexical_cast<int>(request->groups[1]);
  int level = boost::lexical_cast<int>(request->groups[2]);
  int tileY = boost::lexical_cast<int>(request->groups[4]);
  int tileX = boost::lexical_cast<int>(request->groups[3]);

  char tmp[1024];
  sprintf(tmp, "Accessing on-the-fly tile in frame %d of instance %s: (%d,%d) at level %d", frameNumber, instanceId.c_str(), tileX, tileY, level);
  OrthancPluginLogInfo(OrthancPlugins::GetGlobalContext(), tmp);

  if (frameNumber < 0 ||
      level < 0 ||
      tileX < 0 ||
      tileY < 0)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
  }

  std::unique_ptr<Orthanc::ImageAccessor> tile;

  {
    OrthancWSI::OnTheFlyPyramidsCache::Accessor accessor(OrthancWSI::OnTheFlyPyramidsCache::GetInstance(), instanceId, frameNumber);
    if (!accessor.IsValid())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }

    bool isEmpty;  // Ignored
    tile.reset(accessor.GetPyramid().DecodeTile(isEmpty, level, tileX, tileY));
  }

  Orthanc::MimeType mime = Orthanc::MimeType_Png;  // By default, use lossless compression

  // Lookup whether a "Accept" HTTP header is present, to overwrite
  // the default MIME type
  for (uint32_t i = 0; i < request->headersCount; i++)
  {
    std::string key(request->headersKeys[i]);
    Orthanc::Toolbox::ToLowerCase(key);

    if (key == "accept")
    {
      std::vector<std::string> tokens;
      Orthanc::Toolbox::TokenizeString(tokens, request->headersValues[i], ',');

      bool found = false;

      for (size_t j = 0; j < tokens.size(); j++)
      {
        std::string s = Orthanc::Toolbox::StripSpaces(tokens[j]);

        if (s == Orthanc::EnumerationToString(Orthanc::MimeType_Png))
        {
          mime = Orthanc::MimeType_Png;
          found = true;
        }
        else if (s == Orthanc::EnumerationToString(Orthanc::MimeType_Jpeg))
        {
          mime = Orthanc::MimeType_Jpeg;
          found = true;
        }
        else if (s == Orthanc::EnumerationToString(Orthanc::MimeType_Jpeg2000))
        {
          mime = Orthanc::MimeType_Jpeg2000;
          found = true;
        }
        else if (s == "*/*" ||
                 s == "image/*")
        {
          found = true;
        }
      }

      if (!found)
      {
        OrthancPluginSendHttpStatusCode(OrthancPlugins::GetGlobalContext(), output, 406 /* Not acceptable */);
        return;
      }
    }
  }

  std::string encoded;
  OrthancWSI::ImageToolbox::EncodeTile(encoded, *tile, OrthancWSI::ImageToolbox::Convert(mime), 90 /* only used for JPEG */);

  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, encoded.c_str(),
                            encoded.size(), Orthanc::EnumerationToString(mime));
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

    OrthancWSI::DicomPyramidCache::GetInstance().Invalidate(resourceId);
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
  else if (f == "mirador.html")
  {
    resource = Orthanc::EmbeddedResources::MIRADOR_HTML;
    mime = "text/html";
  }
  else if (f == "openseadragon.html")
  {
    resource = Orthanc::EmbeddedResources::OPEN_SEADRAGON_HTML;
    mime = "text/html";
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
    OrthancPlugins::SetGlobalContext(context, ORTHANC_PLUGIN_NAME);
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

#if ORTHANC_FRAMEWORK_VERSION_IS_ABOVE(1, 12, 4)
    Orthanc::Logging::InitializePluginContext(context, ORTHANC_PLUGIN_NAME);
#elif ORTHANC_FRAMEWORK_VERSION_IS_ABOVE(1, 7, 2)
    Orthanc::Logging::InitializePluginContext(context);
#else
    Orthanc::Logging::Initialize(context);
#endif

    // Limit the number of PNG transcoders to the number of available
    // hardware threads (e.g. number of CPUs or cores or
    // hyperthreading units)
    unsigned int threads = Orthanc::SystemToolbox::GetHardwareConcurrency();
    OrthancWSI::RawTile::InitializeTranscoderSemaphore(threads);

    char info[1024];
    sprintf(info, "The whole-slide imaging plugin will use at most %u threads to transcode the tiles", threads);
    OrthancPluginLogWarning(OrthancPlugins::GetGlobalContext(), info);

    OrthancPlugins::SetDescription(ORTHANC_PLUGIN_NAME, "Provides a Web viewer of whole-slide microscopic images within Orthanc.");

    OrthancWSI::DicomPyramidCache::InitializeInstance(10 /* Number of pyramids to be cached - TODO parameter */);
    OrthancWSI::OnTheFlyPyramidsCache::InitializeInstance(
      new OrthancWSI::OrthancPyramidFrameFetcher(new OrthancWSI::OrthancPluginConnection(), false /* TODO PARAMETER */),
      10 /* TODO - PARAMETER */,
      256 * 1024 * 1024 /* TODO - PARAMETER */);

    OrthancPluginRegisterOnChangeCallback(OrthancPlugins::GetGlobalContext(), OnChangeCallback);

    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(ol.css)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(ol.js)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(viewer.html)", true);
    OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(viewer.js)", true);
    OrthancPlugins::RegisterRestCallback<ServePyramid>("/wsi/pyramids/([0-9a-f-]+)", true);
    OrthancPlugins::RegisterRestCallback<ServeTile>("/wsi/tiles/([0-9a-f-]+)/([0-9-]+)/([0-9-]+)/([0-9-]+)", true);
    OrthancPlugins::RegisterRestCallback<ServeFramePyramid>("/wsi/frames-pyramids/([0-9a-f-]+)/([0-9-]+)", true);
    OrthancPlugins::RegisterRestCallback<ServeFrameTile>("/wsi/frames-tiles/([0-9a-f-]+)/([0-9-]+)/([0-9-]+)/([0-9-]+)/([0-9-]+)", true);

    OrthancPlugins::OrthancConfiguration mainConfiguration;

    OrthancPlugins::OrthancConfiguration wsiConfiguration;
    mainConfiguration.GetSection(wsiConfiguration, "WholeSlideImaging");

    const bool enableIIIF = wsiConfiguration.GetBooleanValue("EnableIIIF", true);
    bool serveMirador = false;
    bool serveOpenSeadragon = false;
    std::string iiifPublicUrl;

    if (enableIIIF)
    {
      if (!wsiConfiguration.LookupStringValue(iiifPublicUrl, "OrthancPublicURL"))
      {
        unsigned int port = mainConfiguration.GetUnsignedIntegerValue("HttpPort", 8042);
        iiifPublicUrl = "http://localhost:" + boost::lexical_cast<std::string>(port) + "/";
      }

      if (iiifPublicUrl.empty() ||
          iiifPublicUrl[iiifPublicUrl.size() - 1] != '/')
      {
        iiifPublicUrl += "/";
      }

      iiifPublicUrl += "wsi/iiif/";

      InitializeIIIF(iiifPublicUrl);

      serveMirador = wsiConfiguration.GetBooleanValue("ServeMirador", false);
      serveOpenSeadragon = wsiConfiguration.GetBooleanValue("ServeOpenSeadragon", false);

      bool value;
      if (wsiConfiguration.LookupBooleanValue(value, "ForcePowersOfTwoScaleFactors"))
      {
        SetIIIFForcePowersOfTwoScaleFactors(value);
      }
      else
      {
        /**
         * By default, compatibility mode is disabled. However, if
         * Mirador or OSD are enabled, compatibility mode is
         * automatically enabled to enhance user experience, at least
         * until issue 2379 of OSD is solved:
         * https://github.com/openseadragon/openseadragon/issues/2379
         **/
        SetIIIFForcePowersOfTwoScaleFactors(serveMirador || serveOpenSeadragon);
      }
    }

    LOG(WARNING) << "Support of IIIF is " << (enableIIIF ? "enabled" : "disabled") << " in the whole-slide imaging plugin";

    if (serveMirador)
    {
      OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(mirador.html)", true);
    }

    if (serveOpenSeadragon)
    {
      OrthancPlugins::RegisterRestCallback<ServeFile>("/wsi/app/(openseadragon.html)", true);
    }

    {
      // Extend the default Orthanc Explorer with custom JavaScript for WSI

      std::string explorer;
      Orthanc::EmbeddedResources::GetFileResource(explorer, Orthanc::EmbeddedResources::ORTHANC_EXPLORER);

      std::map<std::string, std::string> dictionary;
      dictionary["ENABLE_IIIF"] = (enableIIIF ? "true" : "false");
      dictionary["SERVE_MIRADOR"] = (serveMirador ? "true" : "false");
      dictionary["SERVE_OPEN_SEADRAGON"] = (serveOpenSeadragon ? "true" : "false");
      explorer = Orthanc::Toolbox::SubstituteVariables(explorer, dictionary);

      OrthancPlugins::ExtendOrthancExplorer(ORTHANC_PLUGIN_NAME, explorer);
    }

    return 0;
  }


  ORTHANC_PLUGINS_API void OrthancPluginFinalize()
  {
    OrthancWSI::OnTheFlyPyramidsCache::FinalizeInstance();
    OrthancWSI::DicomPyramidCache::FinalizeInstance();
    OrthancWSI::RawTile::FinalizeTranscoderSemaphore();
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetName()
  {
    return ORTHANC_PLUGIN_NAME;
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetVersion()
  {
    return ORTHANC_WSI_VERSION;
  }
}
