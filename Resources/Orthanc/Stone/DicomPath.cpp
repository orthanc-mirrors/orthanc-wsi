/**
 * Stone of Orthanc
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2021 Osimis S.A., Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 **/


#include "DicomPath.h"

#include <OrthancException.h>

#include <boost/lexical_cast.hpp>

namespace OrthancStone
{
  const DicomPath::Prefix& DicomPath::GetPrefixItem(size_t depth) const
  {
    if (depth >= prefix_.size())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    else
    {
      return prefix_[depth];
    }
  }


  DicomPath::Prefix& DicomPath::GetPrefixItem(size_t depth)
  {
    if (depth >= prefix_.size())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    else
    {
      return prefix_[depth];
    }
  }


  DicomPath::DicomPath(const Orthanc::DicomTag& sequence,
                       size_t index,
                       const Orthanc::DicomTag& tag) :
    finalTag_(tag)
  {
    AddToPrefix(sequence, index);
  }


  DicomPath::DicomPath(const Orthanc::DicomTag& sequence1,
                       size_t index1,
                       const Orthanc::DicomTag& sequence2,
                       size_t index2,
                       const Orthanc::DicomTag& tag) :
    finalTag_(tag)
  {
    AddToPrefix(sequence1, index1);
    AddToPrefix(sequence2, index2);
  }


  DicomPath::DicomPath(const Orthanc::DicomTag& sequence1,
                       size_t index1,
                       const Orthanc::DicomTag& sequence2,
                       size_t index2,
                       const Orthanc::DicomTag& sequence3,
                       size_t index3,
                       const Orthanc::DicomTag& tag) :
    finalTag_(tag)
  {
    AddToPrefix(sequence1, index1);
    AddToPrefix(sequence2, index2);
    AddToPrefix(sequence3, index3);
  }


  static std::string FormatHexadecimal(const Orthanc::DicomTag& tag)
  {
    char buf[16];
    sprintf(buf, "(%04x,%04x)", tag.GetGroup(), tag.GetElement());
    return buf;
  }
  

  std::string DicomPath::Format() const
  {
    std::string s;
      
    for (size_t i = 0; i < GetPrefixLength(); i++)
    {
      s += (FormatHexadecimal(GetPrefixTag(i)) + " / " +
            boost::lexical_cast<std::string>(i) + " / ");
    }

    return s + FormatHexadecimal(GetFinalTag());
  }
}
