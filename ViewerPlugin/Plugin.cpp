/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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
#include "IAuthenticatedUser.h"
#include "IIIF.h"
#include "OrthancPyramidFrameFetcher.h"
#include "RawTile.h"

#include "../Framework/ColorSpaces.h"
#include "../Framework/ImageToolbox.h"
#include "../Framework/Inputs/DecodedPyramidCache.h"
#include "../Framework/Inputs/DecodedTiledPyramid.h"
#include "../Framework/Inputs/OnTheFlyPyramid.h"

#include <Compatibility.h>  // For std::unique_ptr
#include <Compression/GzipCompressor.h>
#include <Images/Image.h>
#include <Images/ImageProcessing.h>
#include <Logging.h>
#include <OrthancException.h>
#include <SerializationToolbox.h>
#include <SystemToolbox.h>

#include "../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <EmbeddedResources.h>

#include <cassert>


#include "OrthancPluginConnection.h"


#define ORTHANC_PLUGIN_NAME "wsi"


static bool DisplayPerformanceWarning()
{
  (void) DisplayPerformanceWarning;   // Disable warning about unused function
  OrthancPluginLogWarning(OrthancPlugins::GetGlobalContext(), "Performance warning in whole-slide imaging: "
                          "Non-release build, runtime debug assertions are turned on");
  return true;
}


static void DescribePyramid(Json::Value& result,
                            const OrthancWSI::ITiledPyramid& pyramid)
{
  unsigned int totalWidth = pyramid.GetLevelWidth(0);
  unsigned int totalHeight = pyramid.GetLevelHeight(0);

  Json::Value sizes = Json::arrayValue;
  Json::Value resolutions = Json::arrayValue;
  Json::Value tilesCount = Json::arrayValue;
  Json::Value tilesSizes = Json::arrayValue;
  for (unsigned int i = 0; i < pyramid.GetLevelCount(); i++)
  {
    const unsigned int levelWidth = pyramid.GetLevelWidth(i);
    const unsigned int levelHeight = pyramid.GetLevelHeight(i);
    const unsigned int tileWidth = pyramid.GetTileWidth(i);
    const unsigned int tileHeight = pyramid.GetTileHeight(i);

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

  result["Resolutions"] = resolutions;
  result["Sizes"] = sizes;
  result["TilesCount"] = tilesCount;
  result["TilesSizes"] = tilesSizes;
  result["TotalHeight"] = totalHeight;
  result["TotalWidth"] = totalWidth;
}


static void AnswerJson(OrthancPluginRestOutput* output,
                       const Json::Value& value)
{
  std::string s;
  Orthanc::Toolbox::WriteFastJson(s, value);
  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, s.c_str(), s.size(), "application/json");
}


static void AnswerEmpty(OrthancPluginRestOutput* output)
{
  Json::Value answer = Json::objectValue;
  AnswerJson(output, answer);
}


void ServePyramid(OrthancPluginRestOutput* output,
                  const char* url,
                  const OrthancPluginHttpRequest* request)
{
  std::string seriesId(request->groups[0]);

  LOG(INFO) << "Accessing whole-slide pyramid of series " << seriesId;

  Json::Value answer;
  answer["ID"] = seriesId;

  {
    OrthancWSI::DicomPyramidCache::Locker locker(seriesId);
    DescribePyramid(answer, locker.GetPyramid());

    {
      // New in WSI 2.1
      char tmp[16];
      sprintf(tmp, "#%02x%02x%02x", locker.GetPyramid().GetBackgroundRed(),
              locker.GetPyramid().GetBackgroundGreen(),
              locker.GetPyramid().GetBackgroundBlue());
      answer["BackgroundColor"] = tmp;
    }

    // New in WSI 3.1
    double imagedVolumeWidth, imagedVolumeHeight;
    if (locker.GetPyramid().LookupImagedVolumeSize(imagedVolumeWidth, imagedVolumeHeight))
    {
      answer["ImagedVolumeWidth"] = imagedVolumeWidth;
      answer["ImagedVolumeHeight"] = imagedVolumeHeight;
    }
  }

  AnswerJson(output, answer);
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

  Json::Value answer;
  answer["ID"] = instanceId;
  answer["FrameNumber"] = frameNumber;

  {
    OrthancWSI::DecodedPyramidCache::Accessor accessor(OrthancWSI::DecodedPyramidCache::GetInstance(), instanceId, frameNumber);
    DescribePyramid(answer, accessor.GetPyramid());

    {
      uint8_t red, green, blue;
      accessor.GetPyramid().GetBackgroundColor(red, green, blue);

      char tmp[16];
      sprintf(tmp, "#%02x%02x%02x", red, green, blue);
      answer["BackgroundColor"] = tmp;
    }
  }

  AnswerJson(output, answer);
}


static bool LookupAcceptHeader(Orthanc::MimeType& target,
                               const OrthancPluginHttpRequest* request)
{
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

      bool compatible = false;

      for (size_t j = 0; j < tokens.size(); j++)
      {
        std::string s = Orthanc::Toolbox::StripSpaces(tokens[j]);

        if (s == Orthanc::EnumerationToString(Orthanc::MimeType_Png))
        {
          target = Orthanc::MimeType_Png;
          return true;
        }
        else if (s == Orthanc::EnumerationToString(Orthanc::MimeType_Jpeg))
        {
          target = Orthanc::MimeType_Jpeg;
          return true;
        }
        else if (s == Orthanc::EnumerationToString(Orthanc::MimeType_Jpeg2000))
        {
          target = Orthanc::MimeType_Jpeg2000;
          return true;
        }
        else if (s == "*/*" ||
                 s == "image/*")
        {
          compatible = true;
        }
      }

      if (!compatible)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NotAcceptable);
      }
    }
  }

  return false;
}


