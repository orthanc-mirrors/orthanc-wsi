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


#include "ApplicationToolbox.h"

#include "../Framework/Inputs/OpenSlideLibrary.h"
#include "../Framework/Orthanc/Core/HttpClient.h"
#include "../Framework/Orthanc/Core/Logging.h"
#include "../Framework/Orthanc/Core/MultiThreading/BagOfTasksProcessor.h"
#include "../Framework/Orthanc/OrthancServer/FromDcmtkBridge.h"

#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>

namespace OrthancWSI
{
  namespace ApplicationToolbox
  {
    void GlobalInitialize()
    {
      Orthanc::Logging::Initialize();
      Orthanc::HttpClient::InitializeOpenSsl();
      Orthanc::HttpClient::GlobalInitialize();
      Orthanc::FromDcmtkBridge::InitializeDictionary();
    }


    void GlobalFinalize()
    {
      OrthancWSI::OpenSlideLibrary::Finalize();
      Orthanc::HttpClient::GlobalFinalize();
      Orthanc::HttpClient::FinalizeOpenSsl();
    }


    static void PrintProgress(Orthanc::BagOfTasksProcessor::Handle* handle,
                              bool* done)
    {
      unsigned int previous = 0;

      while (!*done)
      {
        unsigned int progress = static_cast<unsigned int>(100.0f * handle->GetProgress());
        if (previous != progress)
        {
          LOG(WARNING) << "Progress: " << progress << "%";
          previous = progress;
        }

        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
      }
    }


    void Execute(Orthanc::BagOfTasks& tasks,
                 unsigned int threadsCount)
    {
      if (threadsCount > 1)
      {
        LOG(WARNING) << "Running " << tasks.GetSize() << " tasks";
        LOG(WARNING) << "Using " << threadsCount << " threads for the computation";
        Orthanc::BagOfTasksProcessor processor(threadsCount);
        std::auto_ptr<Orthanc::BagOfTasksProcessor::Handle> handle(processor.Submit(tasks));

        bool done = false;
        boost::thread progress(PrintProgress, handle.get(), &done);

        if (handle->Join())
        {
          done = true;
          LOG(WARNING) << "All tasks have finished";
        }
        else
        {
          done = true;
          LOG(ERROR) << "Error has occurred, aborting";
          throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
        }
                           
        if (progress.joinable())
        {
          progress.join();
        }
      }
      else
      {
        LOG(WARNING) << "Running " << tasks.GetSize() << " tasks without multithreading";

        unsigned int previous = 0;
        unsigned int size = tasks.GetSize();

        // No multithreading
        while (!tasks.IsEmpty())
        {
          std::auto_ptr<Orthanc::ICommand> task(tasks.Pop());
          if (task->Execute())
          {
            unsigned int progress = static_cast<unsigned int>(100.0f * 
                                                              static_cast<float>((size - tasks.GetSize())) / 
                                                              static_cast<float>(size));
            if (progress != previous)
            {
              LOG(WARNING) << "Progress: " << progress << "%";
              previous = progress;
            }
          }
          else
          {
            LOG(ERROR) << "Error has occurred, aborting";
            throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
          }
        }
      }
    }


    void ParseColor(uint8_t& red,
                    uint8_t& green,
                    uint8_t& blue,
                    const std::string& color)
    {
      boost::regex pattern("([0-9]*),([0-9]*),([0-9]*)");
    
      bool ok = false;
      boost::cmatch what;

      try
      {
        if (regex_match(color.c_str(), what, pattern))
        {
          int r = boost::lexical_cast<int>(what[1]);
          int g = boost::lexical_cast<int>(what[2]);
          int b = boost::lexical_cast<int>(what[3]);

          if (r >= 0 && r <= 255 &&
              g >= 0 && g <= 255 &&
              b >= 0 && b <= 255)
          {
            red = static_cast<uint8_t>(r);
            green = static_cast<uint8_t>(g);
            blue = static_cast<uint8_t>(b);
            ok = true;
          }
        }
      }
      catch (boost::bad_lexical_cast&)
      {
      }
    
      if (!ok)
      {
        LOG(ERROR) << "Bad color specification: " << color;
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
    }
  }
}