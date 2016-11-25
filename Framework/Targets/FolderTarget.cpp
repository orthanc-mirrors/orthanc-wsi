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


#include "../PrecompiledHeadersWSI.h"
#include "FolderTarget.h"

#include "../../Resources/Orthanc/Core/SystemToolbox.h"
#include "../../Resources/Orthanc/Core/Logging.h"

#include <stdio.h>

namespace OrthancWSI
{
  void FolderTarget::Write(const std::string& file)
  {
    std::string path;
    path.resize(pattern_.size() + 16);

    {
      boost::mutex::scoped_lock lock(mutex_);
      sprintf(&path[0], pattern_.c_str(), count_);
      count_ += 1;
    }

    LOG(INFO) << "Writing file " << path;
    Orthanc::SystemToolbox::WriteFile(file, path);
  }
}