void ServeTile(OrthancPluginRestOutput* output,
               const char* url,
               const OrthancPluginHttpRequest* request)
{
  std::string seriesId(request->groups[0]);
  int level = boost::lexical_cast<int>(request->groups[1]);
  int tileY = boost::lexical_cast<int>(request->groups[3]);
  int tileX = boost::lexical_cast<int>(request->groups[2]);

  LOG(INFO) << "Accessing tile in series " << seriesId << ": (" << tileX << "," << tileY << ") at level " << level;

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

  Orthanc::MimeType accept;
  if (LookupAcceptHeader(accept, request))
  {
    mime = accept;
  }

  rawTile->Answer(output, mime);
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

  LOG(INFO) << "Accessing on-the-fly tile in frame " << frameNumber << " of instance " << instanceId <<": (" << tileX << "," << tileY << ") at level " << level;

  if (frameNumber < 0 ||
      level < 0 ||
      tileX < 0 ||
      tileY < 0)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
  }

  std::unique_ptr<Orthanc::ImageAccessor> tile;

  {
    OrthancWSI::DecodedPyramidCache::Accessor accessor(OrthancWSI::DecodedPyramidCache::GetInstance(), instanceId, frameNumber);
    if (!accessor.IsValid())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }

    bool isEmpty;  // Ignored
    tile.reset(accessor.GetPyramid().DecodeTile(isEmpty, level, tileX, tileY));
  }

  Orthanc::MimeType mime;
  if (!LookupAcceptHeader(mime, request))
  {
    mime = Orthanc::MimeType_Png;  // By default, use lossless compression
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
    LOG(INFO) << "New instance has been added to series " << resourceId << ", invalidating it";
    OrthancWSI::DicomPyramidCache::GetInstance().Invalidate(resourceId);
  }

  return OrthancPluginErrorCode_Success;
}


void ServeJavaScriptLibraries(OrthancPluginRestOutput* output,
                              const char* url,
                              const OrthancPluginHttpRequest* request)
{
  OrthancPluginContext* context = OrthancPlugins::GetGlobalContext();

  if (request->method != OrthancPluginHttpMethod_Get)
  {
    OrthancPluginSendMethodNotAllowed(context, output, "GET");
  }
  else
  {
    const std::string path = "/" + std::string(request->groups[0]);
    const char* mime = Orthanc::EnumerationToString(Orthanc::SystemToolbox::AutodetectMimeType(path));

    if (path == "/js/ol.js")
    {
      // Adding "charset" is mandatory with OpenLayers 10.4.0, check out "zoomOutLabel" in the source code
      mime = "application/javascript; charset=utf-8";
    }

    std::string s;
    Orthanc::EmbeddedResources::GetDirectoryResource(s, Orthanc::EmbeddedResources::JAVASCRIPT_LIBS, path.c_str());

    const char* resource = s.size() ? s.c_str() : NULL;
    OrthancPluginAnswerBuffer(context, output, resource, s.size(), mime);
  }
}


void ServeEmbeddedFile(OrthancPluginRestOutput* output,
                       const char* url,
                       const OrthancPluginHttpRequest* request)
{
  Orthanc::EmbeddedResources::FileResourceId resource;

  std::string f(request->groups[0]);
  std::string mime;

  if (f == "viewer.html")
  {
#if ORTHANC_STANDALONE == 0
    throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
#else
    resource = Orthanc::EmbeddedResources::VIEWER_HTML;
#endif

    mime = "text/html";
  }
  else if (f == "viewer.js")
  {
#if ORTHANC_STANDALONE == 0
    throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
#else
    resource = Orthanc::EmbeddedResources::VIEWER_JS;
#endif

    mime = "application/javascript";
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
  else if (f == "annotations.js")
  {
#if ORTHANC_STANDALONE == 0
    throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
#else
    resource = Orthanc::EmbeddedResources::ANNOTATIONS_JS;
#endif

    mime = "application/javascript";
  }
  else
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
  }

  std::string content;
  Orthanc::EmbeddedResources::GetFileResource(content, resource);

  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, content.c_str(), content.size(), mime.c_str());
}


#if ORTHANC_STANDALONE == 0
void ServeSourceFile(OrthancPluginRestOutput* output,
                     const char* url,
                     const OrthancPluginHttpRequest* request)
{
  // This method should only be used during the development to speed up compilation

  std::string filename(request->groups[0]);

  if (filename != "annotations.js" &&
      filename != "viewer.html" &&
      filename != "viewer.js")
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
  }

  const boost::filesystem::path path = Orthanc::SystemToolbox::InterpretRelativePath(
    Orthanc::SystemToolbox::PathFromUtf8(PLUGIN_SOURCE_DIR) / "WebApplication", filename);
  const char* mime = Orthanc::EnumerationToString(Orthanc::SystemToolbox::AutodetectMimeType(filename));

  std::string content;
  Orthanc::SystemToolbox::ReadFile(content, path);

  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, content.c_str(), content.size(), mime);
}
#endif



#include <Cache/SharedObjectCache.h>
#include <MultiThreading/ReaderWriterLock.h>


class AnnotationsId
{
private:
  std::string            projectId_;
  Orthanc::ResourceType  level_;
  std::string            resourceId_;
  unsigned int           frameNumber_;

public:
  AnnotationsId(const std::string& projectId,
                Orthanc::ResourceType level,
                const std::string& resourceId,
                unsigned int frameNumber) :
    projectId_(projectId),
    level_(level),
    resourceId_(resourceId),
    frameNumber_(frameNumber)
  {
    if (level_ != Orthanc::ResourceType_Series &&
        level_ != Orthanc::ResourceType_Instance)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    if (projectId_.find('|') != std::string::npos ||
        resourceId_.find('|') != std::string::npos)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }

  const std::string& GetProjectId() const
  {
    return projectId_;
  }

  Orthanc::ResourceType GetLevel() const
  {
    return level_;
  }

  const std::string& GetResourceId() const
  {
    return resourceId_;
  }

