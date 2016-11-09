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


#include "../Framework/Algorithms/ReconstructPyramidCommand.h"
#include "../Framework/Algorithms/TranscodeTileCommand.h"
#include "../Framework/DicomToolbox.h"
#include "../Framework/DicomizerParameters.h"
#include "../Framework/ImagedVolumeParameters.h"
#include "../Framework/Inputs/HierarchicalTiff.h"
#include "../Framework/Inputs/OpenSlidePyramid.h"
#include "../Framework/Inputs/TiledJpegImage.h"
#include "../Framework/Inputs/TiledPngImage.h"
#include "../Framework/Inputs/TiledPyramidStatistics.h"
#include "../Framework/Orthanc/Core/HttpClient.h"
#include "../Framework/Orthanc/Core/Logging.h"
#include "../Framework/Orthanc/Core/MultiThreading/BagOfTasksProcessor.h"
#include "../Framework/Orthanc/Core/SystemToolbox.h"
#include "../Framework/Orthanc/OrthancServer/FromDcmtkBridge.h"
#include "../Framework/Outputs/DicomPyramidWriter.h"
#include "../Framework/Outputs/TruncatedPyramidWriter.h"

#include "ApplicationToolbox.h"

#include <EmbeddedResources.h>

#include <boost/program_options.hpp>

#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/dcmdata/dcvrobow.h>
#include <dcmtk/dcmdata/dcvrat.h>


static void TranscodePyramid(OrthancWSI::PyramidWriterBase& target,
                             OrthancWSI::ITiledPyramid& source,
                             const OrthancWSI::DicomizerParameters& parameters)
{
  Orthanc::BagOfTasks tasks;

  for (unsigned int i = 0; i < source.GetLevelCount(); i++)
  {
    LOG(WARNING) << "Creating level " << i << " of size " 
                 << source.GetLevelWidth(i) << "x" << source.GetLevelHeight(i);
    target.AddLevel(source.GetLevelWidth(i), source.GetLevelHeight(i));
  }

  LOG(WARNING) << "Transcoding the source pyramid";

  OrthancWSI::TranscodeTileCommand::PrepareBagOfTasks(tasks, target, source, parameters);
  OrthancWSI::ApplicationToolbox::Execute(tasks, parameters.GetThreadsCount());
}


static void ReconstructPyramid(OrthancWSI::PyramidWriterBase& target,
                               OrthancWSI::ITiledPyramid& source,
                               const OrthancWSI::DicomizerParameters& parameters)
{
  Orthanc::BagOfTasks tasks;

  unsigned int levelsCount = parameters.GetPyramidLevelsCount(target, source);
  LOG(WARNING) << "The target pyramid will have " << levelsCount << " levels";
  assert(levelsCount >= 1);

  for (unsigned int i = 0; i < levelsCount; i++)
  {
    unsigned int width = OrthancWSI::CeilingDivision(source.GetLevelWidth(0), 1 << i);
    unsigned int height = OrthancWSI::CeilingDivision(source.GetLevelHeight(0), 1 << i);

    LOG(WARNING) << "Creating level " << i << " of size " << width << "x" << height;
    target.AddLevel(width, height);
  }

  unsigned int lowerLevelsCount = parameters.GetPyramidLowerLevelsCount(target, source);
  if (lowerLevelsCount > levelsCount)
  {
    LOG(WARNING) << "The number of lower levels (" << lowerLevelsCount
                 << ") exceeds the number of levels (" << levelsCount
                 << "), cropping it";
    lowerLevelsCount = levelsCount;
  }

  assert(lowerLevelsCount <= levelsCount);
  if (lowerLevelsCount != levelsCount)
  {
    LOG(WARNING) << "Constructing the " << lowerLevelsCount << " lower levels of the pyramid";
    OrthancWSI::TruncatedPyramidWriter truncated(target, lowerLevelsCount);
    OrthancWSI::ReconstructPyramidCommand::PrepareBagOfTasks
      (tasks, truncated, source, lowerLevelsCount + 1, 0, parameters);
    OrthancWSI::ApplicationToolbox::Execute(tasks, parameters.GetThreadsCount());

    assert(tasks.GetSize() == 0);

    const unsigned int upperLevelsCount = levelsCount - lowerLevelsCount;
    LOG(WARNING) << "Constructing the " << upperLevelsCount << " upper levels of the pyramid";
    OrthancWSI::ReconstructPyramidCommand::PrepareBagOfTasks
      (tasks, target, truncated.GetUpperLevel(), 
       upperLevelsCount, lowerLevelsCount, parameters); 
    OrthancWSI::ApplicationToolbox::Execute(tasks, parameters.GetThreadsCount());
  }
  else
  {
    LOG(WARNING) << "Constructing the pyramid";
    OrthancWSI::ReconstructPyramidCommand::PrepareBagOfTasks
      (tasks, target, source, levelsCount, 0, parameters);
    OrthancWSI::ApplicationToolbox::Execute(tasks, parameters.GetThreadsCount());
  }
}


