/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2021 Osimis S.A., Belgium
 * Copyright (C) 2021-2021 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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
#include "DicomPyramid.h"

#include "../DicomToolbox.h"

#include <Compatibility.h>
#include <Logging.h>
#include <OrthancException.h>
#include <Toolbox.h>

#include <algorithm>
#include <cassert>

namespace OrthancWSI
{
  struct DicomPyramid::Comparator
  {
    bool operator() (DicomPyramidInstance* const& a,
                     DicomPyramidInstance* const& b) const
    {
      return a->GetTotalWidth() > b->GetTotalWidth();
    }
  };


  void DicomPyramid::Clear()
  {
    for (size_t i = 0; i < levels_.size(); i++)
    {
      if (levels_[i] != NULL)
      {
        delete levels_[i];
      }
    }

    for (size_t i = 0; i < instances_.size(); i++)
    {
      if (instances_[i] != NULL)
      {
        delete instances_[i];
      }
    }
  }


  void DicomPyramid::RegisterInstances(const std::string& seriesId,
                                       bool useCache)
  {
    Json::Value series;
    OrthancStone::IOrthancConnection::RestApiGet(series, orthanc_, "/series/" + seriesId);

    if (series.type() != Json::objectValue ||
        !series.isMember("Instances") ||
        series["Instances"].type() != Json::arrayValue)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
    }

    const Json::Value& instances = series["Instances"];
    instances_.reserve(instances.size());

    for (Json::Value::ArrayIndex i = 0; i < instances.size(); i++)
    {
      if (instances[i].type() != Json::stringValue)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NetworkProtocol);
      }

      std::string instanceId = instances[i].asString();

      try
      {
        std::unique_ptr<DicomPyramidInstance> instance(new DicomPyramidInstance(orthanc_, instanceId, useCache));

        std::vector<std::string> tokens;
        Orthanc::Toolbox::TokenizeString(tokens, instance->GetImageType(), '\\');

        // Don't consider the thumbnail and overview as part of the DICOM pyramid (new in 1.0)
        if (tokens.size() < 2 ||
            (tokens[2] != "THUMBNAIL" &&
             tokens[2] != "OVERVIEW"))
        {
          instances_.push_back(instance.release());
        }        
      }
      catch (Orthanc::OrthancException&)
      {
        LOG(ERROR) << "Skipping a DICOM instance that is not part of a whole-slide image: " << instanceId;
      }
    }
  }


  void DicomPyramid::Check(const std::string& seriesId) const
  {
    if (instances_.empty())
    {
      LOG(ERROR) << "This series does not contain a whole-slide image: " << seriesId;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }

    const DicomPyramidInstance& a = *instances_[0];

    for (size_t i = 1; i < instances_.size(); i++)
    {
      const DicomPyramidInstance& b = *instances_[i];

      if (a.GetPixelFormat() != b.GetPixelFormat() ||
          a.GetTotalWidth() < b.GetTotalWidth() ||
          a.GetTotalHeight() < b.GetTotalHeight())            
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageFormat);
      }

      if (a.GetTotalWidth() == b.GetTotalWidth() &&
          a.GetTotalHeight() != b.GetTotalHeight())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageFormat);
      }
    }
  }


  void DicomPyramid::CheckLevel(size_t level) const
  {
    if (level >= levels_.size())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  DicomPyramid::DicomPyramid(OrthancStone::IOrthancConnection& orthanc,
                             const std::string& seriesId,
                             bool useCache) :
    orthanc_(orthanc),
    seriesId_(seriesId)
  {
    RegisterInstances(seriesId, useCache);

    // Sort the instances of the pyramid by decreasing total widths
    std::sort(instances_.begin(), instances_.end(), Comparator());

    try
    {
      Check(seriesId);
    }
    catch (Orthanc::OrthancException&)
    {
      Clear();
      throw;
    }

    for (size_t i = 0; i < instances_.size(); i++)
    {
      if (i == 0 ||
          instances_[i - 1]->GetTotalWidth() != instances_[i]->GetTotalWidth())
      {
        levels_.push_back(new DicomPyramidLevel(*instances_[i]));
      }
      else
      {
        assert(levels_.back() != NULL);
        levels_.back()->AddInstance(*instances_[i]);
      }
    }
  }


  unsigned int DicomPyramid::GetLevelWidth(unsigned int level) const
  {
    CheckLevel(level);
    return levels_[level]->GetTotalWidth();
  }


  unsigned int DicomPyramid::GetLevelHeight(unsigned int level) const
  {
    CheckLevel(level);
    return levels_[level]->GetTotalHeight();
  }


  unsigned int DicomPyramid::GetTileWidth(unsigned int level) const
  {
    CheckLevel(level);
    return levels_[level]->GetTileWidth();
  }


  unsigned int DicomPyramid::GetTileHeight(unsigned int level) const
  {
    CheckLevel(level);
    return levels_[level]->GetTileHeight();
  }


  bool DicomPyramid::ReadRawTile(std::string& tile,
                                 ImageCompression& compression,
                                 unsigned int level,
                                 unsigned int tileX,
                                 unsigned int tileY)
  {
    CheckLevel(level);
      
    Orthanc::PixelFormat format;
      
    if (levels_[level]->DownloadRawTile(tile, format, compression, orthanc_, tileX, tileY))
    {
      if (format != GetPixelFormat())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      return true;
    }
    else
    {
      return false;
    }
  }


  Orthanc::PixelFormat DicomPyramid::GetPixelFormat() const
  {
    assert(!instances_.empty() && instances_[0] != NULL);
    return instances_[0]->GetPixelFormat();
  }

  
  Orthanc::PhotometricInterpretation DicomPyramid::GetPhotometricInterpretation() const
  {
    assert(!instances_.empty() && instances_[0] != NULL);
    return instances_[0]->GetPhotometricInterpretation();
  }
}