  std::string GetKey() const
  {
    switch (level_)
    {
    case Orthanc::ResourceType_Series:
      return projectId_ + "|series|" + resourceId_;

    case Orthanc::ResourceType_Instance:
      return projectId_ + "|instance|" + boost::lexical_cast<std::string>(frameNumber_) + "|" + resourceId_;

    default:
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
  }
};


static const char* const KEY_AUTHOR = "author";
static const char* const KEY_COLOR = "color";
static const char* const KEY_FEATURES = "features";
static const char* const KEY_ID = "id";
static const char* const KEY_LAYERS = "layers";
static const char* const KEY_NAME = "name";
static const char* const KEY_PUBLIC = "public";
static const char* const KEY_SHARED_WITH = "shared_with";
static const char* const KEY_VISIBLE = "visible";

static const char* const KEY_VALUE_STORE = "wsi";


/**

   Content of a layer in the DB:

   - visible             (editable)
   - color               (editable)
   - author type and ID  (read-only)
   - layer ID            (read-only)
   - name of the layer   (editable)
   - shared_with         (editable, can be disabled for learners by the instructors)
   - public              (editable, can be disabled for learners by the instructors)

   Content of an imported shared layer in the DB:

   - visible             (editable, set to "true" on import)
   - color               (editable, can be different from original layer)
   - author type and ID  (read-only)
   - layer ID            (read-only)
   - name of the layer   (read-only)

   Finding shared layers:

   map<UserId, std::list<UserLayer>>

   map<UserId, std::list<std::pair<UserId, const UserLayer*>>>

   API must return: color, author, layer ID, and name (visible is set to true once loaded)

 **/


class ISerializable : public boost::noncopyable
{
public:
  virtual ~ISerializable()
  {
  }

  virtual void Serialize(Json::Value& target) const = 0;

  static void Serialize(std::string& target,
                        const ISerializable& obj)
  {
    Json::Value value;
    obj.Serialize(value);
    Orthanc::Toolbox::WriteFastJson(target, value);
  }
};


class ILayer : public ISerializable
{
public:
  virtual std::string GetId() const = 0;
};


class LayersCollection : public ISerializable
{
private:
  typedef std::list<ILayer*>                        Content;
  typedef std::map<std::string, Content::iterator>  Index;

  Content  content_;
  Index    index_;

public:
  ~LayersCollection()
  {
    for (Content::iterator it = content_.begin(); it != content_.end(); ++it)
    {
      assert(*it != NULL);
      delete *it;
    }
  }

  size_t GetSize() const
  {
    assert(content_.size() == index_.size());
    return content_.size();
  }

  void AddLayer(ILayer* layer /* takes ownership */)
  {
    std::unique_ptr<ILayer> protection(layer);

    if (layer == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }

    const std::string id = protection->GetId();

    if (index_.find(id) != index_.end())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls, "Duplicate layer ID");
    }
    else
    {
      content_.push_back(protection.release());

      Content::iterator it = content_.end();
      --it;  // Points to the element we just inserted
      index_[id] = it;
    }
  }

  bool HasLayer(const std::string& id) const
  {
    return (index_.find(id) != index_.end());
  }

  ILayer& GetLayer(const std::string& id) const
  {
    Index::const_iterator found = index_.find(id);

    if (found == index_.end())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }
    else
    {
      assert(*(found->second) != NULL);
      return **(found->second);
    }
  }

  void DeleteLayer(const std::string& id)
  {
    Index::iterator found = index_.find(id);

    if (found == index_.end())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }
    else
    {
      assert(*(found->second) != NULL);
      delete *(found->second);
      content_.erase(found->second);
      index_.erase(found);
    }
  }

  virtual void Serialize(Json::Value& target) const ORTHANC_OVERRIDE
  {
    target = Json::arrayValue;

    for (Content::const_iterator it = content_.begin(); it != content_.end(); ++it)
    {
      assert(*it != NULL);

      Json::Value item;
      (*it)->Serialize(item);
      target.append(item);
    }
  }
};


static void SetKeyValueStore(const std::string& key,
                             const std::string& value)
{
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
  OrthancPlugins::KeyValueStore store(KEY_VALUE_STORE);
  store.Store(key, value);
#endif
}


static void SetKeyValueStore(const std::string& key,
                             const Json::Value& value)
{
  std::string s;
  Orthanc::Toolbox::WriteFastJson(s, value);
  SetKeyValueStore(key, s);
}


static void SetKeyValueStore(const std::string& key,
                             const ISerializable& value)
{
  std::string s;
  ISerializable::Serialize(s, value);
  SetKeyValueStore(key, s);
}


static bool LookupKeyValueStore(std::string& value,
                                const std::string& key)
{
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
  OrthancPlugins::KeyValueStore store(KEY_VALUE_STORE);
  return store.GetValue(value, key);
#else
  return false;
#endif
}


static bool LookupKeyValueStore(Json::Value& value,
                                const std::string& key)
{
  std::string s;
  if (LookupKeyValueStore(s, key))
  {
    if (Orthanc::Toolbox::ReadJson(value, s))
    {
      return true;
    }
    else
    {
      LOG(WARNING) << "Discarding incorrect JSON in the key-value store: " << key;
      return false;
    }
  }
  else
  {
    return false;
  }
}


static std::string GetInfoKey(const AnnotationsId& annotations)
{
  return annotations.GetKey() + "|info";
}


static std::string GetLayersKey(const AnnotationsId& annotations,
                                const UserId& user)
{
  return annotations.GetKey() + "|layers|" + user.GetKey();
}


static std::string GetFeaturesKey(const AnnotationsId& annotations,
                                  const UserId& user)
{
  return annotations.GetKey() + "|features|" + user.GetKey();
}



static const char* const KEY_ACTIVE_USERS = "active-users";
static const char* const KEY_PROJECT_NAME = "project-name";
static const char* const KEY_PROJECT_DESCRIPTION = "project-description";
static const char* const KEY_USER_LAYERS = "user-layers";
static const char* const KEY_SHARED_LAYERS = "shared-layers";


static unsigned int GetHex(char c)
{
  if (c >= '0' && c <= '9')
  {
    return c - '0';
  }
  else if (c >= 'a' && c <= 'f')
  {
    return c - 'a' + 10;
  }
  else if (c >= 'A' && c <= 'F')
  {
    return c - 'A' + 10;
  }
  else
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
  }
}


static OrthancWSI::RGBColor ParseColor(const std::string& color)
{
  if (color.size() != 7 ||
      color[0] != '#')
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
  }
  else
  {
    unsigned int r = GetHex(color[1]) * 16 + GetHex(color[2]);
    unsigned int g = GetHex(color[3]) * 16 + GetHex(color[4]);
    unsigned int b = GetHex(color[5]) * 16 + GetHex(color[6]);
    return OrthancWSI::RGBColor(r, g, b);
  }
}