static void Recompress(OrthancWSI::IFileTarget& output,
                       OrthancWSI::ITiledPyramid& source,
                       const DcmDataset& dataset,
                       const OrthancWSI::DicomizerParameters& parameters,
                       const OrthancWSI::ImagedVolumeParameters& volume)
{
  OrthancWSI::TiledPyramidStatistics stats(source);

  LOG(WARNING) << "Size of source tiles: " << stats.GetTileWidth() << "x" << stats.GetTileHeight();
  LOG(WARNING) << "Source image compression: " << OrthancWSI::EnumerationToString(stats.GetImageCompression());
  LOG(WARNING) << "Pixel format: " << Orthanc::EnumerationToString(stats.GetPixelFormat());
  LOG(WARNING) << "Smoothing is " << (parameters.IsSmoothEnabled() ? "enabled" : "disabled");

  if (parameters.IsRepaintBackground())
  {
    LOG(WARNING) << "Repainting the background with color: (" 
                 << static_cast<int>(parameters.GetBackgroundColorRed()) << ","
                 << static_cast<int>(parameters.GetBackgroundColorGreen()) << ","
                 << static_cast<int>(parameters.GetBackgroundColorBlue()) << ")";
  }
  else
  {
    LOG(WARNING) << "No repainting of the background";
  }

  OrthancWSI::DicomPyramidWriter target(output, dataset, 
                                        source.GetPixelFormat(), 
                                        parameters.GetTargetCompression(),
                                        parameters.GetTargetTileWidth(source),
                                        parameters.GetTargetTileHeight(source),
                                        parameters.GetDicomMaxFileSize(),
                                        volume);
  target.SetJpegQuality(parameters.GetJpegQuality());

  LOG(WARNING) << "Size of target tiles: " << target.GetTileWidth() << "x" << target.GetTileHeight();

  if (target.GetImageCompression() == OrthancWSI::ImageCompression_Jpeg)
  {
    LOG(WARNING) << "Target image compression: Jpeg with quality " << static_cast<int>(target.GetJpegQuality());
    target.SetJpegQuality(target.GetJpegQuality());
  }
  else
  {
    LOG(WARNING) << "Target image compression: " << OrthancWSI::EnumerationToString(target.GetImageCompression());
  }

  if (stats.GetTileWidth() % target.GetTileWidth() != 0 ||
      stats.GetTileHeight() % target.GetTileHeight() != 0)
  {
    LOG(ERROR) << "When resampling the tile size, it must be a integer divisor of the original tile size";
    throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageSize);
  }

  if (target.GetTileWidth() <= 16 ||
      target.GetTileHeight() <= 16)
  {
    LOG(ERROR) << "Tiles are too small (16 pixels minimum): "
               << target.GetTileWidth() << "x" << target.GetTileHeight();
    throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageSize);
  }

  if (parameters.IsReconstructPyramid())
  {
    ReconstructPyramid(target, stats, parameters);
  }
  else
  {
    TranscodePyramid(target, stats, parameters);
  }

  target.Flush();
}



