/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2018 Osimis S.A., Belgium
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
#include "PyramidWithRawTiles.h"

#include "../../Resources/Orthanc/Core/Images/PngReader.h"
#include "../../Resources/Orthanc/Core/Images/JpegReader.h"
#include "../../Resources/Orthanc/Core/OrthancException.h"
#include "../Jpeg2000Reader.h"

namespace OrthancWSI
{
  Orthanc::ImageAccessor* PyramidWithRawTiles::DecodeTile(unsigned int level,
                                                          unsigned int tileX,
                                                          unsigned int tileY)
  {
    std::string tile;
    ImageCompression compression;

    if (!ReadRawTile(tile, compression, level, tileX, tileY))
    {
      return NULL;
    }

    std::auto_ptr<Orthanc::ImageAccessor> result;

    switch (compression)
    {
      case ImageCompression_None:
        result.reset(new Orthanc::ImageAccessor);
        result->AssignReadOnly(GetPixelFormat(), 
                               GetTileWidth(),
                               GetTileHeight(), 
                               GetBytesPerPixel(GetPixelFormat()) * GetTileWidth(),
                               tile.c_str());
        break;

      case ImageCompression_Jpeg:
        result.reset(new Orthanc::JpegReader);
        dynamic_cast<Orthanc::JpegReader&>(*result).ReadFromMemory(tile);
        break;

      case ImageCompression_Png:
        result.reset(new Orthanc::PngReader);
        dynamic_cast<Orthanc::PngReader&>(*result).ReadFromMemory(tile);
        break;

      case ImageCompression_Jpeg2000:
        result.reset(new Jpeg2000Reader);
        dynamic_cast<Jpeg2000Reader&>(*result).ReadFromMemory(tile);
        break;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
    }

    return result.release();
  }
}