static std::string SerializeColor(const OrthancWSI::RGBColor& color)
{
  char buf[16];
  sprintf(buf, "#%02x%02x%02x", color.GetR(), color.GetG(), color.GetB());
  return buf;
}


class UserLayer : public ILayer
{
private:
  bool                   isVisible_;
  OrthancWSI::RGBColor   color_;
  std::string            id_;
  std::string            name_;
  std::set<UserId>       sharedWith_;
  bool                   isPublic_;

public:
  UserLayer(const OrthancWSI::RGBColor& color,
            const std::string& name) :
    isVisible_(true),
    color_(color),
    id_(Orthanc::Toolbox::GenerateUuid()),
    name_(name),
    isPublic_(false)
  {
  }

  UserLayer(const Json::Value& source) :
    color_(ParseColor(Orthanc::SerializationToolbox::ReadString(source, KEY_COLOR)))
  {
    isVisible_ = Orthanc::SerializationToolbox::ReadBoolean(source, KEY_VISIBLE);
    id_ = Orthanc::SerializationToolbox::ReadString(source, KEY_ID);
    name_ = Orthanc::SerializationToolbox::ReadString(source, KEY_NAME);

    if (source.isMember(KEY_PUBLIC))
    {
      isPublic_ = Orthanc::SerializationToolbox::ReadBoolean(source, KEY_PUBLIC);
    }
    else
    {
      isPublic_ = false;
    }

    if (source.isMember(KEY_SHARED_WITH))
    {
      const Json::Value& sharedWith = source[KEY_SHARED_WITH];

      if (!sharedWith.isArray())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
      }

      for (Json::Value::ArrayIndex i = 0; i < sharedWith.size(); i++)
      {
        sharedWith_.insert(UserId(sharedWith[i]));
      }
    }
  }

  virtual std::string GetId() const ORTHANC_OVERRIDE
  {
    return id_;
  }

  bool IsVisible() const
  {
    return isVisible_;
  }

  const OrthancWSI::RGBColor& GetColor() const
  {
    return color_;
  }

  const std::string& GetName() const
  {
    return name_;
  }

  bool IsSharedWith(const UserId& user) const
  {
    return (isPublic_ ||
            sharedWith_.find(user) != sharedWith_.end());
  }

  void Assign(const UserLayer& other)
  {
    isVisible_ = other.isVisible_;
    color_ = other.color_;
    name_ = other.name_;
    sharedWith_ = other.sharedWith_;
    isPublic_ = other.isPublic_;
  }

  virtual void Serialize(Json::Value& target) const ORTHANC_OVERRIDE
  {
    Json::Value sharedWith = Json::arrayValue;
    for (std::set<UserId>::const_iterator it = sharedWith_.begin(); it != sharedWith_.end(); ++it)
    {
      Json::Value item;
      it->Serialize(item);
      sharedWith.append(item);
    }

    target = Json::objectValue;
    target[KEY_VISIBLE] = isVisible_;
    target[KEY_COLOR] = SerializeColor(color_);
    target[KEY_ID] = id_;
    target[KEY_NAME] = name_;
    target[KEY_PUBLIC] = isPublic_;
    target[KEY_SHARED_WITH] = sharedWith;
  }
};


class SharedLayer : public ILayer
{
private:
  bool                  isVisible_;
  OrthancWSI::RGBColor  color_;
  UserId                author_;
  std::string           id_;
  std::string           name_;

public:
  SharedLayer(const UserId& author,
              const UserLayer& layer) :
    isVisible_(true),
    color_(layer.GetColor()),
    author_(author),
    id_(layer.GetId()),
    name_(layer.GetName())
  {
  }

  SharedLayer(const Json::Value& source) :
    color_(0, 0, 0)
  {
    if (!source.isMember(KEY_AUTHOR))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
    }

    isVisible_ = Orthanc::SerializationToolbox::ReadBoolean(source, KEY_VISIBLE);
    author_ = UserId(source[KEY_AUTHOR]);
    id_ = Orthanc::SerializationToolbox::ReadString(source, KEY_ID);
    name_ = Orthanc::SerializationToolbox::ReadString(source, KEY_NAME);

    std::string color = Orthanc::SerializationToolbox::ReadString(source, KEY_COLOR);
    color_ = ParseColor(color);
  }

  virtual std::string GetId() const ORTHANC_OVERRIDE
  {
    return id_;
  }

  bool IsVisible() const
  {
    return isVisible_;
  }

  const OrthancWSI::RGBColor& GetColor() const
  {
    return color_;
  }

  const UserId& GetAuthor() const
  {
    return author_;
  }

  const std::string& GetName() const
  {
    return name_;
  }

  virtual void Serialize(Json::Value& target) const ORTHANC_OVERRIDE
  {
    target = Json::objectValue;
    target[KEY_VISIBLE] = isVisible_;
    target[KEY_COLOR] = SerializeColor(color_);
    target[KEY_ID] = id_;
    target[KEY_NAME] = name_;

    author_.Serialize(target[KEY_AUTHOR]);
  }
};


class UserData : public ISerializable
{
private:
  LayersCollection    userLayers_;

public:
  UserData()
  {
  }

