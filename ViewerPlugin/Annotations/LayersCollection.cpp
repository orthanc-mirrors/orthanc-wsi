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


#include "../../Framework/PrecompiledHeadersWSI.h"
#include "LayersCollection.h"

#include <OrthancException.h>

#include <cassert>


namespace OrthancWSI
{
  LayersCollection::~LayersCollection()
  {
    for (Content::iterator it = content_.begin(); it != content_.end(); ++it)
    {
      assert(*it != NULL);
      delete *it;
    }
  }


  size_t LayersCollection::GetSize() const
  {
    assert(content_.size() == index_.size());
    return content_.size();
  }


  void LayersCollection::AddLayer(ILayer* layer)
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


  bool LayersCollection::HasLayer(const std::string& id) const
  {
    return (index_.find(id) != index_.end());
  }


  ILayer& LayersCollection::GetLayer(const std::string& id) const
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


  void LayersCollection::DeleteLayer(const std::string& id)
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


  void LayersCollection::Serialize(Json::Value& serialized) const
  {
    serialized = Json::arrayValue;

    for (Content::const_iterator it = content_.begin(); it != content_.end(); ++it)
    {
      assert(*it != NULL);

      Json::Value item;
      (*it)->Serialize(item);
      serialized.append(item);
    }
  }


  LayersCollection::Iterator::Iterator(const LayersCollection& that) :
    it_(that.content_.begin()),
    end_(that.content_.end())
  {
  }


  bool LayersCollection::Iterator::IsDone() const
  {
    return it_ == end_;
  }


  const ILayer& LayersCollection::Iterator::GetLayer() const
  {
    if (IsDone())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      assert(*it_ != NULL);
      return **it_;
    }
  }


  void LayersCollection::Iterator::Next()
  {
    if (IsDone())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      ++it_;
    }
  }
}
