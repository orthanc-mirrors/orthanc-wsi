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


#include "../Framework/DicomToolbox.h"
#include "../Framework/ImageToolbox.h"
#include "../Framework/Inputs/DicomPyramid.h"
#include "../Framework/Messaging/CurlOrthancConnection.h"
#include "../Framework/Orthanc/Core/Logging.h"
#include "../Framework/Orthanc/Core/OrthancException.h"
#include "../Framework/Outputs/HierarchicalTiffWriter.h"

#include "ApplicationToolbox.h"

#include <boost/program_options.hpp>


static bool ParseParameters(int& exitStatus,
                            boost::program_options::variables_map& options,
                            int argc, 
                            char* argv[])
{
  // Declare the supported parameters
  boost::program_options::options_description generic("Generic options");
  generic.add_options()
    ("help", "Display this help and exit")
    ("verbose", "Be verbose in logs")
    ;

  boost::program_options::options_description source("Options for the source DICOM image");
  source.add_options()
    ("orthanc", boost::program_options::value<std::string>()->default_value("http://localhost:8042/"),
     "URL to the REST API of the target Orthanc server")
    ("username", boost::program_options::value<std::string>(), "Username for the target Orthanc server")
    ("password", boost::program_options::value<std::string>(), "Password for the target Orthanc server")
    ;

  boost::program_options::options_description target("Options for the target TIFF image");
  target.add_options()
    ("color", boost::program_options::value<std::string>(), "Color of the background for missing tiles (e.g. \"255,0,0\")")
    ("reencode", boost::program_options::value<bool>(), 
     "Whether to reencode each tile in JPEG (no transcoding, much slower) (Boolean)")
    ("jpeg-quality", boost::program_options::value<int>(), "Set quality level for JPEG (0..100)")
    ;

  boost::program_options::options_description hidden;
  hidden.add_options()
    ("input", boost::program_options::value<std::string>(), "Orthanc identifier of the input series of interest")
    ("output", boost::program_options::value<std::string>(), "Output TIFF file");
  ;

  boost::program_options::options_description allWithoutHidden;
  allWithoutHidden.add(generic).add(source).add(target);

  boost::program_options::options_description all = allWithoutHidden;
  all.add(hidden);

  boost::program_options::positional_options_description positional;
  positional.add("input", 1);
  positional.add("output", 1);

  bool error = false;

  try
  {
    boost::program_options::store(boost::program_options::command_line_parser(argc, argv).
                                  options(all).positional(positional).run(), options);
    boost::program_options::notify(options);    
  }
  catch (boost::program_options::error& e)
  {
    LOG(ERROR) << "Error while parsing the command-line arguments: " << e.what();
    error = true;
  }

  if (!error &&
      options.count("help") == 0)
  {
    if (options.count("input") != 1)
    {
      LOG(ERROR) << "No input series was specified";
      error = true;
    }

    if (options.count("output") != 1)
    {
      LOG(ERROR) << "No output file was specified";
      error = true;
    }
  }

  if (error || options.count("help")) 
  {
    std::cout << std::endl
              << "Usage: " << argv[0] << " [OPTION]... [INPUT] [OUTPUT]"
              << std::endl
              << "Orthanc, lightweight, RESTful DICOM server for healthcare and medical research."
              << std::endl << std::endl
              << "Convert a DICOM for digital pathology stored in some Orthanc server as a standard hierarchical TIFF."
              << std::endl;

    std::cout << allWithoutHidden << "\n";

    if (error)
    {
      exitStatus = -1;
    }

    return false;
  }

  if (options.count("verbose"))
  {
    Orthanc::Logging::EnableInfoLevel(true);
  }

  return true;
}



static Orthanc::ImageAccessor* CreateEmptyTile(const OrthancWSI::IPyramidWriter& writer,
                                               const boost::program_options::variables_map& options)
{
  std::auto_ptr<Orthanc::ImageAccessor> tile
    (OrthancWSI::ImageToolbox::Allocate(writer.GetPixelFormat(), 
                                        writer.GetTileWidth(), 
                                        writer.GetTileHeight()));

  uint8_t red = 255;
  uint8_t green = 255;
  uint8_t blue = 255;

  if (options.count("color"))
  {
    OrthancWSI::ApplicationToolbox::ParseColor(red, green, blue, options["color"].as<std::string>());
  }

  OrthancWSI::ImageToolbox::Set(*tile, red, green, blue);

  return tile.release();
}