  UserData(const Json::Value& source)
  {
    if (!source.isArray())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }
    else
    {
      for (Json::Value::ArrayIndex i = 0; i < source.size(); i++)
      {
        userLayers_.AddLayer(new UserLayer(source[i]));
      }
    }
  }

  std::string AddUserLayer(UserLayer* layer)
  {
    std::unique_ptr<UserLayer> protection(layer);

    if (layer == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }

    const std::string id = protection->GetId();

    userLayers_.AddLayer(protection.release());

    return id;
  }

  UserLayer& GetUserLayer(const std::string& layerId) const
  {
    return dynamic_cast<UserLayer&>(userLayers_.GetLayer(layerId));
  }

  std::string CreateUserLayer()
  {
    static const uint8_t PALETTE[] = {
      0xe6, 0x39, 0x46,  // red: #e63946
      0x2a, 0x9d, 0x8f,
      0xe9, 0xc4, 0x6a,
      0x26, 0x46, 0x53,
      0xf4, 0xa2, 0x61
    };

    static const size_t PALETTE_SIZE = sizeof(PALETTE) / (3 * sizeof(uint8_t));

    size_t item = userLayers_.GetSize() % PALETTE_SIZE;

    OrthancWSI::RGBColor color(PALETTE[3 * item],
                               PALETTE[3 * item + 1],
                               PALETTE[3 * item + 2]);

    std::string name;
    if (userLayers_.GetSize() == 0)
    {
      name = "Default";
    }
    else
    {
      name = "Layer " + boost::lexical_cast<std::string>(userLayers_.GetSize() + 1);
    }

    return AddUserLayer(new UserLayer(color, name));
  }

  void DeleteUserLayer(const std::string& layerId)
  {
    userLayers_.DeleteLayer(layerId);
  }

  virtual void Serialize(Json::Value& target) const ORTHANC_OVERRIDE
  {
    userLayers_.Serialize(target);
  }
};


class AnnotationsInfo : public ISerializable
{
private:
  std::string        projectName_;
  std::string        projectDescription_;
  std::set<UserId>   activeUsers_;

public:
  AnnotationsInfo()
  {
  }

  AnnotationsInfo(const Json::Value& source)
  {
    projectName_ = Orthanc::SerializationToolbox::ReadString(source, KEY_PROJECT_NAME);
    projectDescription_ = Orthanc::SerializationToolbox::ReadString(source, KEY_PROJECT_DESCRIPTION);

    const Json::Value& users = source[KEY_ACTIVE_USERS];

    if (!users.isArray())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }

    for (Json::Value::ArrayIndex i = 0; i < users.size(); i++)
    {
      activeUsers_.insert(UserId(users[i]));
    }
  }

  const std::string& GetProjectName() const
  {
    return projectName_;
  }

  void SetProjectName(const std::string& name)
  {
    projectName_ = name;
  }

  const std::string& GetProjectDescription() const
  {
    return projectDescription_;
  }

  void SetProjectDescription(const std::string& description)
  {
    projectDescription_ = description;
  }

  // Return "true" iff. the user was not already tagged as active
  bool AddActiveUser(const UserId& user)
  {
    if (activeUsers_.find(user) == activeUsers_.end())
    {
      activeUsers_.insert(user);
      return true;
    }
    else
    {
      return false;
    }
  }

  const std::set<UserId>& GetActiveUsers() const
  {
    return activeUsers_;
  }

  virtual void Serialize(Json::Value& target) const ORTHANC_OVERRIDE
  {
    Json::Value users = Json::arrayValue;
    for (std::set<UserId>::const_iterator it = activeUsers_.begin(); it != activeUsers_.end(); ++it)
    {
      Json::Value user;
      it->Serialize(user);
      users.append(user);
    }

    target = Json::objectValue;
    target[KEY_PROJECT_NAME] = projectName_;
    target[KEY_PROJECT_DESCRIPTION] = projectDescription_;
    target[KEY_ACTIVE_USERS] = users;
  }
};


class CachedAnnotations : public Orthanc::IDynamicObject
{
private:
  void LoadUserData(const UserId& user)
  {
    const std::string key = id_.GetKey() + "|layers|" + user.GetKey();

    Json::Value layers;
    if (LookupKeyValueStore(layers, key))
    {
      std::unique_ptr<UserData> item(new UserData(layers));

      if (content_.find(user) == content_.end())  // Should never be false
      {
        content_[user] = item.release();
      }
    }
  }

  typedef std::map<UserId, UserData*>   Content;

  Orthanc::ReaderWriterLock         mutex_;
  std::unique_ptr<AnnotationsInfo>  info_;
  AnnotationsId                     id_;
  Content                           content_;

public:
  CachedAnnotations(const AnnotationsId& id) :
    id_(id)
  {
    const std::string key = GetInfoKey(id);

    Json::Value info;

    if (LookupKeyValueStore(info, key))
    {
      info_.reset(new AnnotationsInfo(info));

      for (std::set<UserId>::const_iterator it = info_->GetActiveUsers().begin();
           it != info_->GetActiveUsers().end(); ++it)
      {
        LoadUserData(*it);
      }
    }
    else
    {
      info_.reset(new AnnotationsInfo);

      if (OrthancPlugins::RestApiGet(info, "/education/api-plugins/project?id=" + id.GetProjectId(), true))
      {
        // The "orthanc-education" plugin is available
        info_->SetProjectName(Orthanc::SerializationToolbox::ReadString(info, "name"));
        info_->SetProjectDescription(Orthanc::SerializationToolbox::ReadString(info, "description"));
      }

      info_->Serialize(info);
      SetKeyValueStore(key, info);
    }
  }

  ~CachedAnnotations()
  {
    for (Content::iterator it = content_.begin(); it != content_.end(); ++it)
    {
      assert(it->second != NULL);
      delete it->second;
    }
  }

  class UserReader : public boost::noncopyable
  {
  private:
    Orthanc::ReaderWriterLock::ReadLock lock_;
    const AnnotationsInfo&              info_;
    const UserData*                     userData_;

  public:
    UserReader(CachedAnnotations& that,
               const IAuthenticatedUser& user) :
      lock_(that.mutex_),
      info_(*that.info_)
    {
      Content::const_iterator found = that.content_.find(user.GetAnnotatingId());

      if (found == that.content_.end())
      {
        userData_ = NULL;
      }
      else
      {
        assert(found->second != NULL);
        userData_ = found->second;
      }
    }

    bool IsValid() const
    {
      return userData_ != NULL;
    }

    const AnnotationsInfo& GetAnnotationsInfo() const
    {
      return info_;
    }

    void ListLayers(Json::Value& target) const
    {
      target = Json::objectValue;

      if (IsValid())
      {
        userData_->Serialize(target[KEY_USER_LAYERS]);
        target[KEY_SHARED_LAYERS] = Json::arrayValue;  // TODO
      }
      else
      {
        target[KEY_USER_LAYERS] = Json::arrayValue;
        target[KEY_SHARED_LAYERS] = Json::arrayValue;
      }
    }
  };


