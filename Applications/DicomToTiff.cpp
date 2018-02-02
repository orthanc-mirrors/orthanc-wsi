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


#include "../Framework/DicomToolbox.h"
#include "../Framework/ImageToolbox.h"
#include "../Framework/Inputs/DicomPyramid.h"
#include "../Framework/Inputs/TiledPyramidStatistics.h"
#include "../Framework/Outputs/HierarchicalTiffWriter.h"

#include "../Resources/Orthanc/Core/Logging.h"
#include "../Resources/Orthanc/Core/OrthancException.h"
#include "../Resources/Orthanc/Plugins/Samples/Common/OrthancHttpConnection.h"

#include "ApplicationToolbox.h"


static bool ParseParameters(int& exitStatus,
                            boost::program_options::variables_map& options,
                            int argc, 
                            char* argv[])
{
  // Declare the supported parameters
  boost::program_options::options_description generic("Generic options");
  generic.add_options()
    ("help", "Display this help and exit")
    ("version", "Output version information and exit")
    ("verbose", "Be verbose in logs")
    ;

  boost::program_options::options_description source("Options for the source DICOM image");
  source.add_options()
    ("orthanc", boost::program_options::value<std::string>()->default_value("http://localhost:8042/"),
     "URL to the REST API of the target Orthanc server")
    ;
  OrthancWSI::ApplicationToolbox::AddRestApiOptions(source);

  boost::program_options::options_description target("Options for the target TIFF image");
  target.add_options()
    ("color", boost::program_options::value<std::string>(), "Color of the background for missing tiles (e.g. \"255,0,0\")")
    ("reencode", boost::program_options::value<bool>(), 
     "Whether to re-encode each tile in JPEG (no transcoding, much slower) (Boolean)")
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
      options.count("help") == 0 &&
      options.count("version") == 0)
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
              << "Convert a DICOM image for digital pathology stored in some Orthanc server as a" << std::endl
              << "standard hierarchical TIFF (whose tiles are all encoded using JPEG)."
              << std::endl;

    std::cout << allWithoutHidden << "\n";

    if (error)
    {
      exitStatus = -1;
    }

    return false;
  }

  if (options.count("version")) 
  { 
    OrthancWSI::ApplicationToolbox::PrintVersion(argv[0]);
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



static void Run(OrthancWSI::ITiledPyramid& source,
                const boost::program_options::variables_map& options)
{
  OrthancWSI::HierarchicalTiffWriter target(options["output"].as<std::string>(),
                                            source.GetPixelFormat(), 
                                            OrthancWSI::ImageCompression_Jpeg,
                                            source.GetTileWidth(), 
                                            source.GetTileHeight());

  bool reencode = (options.count("reencode") &&
                   options["reencode"].as<bool>());

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
    LOG(WARNING) << std::string(reencode ? "Reencoding" : "Transcoding") << " level " << level;

    unsigned int countX = OrthancWSI::CeilingDivision(source.GetLevelWidth(level), source.GetTileWidth());
    unsigned int countY = OrthancWSI::CeilingDivision(source.GetLevelHeight(level), source.GetTileHeight());

    for (unsigned int tileY = 0; tileY < countY; tileY++)
    {
      for (unsigned int tileX = 0; tileX < countX; tileX++)
      {
        LOG(INFO) << "Dealing with tile (" << tileX << "," << tileY << ") at level " << level;

        bool missing = false;
        bool success = true;

        // Give a first try to get the raw tile
        std::string tile;
        OrthancWSI::ImageCompression compression;
        if (source.ReadRawTile(tile, compression, level, tileX, tileY))
        {
          if (reencode ||
              compression == OrthancWSI::ImageCompression_Jpeg)
          {
            target.WriteRawTile(tile, compression, level, tileX, tileY);
          }
          else
          {
            success = false;  // Re-encoding is mandatory
          }
        }
        else
        {
          // Give a second try to get the decoded tile
          compression = OrthancWSI::ImageCompression_Unknown;

          std::auto_ptr<Orthanc::ImageAccessor> tile(source.DecodeTile(level, tileX, tileY));
          if (tile.get() == NULL)
          {
            // Unable to read the raw tile or to decode it: The tile is missing (sparse tiling)
            missing = true;
          }
          else if (reencode)
          {
            target.EncodeTile(*empty, level, tileX, tileY);
          }
          else
          {
            success = false;  // Re-encoding is mandatory
          }
        }

        if (!success)
        {
          LOG(WARNING) << "Cannot transcode a DICOM image that is not encoded using JPEG (it is " 
                       << OrthancWSI::EnumerationToString(compression) 
                       << "), please use the --reencode=1 option";
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
        }
        
        if (missing)
        {
          LOG(WARNING) << "Sparse tiling: Using an empty image for missing tile ("
                       << tileX << "," << tileY << ") at level " << level;
          target.EncodeTile(*empty, level, tileX, tileY);
        }
      }        
    }

    target.Flush();
  }
}



int main(int argc, char* argv[])
{
  OrthancWSI::ApplicationToolbox::GlobalInitialize();
  OrthancWSI::ApplicationToolbox::ShowVersionInLog(argv[0]);

  int exitStatus = 0;
  boost::program_options::variables_map options;

  try
  {
    if (ParseParameters(exitStatus, options, argc, argv))
    {
      Orthanc::WebServiceParameters params;

      OrthancWSI::ApplicationToolbox::SetupRestApi(params, options);

      OrthancPlugins::OrthancHttpConnection orthanc(params);
      OrthancWSI::DicomPyramid source(orthanc, options["input"].as<std::string>(), 
                                      false /* don't use cached metadata */);

      OrthancWSI::TiledPyramidStatistics stats(source);
      Run(stats, options);
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