static void RunTranscode(OrthancWSI::ITiledPyramid& source,
                         const boost::program_options::variables_map& options)
{
  OrthancWSI::HierarchicalTiffWriter target(options["output"].as<std::string>(),
                                            source.GetPixelFormat(), 
                                            source.GetImageCompression(), 
                                            source.GetTileWidth(), 
                                            source.GetTileHeight());

  std::auto_ptr<Orthanc::ImageAccessor> empty(CreateEmptyTile(target, options));

  for (unsigned int level = 0; level < source.GetLevelCount(); level++)
  {
    LOG(WARNING) << "Creating level " << level << " of size " 
                 << source.GetLevelWidth(level) << "x" << source.GetLevelHeight(level);
    target.AddLevel(source.GetLevelWidth(level), source.GetLevelHeight(level));
  }

  for (unsigned int level = 0; level < source.GetLevelCount(); level++)
  {
    LOG(WARNING) << "Transcoding level " << level;

    unsigned int countX = OrthancWSI::CeilingDivision(source.GetLevelWidth(level), source.GetTileWidth());
    unsigned int countY = OrthancWSI::CeilingDivision(source.GetLevelHeight(level), source.GetTileHeight());

    for (unsigned int tileY = 0; tileY < countY; tileY++)
    {
      for (unsigned int tileX = 0; tileX < countX; tileX++)
      {
        LOG(INFO) << "Dealing with tile (" << tileX << "," << tileY << ") at level " << level;
        std::string tile;

        if (source.ReadRawTile(tile, level, tileX, tileY))
        {
          target.WriteRawTile(tile, source.GetImageCompression(), level, tileX, tileY);
        }
        else
        {
          target.EncodeTile(*empty, level, tileX, tileY);
        }
      }        
    }

    target.Flush();
  }
}


static void RunReencode(OrthancWSI::ITiledPyramid& source,
                        const boost::program_options::variables_map& options)
{
  OrthancWSI::HierarchicalTiffWriter target(options["output"].as<std::string>(),
                                            source.GetPixelFormat(), 
                                            OrthancWSI::ImageCompression_Jpeg,
                                            source.GetTileWidth(), 
                                            source.GetTileHeight());

  if (options.count("jpeg-quality"))
  {
    target.SetJpegQuality(options["jpeg-quality"].as<int>());
  }

  std::auto_ptr<Orthanc::ImageAccessor> empty(CreateEmptyTile(target, options));

  for (unsigned int level = 0; level < source.GetLevelCount(); level++)
  {
    LOG(WARNING) << "Creating level " << level << " of size " 
                 << source.GetLevelWidth(level) << "x" << source.GetLevelHeight(level);
    target.AddLevel(source.GetLevelWidth(level), source.GetLevelHeight(level));
  }

  for (unsigned int level = 0; level < source.GetLevelCount(); level++)
  {
    LOG(WARNING) << "Reencoding level " << level;

    unsigned int countX = OrthancWSI::CeilingDivision(source.GetLevelWidth(level), source.GetTileWidth());
    unsigned int countY = OrthancWSI::CeilingDivision(source.GetLevelHeight(level), source.GetTileHeight());

    for (unsigned int tileY = 0; tileY < countY; tileY++)
    {
      for (unsigned int tileX = 0; tileX < countX; tileX++)
      {
        LOG(INFO) << "Dealing with tile (" << tileX << "," << tileY << ") at level " << level;

        std::auto_ptr<Orthanc::ImageAccessor> tile(source.DecodeTile(level, tileX, tileY));
        if (tile.get() == NULL)
        {
          target.EncodeTile(*empty, level, tileX, tileY);
        }
        else
        {
          target.EncodeTile(*tile, level, tileX, tileY);
        }
      }        
    }

    target.Flush();
  }
}



int main(int argc, char* argv[])
{
  OrthancWSI::ApplicationToolbox::GlobalInitialize();

  int exitStatus = 0;
  boost::program_options::variables_map options;

  try
  {
    if (ParseParameters(exitStatus, options, argc, argv))
    {
      Orthanc::WebServiceParameters params;

      if (options.count("orthanc"))
      {
        params.SetUrl(options["orthanc"].as<std::string>());
      }

      if (options.count("username"))
      {
        params.SetUsername(options["username"].as<std::string>());
      }

      if (options.count("password"))
      {
        params.SetPassword(options["password"].as<std::string>());
      }

      OrthancWSI::CurlOrthancConnection orthanc(params);
      OrthancWSI::DicomPyramid source(orthanc, options["input"].as<std::string>());

      if (options.count("reencode") &&
          options["reencode"].as<bool>())
      {
        RunReencode(source, options);
      }
      else
      {
        RunTranscode(source, options);
      }
    }
  }
  catch (Orthanc::OrthancException& e)
  {
    LOG(ERROR) << "Terminating on exception: " << e.What();

    if (options.count("reencode") == 0)
    {
      LOG(ERROR) << "Consider using option \"--reencode\"";
    }
    
    exitStatus = -1;
  }

  OrthancWSI::ApplicationToolbox::GlobalFinalize();

  return exitStatus;
}