  class UserWriter : public boost::noncopyable
  {
  private:
    Orthanc::ReaderWriterLock::WriteLock  lock_;
    CachedAnnotations&                    that_;
    UserId                                userId_;
    UserData*                             userData_;

    void Commit()
    {
      SetKeyValueStore(GetLayersKey(that_.id_, userId_), *userData_);
    }

  public:
    UserWriter(CachedAnnotations& that,
               const IAuthenticatedUser& user) :
      lock_(that.mutex_),
      that_(that),
      userId_(user.GetAnnotatingId())
    {
      const UserId id = user.GetAnnotatingId();

      if (that.info_->AddActiveUser(userId_))
      {
        // Only update the key-value store if this is the first time we meet this user
        SetKeyValueStore(GetInfoKey(that.id_), *that.info_);
      }

      Content::iterator found = that.content_.find(userId_);

      if (found == that.content_.end())
      {
        std::unique_ptr<UserData> layers(new UserData);
        userData_ = layers.get();
        that.content_[userId_] = layers.release();
        Commit();
      }
      else
      {
        assert(found->second != NULL);
        userData_ = found->second;
      }
    }

    void CreateUserLayer(Json::Value& answer)
    {
      assert(userData_ != NULL);

      const std::string layerId = userData_->CreateUserLayer();
      Commit();

      UserLayer& layer = userData_->GetUserLayer(layerId);
      layer.Serialize(answer);
    }

    void UpdateUserLayer(const UserLayer& updated)
    {
      assert(userData_ != NULL);

      UserLayer& layer = userData_->GetUserLayer(updated.GetId());
      layer.Assign(updated);
      Commit();
    }

    void DeleteUserLayer(const std::string& layerId)
    {
      assert(userData_ != NULL);
      userData_->DeleteUserLayer(layerId);
      Commit();
    }
  };


#if 0
  void GetSharedLayers(Json::Value& layers,
                       const IAuthenticatedUser& user)
  {
    Orthanc::ReaderWriterLock::ReadLock lock(mutex_);

    layers = Json::arrayValue;   // TODO
  }

  bool HasAccessToLayer(const IAuthenticatedUser& user,
                        const UserId& author,
                        const std::string& layerId)
  {
    Orthanc::ReaderWriterLock::ReadLock lock(mutex_);

    return false;  // TODO
  }

  void CreateUserLayer(const IAuthenticatedUser& user)
  {
    // TODO
  }

  void AnswerLayers(OrthancPluginRestOutput* output,
                    const IAuthenticatedUser& user)
  {

    AnswerJson(output, answer);
  }
#endif
};


class AnnotationsCommandContext : public boost::noncopyable
{
private:
  std::unique_ptr<IAuthenticatedUser>         user_;
  Json::Value                                 body_;
  std::unique_ptr<AnnotationsId>              annotationsId_;
  boost::shared_ptr<Orthanc::IDynamicObject>  cachedAnnotations_;

public:
  AnnotationsCommandContext(const OrthancPluginHttpRequest* request)
  {
    if (request->method != OrthancPluginHttpMethod_Post)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }

    user_.reset(IAuthenticatedUser::FromHttpRequest(request));

    if (!Orthanc::Toolbox::ReadJson(body_, request->body, request->bodySize) ||
        !body_.isObject())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
    }

    const std::string projectId = Orthanc::SerializationToolbox::ReadString(body_, "project", "" /* default project */);
    const std::string level = Orthanc::SerializationToolbox::ReadString(body_, "level");
    const std::string resourceId = Orthanc::SerializationToolbox::ReadString(body_, "resource");
    unsigned int frameNumber = Orthanc::SerializationToolbox::ReadUnsignedInteger(body_, "frame", 0 /* default frame */);

    annotationsId_.reset(new AnnotationsId(projectId, Orthanc::StringToResourceType(level.c_str()), resourceId, frameNumber));

    IAuthenticatedUser::ProjectRole role = user_->GetRoleInProject(annotationsId_->GetProjectId());

    if (role != IAuthenticatedUser::ProjectRole_Instructor &&
        role != IAuthenticatedUser::ProjectRole_Learner)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ForbiddenAccess, "User \"" + user_->Format() +
                                      "\" is not instructor or learner of project \"" + annotationsId_->GetProjectId() + "\"");
    }

    const std::string cacheKey = annotationsId_->GetKey();

    static Orthanc::SharedObjectCache annotationsCache_(100);  // TODO - PARAMETER

    cachedAnnotations_ = annotationsCache_.GetCachedValue(cacheKey);

    if (cachedAnnotations_.get() == NULL)
    {
      cachedAnnotations_.reset(new CachedAnnotations(*annotationsId_));
      annotationsCache_.Store(cacheKey, cachedAnnotations_, 1);
    }
  }

  const IAuthenticatedUser& GetUser() const
  {
    assert(user_.get() != NULL);
    return *user_;
  }

  const AnnotationsId& GetAnnotationsId() const
  {
    assert(annotationsId_.get() != NULL);
    return *annotationsId_;
  }

  std::string GetFeaturesKey() const
  {
    return ::GetFeaturesKey(GetAnnotationsId(), GetUser().GetAnnotatingId());
  }

  std::string GetBodyString(const char* field) const
  {
    return Orthanc::SerializationToolbox::ReadString(body_, field);
  }

  const Json::Value& GetBodyField(const char* field) const
  {
    if (!body_.isMember(field))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
    }
    else
    {
      return body_[field];
    }
  }

  CachedAnnotations& GetCachedAnnotations()
  {
    return dynamic_cast<CachedAnnotations&>(*cachedAnnotations_);
  }
};


static bool ProtectPostRequest(OrthancPluginRestOutput* output,
                               const OrthancPluginHttpRequest* request)
{
  if (request->method != OrthancPluginHttpMethod_Post)
  {
    OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
    return false;
  }
  else
  {
    return true;
  }
}




