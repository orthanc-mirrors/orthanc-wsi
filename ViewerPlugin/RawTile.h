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


#pragma once

#include "../Framework/Enumerations.h"
#include "../Framework/Inputs/ITiledPyramid.h"

#include <orthanc/OrthancCPlugin.h>


namespace OrthancWSI
{
  class RawTile : public boost::noncopyable
  {
  private:
    Orthanc::PixelFormat               format_;
    unsigned int                       tileWidth_;
    unsigned int                       tileHeight_;
    Orthanc::PhotometricInterpretation photometric_;
    std::string                        tile_;
    ImageCompression                   compression_;

    Orthanc::ImageAccessor* DecodeInternal();

    static void EncodeInternal(std::string& encoded,
                               const Orthanc::ImageAccessor& decoded,
                               Orthanc::MimeType transcodingType);

  public:
    RawTile(ITiledPyramid& pyramid,
            unsigned int level,
            unsigned int tileX,
            unsigned int tileY);

    void Answer(OrthancPluginRestOutput* output,
                Orthanc::MimeType transcodingType);

    Orthanc::ImageAccessor* Decode();

    static void Encode(std::string& encoded,
                       const Orthanc::ImageAccessor& decoded,
                       Orthanc::MimeType transcodingType);

    // This semaphore is used to implement throttling for the
    // decoding/encoding of tiles
    static void InitializeTranscoderSemaphore(unsigned int maxThreads);

    static void FinalizeTranscoderSemaphore();
  };
}
