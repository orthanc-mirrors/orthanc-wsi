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


#pragma once

#include "ICommand.h"

#include <list>
#include <cstddef>

namespace Orthanc
{
  class BagOfTasks : public boost::noncopyable
  {
  private:
    typedef std::list<ICommand*>  Tasks;

    Tasks  tasks_;

  public:
    ~BagOfTasks()
    {
      for (Tasks::iterator it = tasks_.begin(); it != tasks_.end(); ++it)
      {
        delete *it;
      }
    }

    ICommand* Pop()
    {
      ICommand* task = tasks_.front();
      tasks_.pop_front();
      return task;
    }

    void Push(ICommand* task)   // Takes ownership
    {
      if (task != NULL)
      {
        tasks_.push_back(task);
      }
    }

    size_t GetSize() const
    {
      return tasks_.size();
    }

    bool IsEmpty() const
    {
      return tasks_.empty();
    }
  };
}