void GetAnnotationsInfo(OrthancPluginRestOutput* output,
                        const char* url,
                        const OrthancPluginHttpRequest* request)
{
  if (ProtectPostRequest(output, request))
  {
    AnnotationsCommandContext context(request);

    CachedAnnotations::UserReader reader(context.GetCachedAnnotations(), context.GetUser());

    Json::Value answer;
    answer["project-name"] = reader.GetAnnotationsInfo().GetProjectName();
    answer["project-description"] = reader.GetAnnotationsInfo().GetProjectDescription();

    const UserId user = context.GetUser().GetAnnotatingId();

    if (user.GetType() == UserId::Type_Standard)
    {
      answer["user"] = user.GetName();
    }
    else
    {
      answer["user"] = "";
    }

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
    answer["persistent-annotations"] = true;
#else
    answer["persistent-annotations"] = false;
#endif

    AnswerJson(output, answer);
  }
}


void ListLayers(OrthancPluginRestOutput* output,
                const char* url,
                const OrthancPluginHttpRequest* request)
{
  if (ProtectPostRequest(output, request))
  {
    AnnotationsCommandContext context(request);

    CachedAnnotations::UserReader reader(context.GetCachedAnnotations(), context.GetUser());

    Json::Value answer;
    reader.ListLayers(answer);

    AnswerJson(output, answer);
  }
}


void CreateUserLayer(OrthancPluginRestOutput* output,
                     const char* url,
                     const OrthancPluginHttpRequest* request)
{
  if (ProtectPostRequest(output, request))
  {
    AnnotationsCommandContext context(request);

    CachedAnnotations::UserWriter writer(context.GetCachedAnnotations(), context.GetUser());

    Json::Value answer;
    writer.CreateUserLayer(answer);

    AnswerJson(output, answer);
  }
}


void SaveUserLayer(OrthancPluginRestOutput* output,
                   const char* url,
                   const OrthancPluginHttpRequest* request)
{
  if (ProtectPostRequest(output, request))
  {
    AnnotationsCommandContext context(request);

    UserLayer updated(context.GetBodyField("layer"));

    {
      CachedAnnotations::UserWriter writer(context.GetCachedAnnotations(), context.GetUser());
      writer.UpdateUserLayer(updated);
    }

    AnswerEmpty(output);
  }
}


void DeleteUserLayer(OrthancPluginRestOutput* output,
                     const char* url,
                     const OrthancPluginHttpRequest* request)
{
  if (ProtectPostRequest(output, request))
  {
    AnnotationsCommandContext context(request);

    const std::string layerId = context.GetBodyString("layer-id");

    {
      CachedAnnotations::UserWriter writer(context.GetCachedAnnotations(), context.GetUser());
      writer.DeleteUserLayer(layerId);
    }

    AnswerEmpty(output);
  }
}


void LoadUserFeatures(OrthancPluginRestOutput* output,
                      const char* url,
                      const OrthancPluginHttpRequest* request)
{
  if (request->method != OrthancPluginHttpMethod_Post)
  {
    OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
  }
  else
  {
    AnnotationsCommandContext context(request);

    Json::Value answer;
    answer[KEY_FEATURES] = Json::arrayValue;

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
    std::string compressed;
    if (LookupKeyValueStore(compressed, context.GetFeaturesKey()))
    {
      std::string uncompressed;
      Orthanc::GzipCompressor compressor;
      Orthanc::IBufferCompressor::Uncompress(uncompressed, compressor, compressed);

      if (!Orthanc::Toolbox::ReadJson(answer[KEY_FEATURES], uncompressed))
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }
    }
#else
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "Your Orthanc SDK is too old to load annotations");
#endif

    AnswerJson(output, answer);
  }
}


void SaveUserFeatures(OrthancPluginRestOutput* output,
                      const char* url,
                      const OrthancPluginHttpRequest* request)
{
  if (request->method != OrthancPluginHttpMethod_Post)
  {
    OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
  }
  else
  {
    AnnotationsCommandContext context(request);

    std::string features;
    Orthanc::Toolbox::WriteFastJson(features, context.GetBodyField(KEY_FEATURES));

    std::string compressed;
    Orthanc::GzipCompressor compressor;
    Orthanc::IBufferCompressor::Compress(compressed, compressor, features);

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
    SetKeyValueStore(context.GetFeaturesKey(), compressed);
    AnswerEmpty(output);
#else
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "Your Orthanc SDK is too old to save annotations");
#endif
  }
}


void ListSharedLayers(OrthancPluginRestOutput* output,
                      const char* url,
                      const OrthancPluginHttpRequest* request)
{
  if (request->method != OrthancPluginHttpMethod_Post)
  {
    OrthancPluginSendMethodNotAllowed(OrthancPlugins::GetGlobalContext(), output, "POST");
  }
  else
  {
    std::unique_ptr<IAuthenticatedUser> user(IAuthenticatedUser::FromHttpRequest(request));

    Json::Value body;
    if (!Orthanc::Toolbox::ReadJson(body, request->body, request->bodySize) ||
        !body.isObject())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
    }

    Json::Value answer;

    for (size_t i = 0; i < 10; i++)
    {
      const std::string userId = "User " + boost::lexical_cast<std::string>(i);

      Json::Value layers = Json::arrayValue;
      for (size_t j = 0; j < 5; j++) {
        Json::Value layer;
        layer["id"] = Orthanc::Toolbox::GenerateUuid();
        layer["name"] = "Layer " + boost::lexical_cast<std::string>(j);
        layers.append(layer);
      }

      Json::Value user;
      user["layers"] = layers;
      answer[userId] = user;
    }

    AnswerJson(output, answer);
  }
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

#if !ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
    LOG(WARNING) << "The whole-slide imaging viewer was compiled against an old "
                 << "version of the Orthanc SDK, annotations will not be persistent";
#elif !ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 9)
    LOG(WARNING) << "The whole-slide imaging viewer was compiled against an old "
                 << "version of the Orthanc SDK, per-user annotations are not supported";