static DcmDataset* ParseDataset(const std::string& path)
{
  Json::Value json;

  if (path.empty())
  {
    json = Json::objectValue;   // Empty dataset => TODO EMBED
  }
  else
  {
    std::string content;
    Orthanc::SystemToolbox::ReadFile(content, path);

    Json::Reader reader;
    if (!reader.parse(content, json, false))
    {
      LOG(ERROR) << "Cannot parse the JSON file in: " << path;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
    }
  }

  std::auto_ptr<DcmDataset> dataset(Orthanc::FromDcmtkBridge::FromJson(json, true, true, Orthanc::Encoding_Latin1));
  if (dataset.get() == NULL)
  {
    LOG(ERROR) << "Cannot convert to JSON file to a DICOM dataset: " << path;
    throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
  }

  // VL Whole Slide Microscopy Image IOD
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_SOPClassUID, "1.2.840.10008.5.1.4.1.1.77.1.6");

  // Slide Microscopy
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_Modality, "SM");  

  // Patient orientation makes no sense in whole-slide images
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_PatientOrientation, "");  

  // Some basic coordinate information
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_VolumetricProperties, "VOLUME");
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_ImageOrientationSlide, "0\\-1\\0\\-1\\0\\0");

  std::string date, time;
  Orthanc::SystemToolbox::GetNowDicom(date, time);
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_StudyDate, date);
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_StudyTime, time);
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_SeriesDate, date);
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_SeriesTime, time);
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_ContentDate, date);
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_ContentTime, time);
  OrthancWSI::DicomToolbox::SetStringTag(*dataset, DCM_AcquisitionDateTime, date + time);

  return dataset.release();
}



static void SetupDimension(DcmDataset& dataset,
                           const std::string& opticalPathId,
                           const OrthancWSI::ITiledPyramid& source,
                           const OrthancWSI::ImagedVolumeParameters& volume)
{
  std::string uid;
  DcmItem* previous = OrthancWSI::DicomToolbox::ExtractSingleSequenceItem(dataset, DCM_DimensionOrganizationSequence);

  if (previous != NULL)
  {
    const char* tmp = NULL;
    if (previous->findAndGetString(DCM_DimensionOrganizationUID, tmp).good() &&
        tmp != NULL)
    {
      uid.assign(tmp);
    }
  }

  if (uid.empty())
  {
    // Generate an unique identifier for the Dimension Organization
    uid = Orthanc::FromDcmtkBridge::GenerateUniqueIdentifier(Orthanc::ResourceType_Instance);
  }

  dataset.remove(DCM_DimensionIndexSequence);

  std::auto_ptr<DcmItem> item(new DcmItem);
  std::auto_ptr<DcmSequenceOfItems> sequence(new DcmSequenceOfItems(DCM_DimensionOrganizationSequence));

  if (!item->putAndInsertString(DCM_DimensionOrganizationUID, uid.c_str()).good() ||
      !sequence->insert(item.release(), false, false).good() ||
      !dataset.insert(sequence.release(), true, false).good())
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
  }

  item.reset(new DcmItem);
  sequence.reset(new DcmSequenceOfItems(DCM_DimensionIndexSequence));

  std::auto_ptr<DcmAttributeTag> a1(new DcmAttributeTag(DCM_FunctionalGroupPointer));
  std::auto_ptr<DcmAttributeTag> a2(new DcmAttributeTag(DCM_DimensionIndexPointer));

  if (!item->putAndInsertString(DCM_DimensionOrganizationUID, uid.c_str()).good() ||
      !a1->putTagVal(DCM_FrameContentSequence).good() ||
      !a2->putTagVal(DCM_DimensionIndexValues).good() ||
      !item->insert(a1.release()).good() ||
      !item->insert(a2.release()).good() ||
      !sequence->insert(item.release(), false, false).good() ||
      !dataset.insert(sequence.release(), true, false).good())
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
  }

  float spacingX = volume.GetWidth() / static_cast<float>(source.GetLevelHeight(0));  // Remember to switch X/Y!
  float spacingY = volume.GetHeight() / static_cast<float>(source.GetLevelWidth(0));  // Remember to switch X/Y!
  std::string spacing = (boost::lexical_cast<std::string>(spacingX) + '\\' +
                         boost::lexical_cast<std::string>(spacingY));

  item.reset(new DcmItem);
  sequence.reset(new DcmSequenceOfItems(DCM_SharedFunctionalGroupsSequence));
  std::auto_ptr<DcmItem> item2(new DcmItem);
  std::auto_ptr<DcmItem> item3(new DcmItem);
  std::auto_ptr<DcmSequenceOfItems> sequence2(new DcmSequenceOfItems(DCM_PixelMeasuresSequence));
  std::auto_ptr<DcmSequenceOfItems> sequence3(new DcmSequenceOfItems(DCM_OpticalPathIdentificationSequence));

  OrthancWSI::DicomToolbox::SetStringTag(*item2, DCM_SliceThickness, boost::lexical_cast<std::string>(volume.GetDepth()));
  OrthancWSI::DicomToolbox::SetStringTag(*item2, DCM_PixelSpacing, spacing);
  OrthancWSI::DicomToolbox::SetStringTag(*item3, DCM_OpticalPathIdentifier, opticalPathId);

  if (!sequence2->insert(item2.release(), false, false).good() ||
      !sequence3->insert(item3.release(), false, false).good() ||
      !item->insert(sequence2.release(), false, false).good() ||
      !item->insert(sequence3.release(), false, false).good() ||
      !sequence->insert(item.release(), false, false).good() ||
      !dataset.insert(sequence.release(), true, false).good())
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
  }
}


