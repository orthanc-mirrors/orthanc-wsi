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


#pragma once

#include "ILayer.h"

#include <Compatibility.h>

#include <list>
#include <map>


namespace OrthancWSI
{
  class LayersCollection : public ISerializable
  {
  private:
    typedef std::list<ILayer*>                        Content;
    typedef std::map<std::string, Content::iterator>  Index;

    Content  content_;
    Index    index_;

  public:
    ~LayersCollection();

    size_t GetSize() const;

    void AddLayer(ILayer* layer /* takes ownership */);

    bool HasLayer(const std::string& id) const;

    ILayer& GetLayer(const std::string& id) const;

    void DeleteLayer(const std::string& id);

    virtual void Serialize(Json::Value& serialized) const ORTHANC_OVERRIDE;

    class Iterator : public boost::noncopyable
    {
    private:
      Content::const_iterator  it_;
      Content::const_iterator  end_;

    public:
      Iterator(const LayersCollection& that);

      bool IsDone() const;

      const ILayer& GetLayer() const;

      void Next();
    };
  };
}