#endif

    try
    {
      /**

         C.10.7.1.1 Encoding of CIELab Values

         Attributes such as Graphic Layer Recommended Display CIELab
         Value (0070,0401) consist of three unsigned short values:

         An L value linearly scaled to 16 bits, such that 0x0000
         corresponds to an L of 0.0, and 0xFFFF corresponds to an L of
         100.0.

         An a* then a b* value, each linearly scaled to 16 bits and
         offset to an unsigned range, such that 0x0000 corresponds to
         an a* or b* of -128.0, 0x8080 corresponds to an a* or b* of
         0.0 and 0xFFFF corresponds to an a* or b* of 127.0

      **/

      OrthancWSI::LABColor lab;
      if (!OrthancWSI::LABColor::DecodeDicomRecommendedAbsentPixelCIELab(lab, "65535\\0\\0") ||
          !OrthancWSI::ImageToolbox::IsNear(lab.GetL(), 100.0, 0.001) ||
          !OrthancWSI::ImageToolbox::IsNear(lab.GetA(), -128.0, 0.001) ||
          !OrthancWSI::ImageToolbox::IsNear(lab.GetB(), -128.0, 0.001))
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }

      if (!OrthancWSI::LABColor::DecodeDicomRecommendedAbsentPixelCIELab(lab, "0\\32896\\65535") ||
          !OrthancWSI::ImageToolbox::IsNear(lab.GetL(), 0.0, 0.001) ||
          !OrthancWSI::ImageToolbox::IsNear(lab.GetA(), 0.0, 0.001) ||
          !OrthancWSI::ImageToolbox::IsNear(lab.GetB(), 127.0, 0.001))
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }
    }
    catch (Orthanc::OrthancException& e)
    {
      LOG(ERROR) << "Exception in startup tests: " << e.What();
      return -1;
    }

    // Limit the number of PNG transcoders to the number of available
    // hardware threads (e.g. number of CPUs or cores or
    // hyperthreading units)
    unsigned int threads = Orthanc::SystemToolbox::GetHardwareConcurrency();
    OrthancWSI::RawTile::InitializeTranscoderSemaphore(threads);

    LOG(WARNING) << "The whole-slide imaging plugin will use at most " << threads << " threads to transcode the tiles";

    OrthancPlugins::SetDescription(ORTHANC_PLUGIN_NAME, "Provides a Web viewer of whole-slide microscopic images within Orthanc.");

    OrthancWSI::DicomPyramidCache::InitializeInstance(10 /* Number of pyramids to be cached - TODO parameter */,
                                                      true /* Use the metadata cache - Should be "false" only during development */);

    {
      std::unique_ptr<OrthancWSI::OrthancPyramidFrameFetcher> fetcher(
        new OrthancWSI::OrthancPyramidFrameFetcher(new OrthancWSI::OrthancPluginConnection(), false /* smooth - TODO PARAMETER */));
      fetcher->SetPaddingX(64);  // TODO PARAMETER
      fetcher->SetPaddingY(64);  // TODO PARAMETER
      fetcher->SetDefaultBackgroundColor(255, 255, 255);  // TODO PARAMETER

      OrthancWSI::DecodedPyramidCache::InitializeInstance(fetcher.release(),
                                                          10 /* TODO - PARAMETER */,
                                                          256 * 1024 * 1024 /* TODO - PARAMETER */);
    }

    OrthancPluginRegisterOnChangeCallback(OrthancPlugins::GetGlobalContext(), OnChangeCallback);

    OrthancPlugins::RegisterRestCallback<ServeJavaScriptLibraries>("/wsi/libs/(.*)", true);

#if ORTHANC_STANDALONE == 1
    OrthancPlugins::RegisterRestCallback<ServeEmbeddedFile>("/wsi/app/(annotations.js)", true);
    OrthancPlugins::RegisterRestCallback<ServeEmbeddedFile>("/wsi/app/(viewer.html)", true);
    OrthancPlugins::RegisterRestCallback<ServeEmbeddedFile>("/wsi/app/(viewer.js)", true);
#else
    OrthancPlugins::RegisterRestCallback<ServeSourceFile>("/wsi/app/(annotations.js)", true);
    OrthancPlugins::RegisterRestCallback<ServeSourceFile>("/wsi/app/(viewer.html)", true);
    OrthancPlugins::RegisterRestCallback<ServeSourceFile>("/wsi/app/(viewer.js)", true);
#endif

    OrthancPlugins::RegisterRestCallback<ServePyramid>("/wsi/pyramids/([0-9a-f-]+)", true);
    OrthancPlugins::RegisterRestCallback<ServeTile>("/wsi/tiles/([0-9a-f-]+)/([0-9-]+)/([0-9-]+)/([0-9-]+)", true);
    OrthancPlugins::RegisterRestCallback<ServeFramePyramid>("/wsi/frames-pyramids/([0-9a-f-]+)/([0-9-]+)", true);
    OrthancPlugins::RegisterRestCallback<ServeFrameTile>("/wsi/frames-tiles/([0-9a-f-]+)/([0-9-]+)/([0-9-]+)/([0-9-]+)/([0-9-]+)", true);

    // NEW
    OrthancPlugins::RegisterRestCallback<GetAnnotationsInfo>("/wsi/api/annotations-info", true);
    OrthancPlugins::RegisterRestCallback<CreateUserLayer>("/wsi/api/create-user-layer", true);
    OrthancPlugins::RegisterRestCallback<DeleteUserLayer>("/wsi/api/delete-user-layer", true);
    OrthancPlugins::RegisterRestCallback<ListLayers>("/wsi/api/list-layers", true);
    OrthancPlugins::RegisterRestCallback<SaveUserLayer>("/wsi/api/save-user-layer", true);
    OrthancPlugins::RegisterRestCallback<LoadUserFeatures>("/wsi/api/load-user-features", true);
    OrthancPlugins::RegisterRestCallback<SaveUserFeatures>("/wsi/api/save-user-features", true);

    // TODO
    OrthancPlugins::RegisterRestCallback<ListSharedLayers>("/wsi/api/shared-layers", true);


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
      OrthancPlugins::RegisterRestCallback<ServeEmbeddedFile>("/wsi/app/(mirador.html)", true);
    }

    if (serveOpenSeadragon)
    {
      OrthancPlugins::RegisterRestCallback<ServeEmbeddedFile>("/wsi/app/(openseadragon.html)", true);
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
    OrthancWSI::DecodedPyramidCache::FinalizeInstance();
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