static void EnrichDataset(DcmDataset& dataset,
                          const OrthancWSI::ITiledPyramid& source,
                          const OrthancWSI::DicomizerParameters& parameters,
                          const OrthancWSI::ImagedVolumeParameters& volume)
{
  Orthanc::Encoding encoding = Orthanc::FromDcmtkBridge::DetectEncoding(dataset, Orthanc::Encoding_Latin1);

  if (source.GetImageCompression() == OrthancWSI::ImageCompression_Jpeg ||
      parameters.GetTargetCompression() == OrthancWSI::ImageCompression_Jpeg)
  {
    // Takes as estimation a 1:10 compression ratio
    OrthancWSI::DicomToolbox::SetStringTag(dataset, DCM_LossyImageCompression, "01");
    OrthancWSI::DicomToolbox::SetStringTag(dataset, DCM_LossyImageCompressionRatio, "10");
    OrthancWSI::DicomToolbox::SetStringTag(dataset, DCM_LossyImageCompressionMethod, "ISO_10918_1"); // JPEG Lossy Compression
  }
  else
  {
    OrthancWSI::DicomToolbox::SetStringTag(dataset, DCM_LossyImageCompression, "00");
  }

  OrthancWSI::DicomToolbox::SetStringTag(dataset, DCM_ImagedVolumeWidth, boost::lexical_cast<std::string>(volume.GetWidth()));
  OrthancWSI::DicomToolbox::SetStringTag(dataset, DCM_ImagedVolumeHeight, boost::lexical_cast<std::string>(volume.GetHeight()));
  OrthancWSI::DicomToolbox::SetStringTag(dataset, DCM_ImagedVolumeDepth, boost::lexical_cast<std::string>(volume.GetDepth()));

  std::auto_ptr<DcmItem> origin(new DcmItem);
  OrthancWSI::DicomToolbox::SetStringTag(*origin, DCM_XOffsetInSlideCoordinateSystem, 
                                         boost::lexical_cast<std::string>(volume.GetOffsetX()));
  OrthancWSI::DicomToolbox::SetStringTag(*origin, DCM_YOffsetInSlideCoordinateSystem, 
                                         boost::lexical_cast<std::string>(volume.GetOffsetY()));

  std::auto_ptr<DcmSequenceOfItems> sequenceOrigin(new DcmSequenceOfItems(DCM_TotalPixelMatrixOriginSequence));
  if (!sequenceOrigin->insert(origin.release(), false, false).good() ||
      !dataset.insert(sequenceOrigin.release(), false, false).good())
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
  }


  if (parameters.GetOpticalPath() == OrthancWSI::OpticalPath_Brightfield)
  {
    if (dataset.tagExists(DCM_OpticalPathSequence))
    {
      LOG(ERROR) << "The user DICOM dataset already contains an optical path sequence, giving up";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);      
    }

    std::string brightfield;
    Orthanc::EmbeddedResources::GetFileResource(brightfield, Orthanc::EmbeddedResources::BRIGHTFIELD_OPTICAL_PATH);

    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(brightfield, json, false))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }

    std::auto_ptr<DcmElement> element(Orthanc::FromDcmtkBridge::FromJson(
                                        Orthanc::DicomTag(DCM_OpticalPathSequence.getGroup(),
                                                          DCM_OpticalPathSequence.getElement()),
                                        json, false, encoding));
    if (!dataset.insert(element.release()).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
  }


  std::string profile;
  if (parameters.GetIccProfilePath().empty())
  {
    Orthanc::EmbeddedResources::GetFileResource(profile, Orthanc::EmbeddedResources::SRGB_ICC_PROFILE);
  }
  else
  {
    Orthanc::SystemToolbox::ReadFile(profile, parameters.GetIccProfilePath());
  }

  
  DcmItem* opticalPath = OrthancWSI::DicomToolbox::ExtractSingleSequenceItem(dataset, DCM_OpticalPathSequence);
  if (opticalPath == NULL)
  {
    LOG(ERROR) << "No optical path specified";
    throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
  }

  if (!opticalPath->tagExists(DCM_ICCProfile))
  {
    std::auto_ptr<DcmOtherByteOtherWord> icc(new DcmOtherByteOtherWord(DCM_ICCProfile));

    if (!icc->putUint8Array(reinterpret_cast<const Uint8*>(profile.c_str()), profile.size()).good() ||
        !opticalPath->insert(icc.release()).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
  }

  const char* opticalPathId = NULL;
  if (!opticalPath->findAndGetString(DCM_OpticalPathIdentifier, opticalPathId).good() ||
      opticalPathId == NULL)
  {
    LOG(ERROR) << "No identifier in the optical path";
    throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);    
  }

  SetupDimension(dataset, opticalPathId, source, volume);
}


