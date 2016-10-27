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


#include "PrecompiledHeadersWSI.h"
#include "DicomToolbox.h"

#include "Orthanc/Core/Logging.h"
#include "Orthanc/Core/OrthancException.h"
#include "Orthanc/Core/Toolbox.h"

#if ORTHANC_ENABLE_DCMTK == 1
#  include <dcmtk/dcmdata/dcelem.h>
#  include <dcmtk/dcmdata/dcsequen.h>
#endif

#include <boost/lexical_cast.hpp>

namespace OrthancWSI
{
  namespace DicomToolbox
  {
#if ORTHANC_ENABLE_DCMTK == 1
    void SetStringTag(DcmItem& dataset,
                      const DcmTagKey& key,
                      const std::string& value)
    {
      if (!dataset.tagExists(key) &&
          !dataset.putAndInsertString(key, value.c_str()).good())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }
    }

    void SetUint16Tag(DcmItem& dataset,
                      const DcmTagKey& key,
                      uint16_t value)
    {
      if (!dataset.tagExists(key) &&
          !dataset.putAndInsertUint16(key, value).good())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }
    }


    void SetUint32Tag(DcmItem& dataset,
                      const DcmTagKey& key,
                      uint32_t value)
    {
      if (!dataset.tagExists(key) &&
          !dataset.putAndInsertUint32(key, value).good())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }
    }


    DcmItem* ExtractSingleSequenceItem(DcmItem& dataset,
                                       const DcmTagKey& key)
    {
      DcmElement* element = NULL;
      if (!const_cast<DcmItem&>(dataset).findAndGetElement(key, element).good() ||
          element == NULL)
      {
        return NULL;
      }

      if (element->getVR() != EVR_SQ)
      {
        DcmTag tag(key);
        LOG(ERROR) << "The following element in the DICOM dataset is not a sequence as expected: " << tag.getTagName();
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      DcmSequenceOfItems& sequence = dynamic_cast<DcmSequenceOfItems&>(*element);
      if (sequence.card() != 1)
      {
        LOG(ERROR) << "Bad number of elements in the sequence (it must contain exactly 1 element)";
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      return sequence.getItem(0);
    }


    uint16_t GetUint16Tag(DcmItem& dataset,
                          const DcmTagKey& key)
    {
      uint16_t value;
      if (dataset.findAndGetUint16(key, value).good())
      {
        return value;
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InexistentTag);
      }
    }


    uint32_t GetUint32Tag(DcmItem& dataset,
                          const DcmTagKey& key)
    {
      Uint32 value;
      if (dataset.findAndGetUint32(key, value).good())
      {
        return value;
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InexistentTag);
      }
    }


    int32_t GetInt32Tag(DcmItem& dataset,
                        const DcmTagKey& key)
    {
      Sint32 value;
      if (dataset.findAndGetSint32(key, value).good())
      {
        return value;
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InexistentTag);
      }
    }


    std::string GetStringTag(DcmItem& dataset,
                             const DcmTagKey& key)
    {
      const char* value = NULL;
      if (dataset.findAndGetString(key, value).good() &&
          value != NULL)
      {
        return Orthanc::Toolbox::StripSpaces(value);
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InexistentTag);
      }
    }
#endif


    bool GetStringTag(std::string& result,
                      const Json::Value& simplifiedTags,
                      const std::string& tagName,
                      const std::string& defaultValue)
    {
      if (simplifiedTags.type() != Json::objectValue)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      if (!simplifiedTags.isMember(tagName))
      {
        result = defaultValue;
        return false;
      }
      else if (simplifiedTags[tagName].type() != Json::stringValue)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }
      else
      {
        result = simplifiedTags[tagName].asString();
        return true;
      }
    }


    std::string GetMandatoryStringTag(const Json::Value& simplifiedTags,
                                      const std::string& tagName)
    {
      std::string s;
      if (GetStringTag(s, simplifiedTags, tagName, ""))
      {
        return s;
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InexistentTag);        
      }
    }


    const Json::Value& GetSequenceTag(const Json::Value& simplifiedTags,
                                      const std::string& tagName)
    {
      if (simplifiedTags.type() != Json::objectValue ||
          !simplifiedTags.isMember(tagName) ||
          simplifiedTags[tagName].type() != Json::arrayValue)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      return simplifiedTags[tagName];
    }


    int GetIntegerTag(const Json::Value& simplifiedTags,
                      const std::string& tagName)
    {
      try
      {
        std::string s = Orthanc::Toolbox::StripSpaces(GetMandatoryStringTag(simplifiedTags, tagName));
        return boost::lexical_cast<int>(s);
      }
      catch (boost::bad_lexical_cast&)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);        
      }
    }


    unsigned int GetUnsignedIntegerTag(const Json::Value& simplifiedTags,
                                       const std::string& tagName)
    {
      int value = GetIntegerTag(simplifiedTags, tagName);

      if (value >= 0)
      {
        return static_cast<unsigned int>(value);
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
    }
  }
}