static bool ParseParameters(int& exitStatus,
                            OrthancWSI::DicomizerParameters& parameters,
                            OrthancWSI::ImagedVolumeParameters& volume,
                            int argc, 
                            char* argv[])
{
  // Declare the supported parameters
  boost::program_options::options_description generic("Generic options");
  generic.add_options()
    ("help", "Display this help and exit")
    ("version", "Output version information and exit")
    ("verbose", "Be verbose in logs")
    ("threads", boost::program_options::value<int>()->default_value(parameters.GetThreadsCount()), 
     "Number of processing threads to be used")
    ("openslide", boost::program_options::value<std::string>(), 
     "Path to the shared library of OpenSlide (not necessary if converting from standard hierarchical TIFF)")
    ;

  boost::program_options::options_description source("Options for the source image");
  source.add_options()
    ("dataset", boost::program_options::value<std::string>(), "Path to a JSON file containing the DICOM dataset")
    ("sample-dataset", "Display a minimalistic sample DICOM dataset in JSON format, then exit")
    ("reencode", boost::program_options::value<bool>(), "Whether to re-encode each tile (no transcoding, much slower) (Boolean)")
    ("repaint", boost::program_options::value<bool>(), "Whether to repaint the background of the image (Boolean)")
    ("color", boost::program_options::value<std::string>(), "Color of the background (e.g. \"255,0,0\")")
    ;

  boost::program_options::options_description pyramid("Options to construct the pyramid");
  pyramid.add_options()
    ("pyramid", boost::program_options::value<bool>()->default_value(false), 
     "Reconstruct the full pyramid (slow) (Boolean)")
    ("smooth", boost::program_options::value<bool>()->default_value(false), 
     "Apply smoothing when reconstructing the pyramid "
     "(slower, but higher quality) (Boolean)")
    ("levels", boost::program_options::value<int>(), "Number of levels in the target pyramid")
    ;

  boost::program_options::options_description target("Options for the target image");
  target.add_options()
    ("tile-width", boost::program_options::value<int>(), "Width of the tiles in the target image")
    ("tile-height", boost::program_options::value<int>(), "Height of the tiles in the target image")
    ("compression", boost::program_options::value<std::string>(), 
     "Compression of the target image (\"none\", \"jpeg\" or \"jpeg2000\")")
    ("jpeg-quality", boost::program_options::value<int>(), "Set quality level for JPEG (0..100)")
    ("max-size", boost::program_options::value<int>()->default_value(10), "Maximum size per DICOM instance (in MB)")
    ("folder", boost::program_options::value<std::string>(),
     "Folder where to store the output DICOM instances")
    ("folder-pattern", boost::program_options::value<std::string>()->default_value("wsi-%06d.dcm"),
     "Pattern for the files in the output folder")
    ("orthanc", boost::program_options::value<std::string>()->default_value("http://localhost:8042/"),
     "URL to the REST API of the target Orthanc server")
    ("username", boost::program_options::value<std::string>(), "Username for the target Orthanc server")
    ("password", boost::program_options::value<std::string>(), "Password for the target Orthanc server")
    ;

  boost::program_options::options_description volumeOptions("Description of the imaged volume");
  volumeOptions.add_options()
    ("imaged-width", boost::program_options::value<float>()->default_value(15), "With of the specimen (in mm)")
    ("imaged-height", boost::program_options::value<float>()->default_value(15), "Height of the specimen (in mm)")
    ("imaged-depth", boost::program_options::value<float>()->default_value(1), "Depth of the specimen (in mm)")
    ("offset-x", boost::program_options::value<float>()->default_value(20), 
     "X offset the specimen, wrt. slide coordinates origin (in mm)")
    ("offset-y", boost::program_options::value<float>()->default_value(40), 
     "Y offset the specimen, wrt. slide coordinates origin (in mm)")
    ;

  boost::program_options::options_description advancedOptions("Advanced options");
  advancedOptions.add_options()
    ("optical-path", boost::program_options::value<std::string>()->default_value("brightfield"), 
     "Optical path to be automatically added to the DICOM dataset (\"none\" or \"brightfield\")")
    ("icc-profile", boost::program_options::value<std::string>(), 
     "Path to the ICC profile to be included. If empty, a default sRGB profile will be added.")
    ("safety", boost::program_options::value<bool>()->default_value(true), 
     "Whether to do additional checks to verify the source image is supported (might slow down) (Boolean)")
    ("lower-levels", boost::program_options::value<int>(), "Number of pyramid levels up to which multithreading "
     "should be applied (only for performance/memory tuning)")
    ;

  boost::program_options::options_description hidden;
  hidden.add_options()
    ("input", boost::program_options::value<std::string>(), "Input file");
  ;

  boost::program_options::options_description allWithoutHidden;
  allWithoutHidden.add(generic).add(source).add(pyramid).add(target).add(volumeOptions).add(advancedOptions);

  boost::program_options::options_description all = allWithoutHidden;
  all.add(hidden);

  boost::program_options::positional_options_description positional;
  positional.add("input", 1);

  boost::program_options::variables_map options;
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
      options.count("sample-dataset"))
  {
    std::string sample;
    Orthanc::EmbeddedResources::GetFileResource(sample, Orthanc::EmbeddedResources::SAMPLE_DATASET);

    std::cout << std::endl << sample << std::endl;

    return false;
  }

  if (!error &&
      options.count("help") == 0 &&
      options.count("version") == 0 &&
      options.count("input") != 1)
  {
    LOG(ERROR) << "No input file was specified";
    error = true;
  }

  if (error || options.count("help")) 
  {
    std::cout << std::endl
              << "Usage: " << argv[0] << " [OPTION]... [INPUT]"
              << std::endl
              << "Orthanc, lightweight, RESTful DICOM server for healthcare and medical research."
              << std::endl << std::endl
              << "Create a DICOM file from a digital pathology image."
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

  if (options.count("openslide"))
  {
    OrthancWSI::OpenSlideLibrary::Initialize(options["openslide"].as<std::string>());
  }

  if (options.count("pyramid") &&
      options["pyramid"].as<bool>())
  {
    parameters.SetReconstructPyramid(true);
  }

  if (options.count("smooth") &&
      options["smooth"].as<bool>())
  {
    parameters.SetSmoothEnabled(true);
  }

  if (options.count("safety") &&
      options["safety"].as<bool>())
  {
    parameters.SetSafetyCheck(true);
  }

  if (options.count("reencode") &&
      options["reencode"].as<bool>())
  {
    parameters.SetForceReencode(true);
  }

  if (options.count("repaint") &&
      options["repaint"].as<bool>())
  {
    parameters.SetRepaintBackground(true);
  }

  if (options.count("tile-width") ||
      options.count("tile-height"))
  {
    int w = 0;
    if (options.count("tile-width"))
    {
      w = options["tile-width"].as<int>();
    }

    unsigned int h = 0;
    if (options.count("tile-height"))
    {
      h = options["tile-height"].as<int>();
    }

    if (w < 0 || h < 0)
    {
      LOG(ERROR) << "Negative target tile size specified: " << w << "x" << h;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    parameters.SetTargetTileSize(w, h);
  }

  parameters.SetInputFile(options["input"].as<std::string>());

  if (options.count("color"))
  {
    uint8_t r, g, b;
    OrthancWSI::ApplicationToolbox::ParseColor(r, g, b, options["color"].as<std::string>());
    parameters.SetBackgroundColor(r, g, b);
  }

  if (options.count("compression"))
  {
    std::string s = options["compression"].as<std::string>();
    if (s == "none")
    {
      parameters.SetTargetCompression(OrthancWSI::ImageCompression_None);
    }
    else if (s == "jpeg")
    {
      parameters.SetTargetCompression(OrthancWSI::ImageCompression_Jpeg);
    }
    else if (s == "jpeg2000")
    {
      parameters.SetTargetCompression(OrthancWSI::ImageCompression_Jpeg2000);
    }
    else
    {
      LOG(ERROR) << "Unknown image compression for the target image: " << s;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }

  if (options.count("jpeg-quality"))
  {
    parameters.SetJpegQuality(options["jpeg-quality"].as<int>());
  }

  if (options.count("levels"))
  {
    parameters.SetPyramidLevelsCount(options["levels"].as<int>());
  }

  if (options.count("lower-levels"))
  {
    parameters.SetPyramidLowerLevelsCount(options["lower-levels"].as<int>());
  }

  if (options.count("threads"))
  {
    parameters.SetThreadsCount(options["threads"].as<int>());
  }

  if (options.count("max-size"))
  {
    parameters.SetDicomMaxFileSize(options["max-size"].as<int>() * 1024 * 1024);
  }

  if (options.count("folder"))
  {
    parameters.SetTargetFolder(options["folder"].as<std::string>());
  }

  if (options.count("folder-pattern"))
  {
    parameters.SetTargetFolderPattern(options["folder-pattern"].as<std::string>());
  }

  if (options.count("orthanc"))
  {
    parameters.GetOrthancParameters().SetUrl(options["orthanc"].as<std::string>());

    if (options.count("username") &&
        options.count("password"))
    {
      parameters.GetOrthancParameters().SetUsername(options["username"].as<std::string>());
      parameters.GetOrthancParameters().SetPassword(options["password"].as<std::string>());
    }
  }

  if (options.count("dataset"))
  {
    parameters.SetDatasetPath(options["dataset"].as<std::string>());
  }

  if (options.count("imaged-width"))
  {
    volume.SetWidth(options["imaged-width"].as<float>());
  }

  if (options.count("imaged-height"))
  {
    volume.SetHeight(options["imaged-height"].as<float>());
  }

  if (options.count("imaged-depth"))
  {
    volume.SetDepth(options["imaged-depth"].as<float>());
  }

  if (options.count("offset-x"))
  {
    volume.SetOffsetX(options["offset-x"].as<float>());
  }

  if (options.count("offset-y"))
  {
    volume.SetOffsetY(options["offset-y"].as<float>());
  }

  if (options.count("optical-path"))
  {
    std::string s = options["optical-path"].as<std::string>();
    if (s == "none")
    {
      parameters.SetOpticalPath(OrthancWSI::OpticalPath_None);
    }
    else if (s == "brightfield")
    {
      parameters.SetOpticalPath(OrthancWSI::OpticalPath_Brightfield);
    }
    else
    {
      LOG(ERROR) << "Unknown optical path definition: " << s;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }

  if (options.count("icc-profile"))
  {
    parameters.SetIccProfilePath(options["icc-profile"].as<std::string>());
  }

  return true;
}


OrthancWSI::ITiledPyramid* OpenInputPyramid(const std::string& path,
                                            const OrthancWSI::DicomizerParameters& parameters)
{
  LOG(WARNING) << "The input image is: " << path;

  OrthancWSI::ImageCompression format = OrthancWSI::DetectFormatFromFile(path);
  LOG(WARNING) << "File format of the input image: " << EnumerationToString(format);

  switch (format)
  {
    case OrthancWSI::ImageCompression_Png:
      return new OrthancWSI::TiledPngImage(path, 
                                           parameters.GetTargetTileWidth(512), 
                                           parameters.GetTargetTileHeight(512));

    case OrthancWSI::ImageCompression_Jpeg:
      return new OrthancWSI::TiledJpegImage(path, 
                                            parameters.GetTargetTileWidth(512), 
                                            parameters.GetTargetTileHeight(512));

    case OrthancWSI::ImageCompression_Tiff:
    {
      try
      {
        return new OrthancWSI::HierarchicalTiff(path);
      }
      catch (Orthanc::OrthancException&)
      {
        LOG(WARNING) << "This is not a standard hierarchical TIFF file";
      }
    }

    default:
      break;
  }

  try
  {
    LOG(WARNING) << "Trying to open the input pyramid with OpenSlide";
    return new OrthancWSI::OpenSlidePyramid(path, 
                                            parameters.GetTargetTileWidth(512), 
                                            parameters.GetTargetTileHeight(512));
  }
  catch (Orthanc::OrthancException&)
  {
    LOG(ERROR) << "This file is not supported by OpenSlide";
    return NULL;
  }
}


int main(int argc, char* argv[])
{
  OrthancWSI::ApplicationToolbox::GlobalInitialize();

  int exitStatus = 0;

  try
  {
    OrthancWSI::DicomizerParameters  parameters;
    OrthancWSI::ImagedVolumeParameters volume;

    if (ParseParameters(exitStatus, parameters, volume, argc, argv))
    {
      std::auto_ptr<OrthancWSI::ITiledPyramid> source(OpenInputPyramid(parameters.GetInputFile(), parameters));
      if (source.get() == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }
      
      // Create the shared DICOM tags
      std::auto_ptr<DcmDataset> dataset(ParseDataset(parameters.GetDatasetPath()));
      EnrichDataset(*dataset, *source, parameters, volume);

      std::auto_ptr<OrthancWSI::IFileTarget> output(parameters.CreateTarget());
      Recompress(*output, *source, *dataset, parameters, volume);
    }
  }
  catch (Orthanc::OrthancException& e)
  {
    LOG(ERROR) << "Terminating on exception: " << e.What();
    exitStatus = -1;
  }

  OrthancWSI::ApplicationToolbox::GlobalFinalize();

  return exitStatus;
}
