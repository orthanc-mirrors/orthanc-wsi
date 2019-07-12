/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2019 Osimis S.A., Belgium
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
#include "../Framework/MultiThreading/BagOfTasksProcessor.h"
#include "../Framework/Outputs/DicomPyramidWriter.h"
#include "../Framework/Outputs/TruncatedPyramidWriter.h"

#include <Core/DicomParsing/FromDcmtkBridge.h>
#include <Core/Logging.h>
#include <Core/OrthancException.h>
#include <Core/SystemToolbox.h>

#include "ApplicationToolbox.h"

#include <EmbeddedResources.h>

#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/dcmdata/dcvrobow.h>
#include <dcmtk/dcmdata/dcvrat.h>


static const char* OPTION_COLOR = "color";
static const char* OPTION_COMPRESSION = "compression";
static const char* OPTION_DATASET = "dataset";
static const char* OPTION_FOLDER = "folder";
static const char* OPTION_FOLDER_PATTERN = "folder-pattern";
static const char* OPTION_HELP = "help";
static const char* OPTION_ICC_PROFILE = "icc-profile";
static const char* OPTION_IMAGED_DEPTH = "imaged-depth";
static const char* OPTION_IMAGED_HEIGHT = "imaged-height";
static const char* OPTION_IMAGED_WIDTH = "imaged-width";
static const char* OPTION_INPUT = "input";
static const char* OPTION_JPEG_QUALITY = "jpeg-quality";
static const char* OPTION_LEVELS = "levels";
static const char* OPTION_LOWER_LEVELS = "lower-levels";
static const char* OPTION_MAX_SIZE = "max-size";
static const char* OPTION_OFFSET_X = "offset-x";
static const char* OPTION_OFFSET_Y = "offset-y";
static const char* OPTION_OPENSLIDE = "openslide";
static const char* OPTION_OPTICAL_PATH = "optical-path";
static const char* OPTION_PYRAMID = "pyramid";
static const char* OPTION_REENCODE = "reencode";
static const char* OPTION_REPAINT = "repaint";
static const char* OPTION_SAFETY = "safety";
static const char* OPTION_SAMPLE_DATASET = "sample-dataset";
static const char* OPTION_SMOOTH = "smooth";
static const char* OPTION_THREADS = "threads";
static const char* OPTION_TILE_HEIGHT = "tile-height";
static const char* OPTION_TILE_WIDTH = "tile-width";
static const char* OPTION_VERBOSE = "verbose";
static const char* OPTION_VERSION = "version";


static void TranscodePyramid(OrthancWSI::PyramidWriterBase& target,
                             OrthancWSI::ITiledPyramid& source,
                             const OrthancWSI::DicomizerParameters& parameters)
{
  LOG(WARNING) << "Transcoding the source pyramid (not re-encoding)";

  Orthanc::BagOfTasks tasks;

  for (unsigned int i = 0; i < source.GetLevelCount(); i++)
  {
    LOG(WARNING) << "Creating level " << i << " of size " 
                 << source.GetLevelWidth(i) << "x" << source.GetLevelHeight(i);
    target.AddLevel(source.GetLevelWidth(i), source.GetLevelHeight(i));
  }

  OrthancWSI::TranscodeTileCommand::PrepareBagOfTasks(tasks, target, source, parameters);
  OrthancWSI::ApplicationToolbox::Execute(tasks, parameters.GetThreadsCount());
}


static void ReconstructPyramid(OrthancWSI::PyramidWriterBase& target,
                               OrthancWSI::ITiledPyramid& source,
                               const OrthancWSI::DicomizerParameters& parameters)
{
  LOG(WARNING) << "Re-encoding the source pyramid (not transcoding, slower process)";

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
    OrthancWSI::TruncatedPyramidWriter truncated(target, lowerLevelsCount, source.GetPhotometricInterpretation());
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
                       const OrthancWSI::ImagedVolumeParameters& volume,
                       OrthancWSI::ImageCompression sourceCompression)
{
  OrthancWSI::TiledPyramidStatistics stats(source);

  LOG(WARNING) << "Size of source tiles: " << stats.GetTileWidth() << "x" << stats.GetTileHeight();
  LOG(WARNING) << "Pixel format: " << Orthanc::EnumerationToString(source.GetPixelFormat());
  LOG(WARNING) << "Source photometric interpretation: " << Orthanc::EnumerationToString(source.GetPhotometricInterpretation());
  LOG(WARNING) << "Source compression: " << EnumerationToString(sourceCompression);
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

  Orthanc::PhotometricInterpretation targetPhotometric;
  bool transcoding;

  if (parameters.IsForceReencode() ||
      parameters.IsReconstructPyramid() ||
      sourceCompression != parameters.GetTargetCompression())
  {
    // The tiles of the source image will be re-encoded
    transcoding = false;
    
    switch (parameters.GetTargetCompression())
    {
      case OrthancWSI::ImageCompression_Jpeg:
      case OrthancWSI::ImageCompression_Jpeg2000:
        targetPhotometric = Orthanc::PhotometricInterpretation_YBRFull422;
        break;

      case OrthancWSI::ImageCompression_None:
        targetPhotometric = Orthanc::PhotometricInterpretation_RGB;
        break;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
  }
  else
  {
    // Transcoding: The tiles are copied (no re-encoding)
    transcoding = true;
    targetPhotometric = source.GetPhotometricInterpretation();
  }
  
  OrthancWSI::DicomPyramidWriter target(output, dataset, 
                                        source.GetPixelFormat(), 
                                        parameters.GetTargetCompression(),
                                        parameters.GetTargetTileWidth(source),
                                        parameters.GetTargetTileHeight(source),
                                        parameters.GetDicomMaxFileSize(),
                                        volume, targetPhotometric);
  target.SetJpegQuality(parameters.GetJpegQuality());

  LOG(WARNING) << "Size of target tiles: " << target.GetTileWidth() << "x" << target.GetTileHeight();
  LOG(WARNING) << "Target photometric interpretation: " << Orthanc::EnumerationToString(targetPhotometric);

  if (!transcoding &&
      target.GetImageCompression() == OrthancWSI::ImageCompression_Jpeg)
  {
    LOG(WARNING) << "Target compression: Jpeg with quality "
                 << static_cast<int>(target.GetJpegQuality());
    target.SetJpegQuality(target.GetJpegQuality());
  }
  else
  {
    LOG(WARNING) << "Target compression: "
                 << OrthancWSI::EnumerationToString(target.GetImageCompression());
  }

  if (stats.GetTileWidth() % target.GetTileWidth() != 0 ||
      stats.GetTileHeight() % target.GetTileHeight() != 0)
  {
    LOG(ERROR) << "When resampling the tile size, "
               << "it must be a integer divisor of the original tile size";
    throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageSize);
  }

  if (target.GetTileWidth() <= 16 ||
      target.GetTileHeight() <= 16)
  {
    LOG(ERROR) << "Tiles are too small (16 pixels minimum): "
               << target.GetTileWidth() << "x" << target.GetTileHeight();
    throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleImageSize);
  }

  if (transcoding)
  {
    TranscodePyramid(target, stats, parameters);
  }
  else
  {
    ReconstructPyramid(target, stats, parameters);
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
  Orthanc::SystemToolbox::GetNowDicom(date, time, true /* use UTC time (not local time) */);
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
  // Extract the identifier of the Dimension Organization, if provided
  std::string organization;
  DcmItem* previous = OrthancWSI::DicomToolbox::ExtractSingleSequenceItem(dataset, DCM_DimensionOrganizationSequence);

  if (previous != NULL &&
      previous->tagExists(DCM_DimensionOrganizationUID))
  {
    organization = OrthancWSI::DicomToolbox::GetStringTag(*previous, DCM_DimensionOrganizationUID);
  }
  else
  {
    // No Dimension Organization provided: Generate an unique identifier
    organization = Orthanc::FromDcmtkBridge::GenerateUniqueIdentifier(Orthanc::ResourceType_Instance);
  }


  {
    // Construct tag "Dimension Organization Sequence" (0020,9221)
    std::auto_ptr<DcmItem> item(new DcmItem);
    OrthancWSI::DicomToolbox::SetStringTag(*item, DCM_DimensionOrganizationUID, organization);
    
    std::auto_ptr<DcmSequenceOfItems> sequence(new DcmSequenceOfItems(DCM_DimensionOrganizationSequence));

    if (!sequence->insert(item.release(), false, false).good() ||
        !dataset.insert(sequence.release(), true /* replace */, false).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
  }


  {
    // Construct tag "Dimension Index Sequence" (0020,9222)
    std::auto_ptr<DcmItem> item(new DcmItem);
    OrthancWSI::DicomToolbox::SetStringTag(*item, DCM_DimensionOrganizationUID, organization);
    OrthancWSI::DicomToolbox::SetAttributeTag(*item, DCM_FunctionalGroupPointer, DCM_PlanePositionSlideSequence);
    OrthancWSI::DicomToolbox::SetAttributeTag(*item, DCM_DimensionIndexPointer, DCM_ColumnPositionInTotalImagePixelMatrix);

    std::auto_ptr<DcmItem> item2(new DcmItem);
    OrthancWSI::DicomToolbox::SetStringTag(*item2, DCM_DimensionOrganizationUID, organization);
    OrthancWSI::DicomToolbox::SetAttributeTag(*item2, DCM_FunctionalGroupPointer, DCM_PlanePositionSlideSequence);
    OrthancWSI::DicomToolbox::SetAttributeTag(*item2, DCM_DimensionIndexPointer, DCM_RowPositionInTotalImagePixelMatrix);

    std::auto_ptr<DcmSequenceOfItems> sequence(new DcmSequenceOfItems(DCM_DimensionIndexSequence));

    if (!sequence->insert(item.release(), false, false).good() ||
        !sequence->insert(item2.release(), false, false).good() ||
        !dataset.insert(sequence.release(), true /* replace */, false).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
  }


  {
    // Construct tag "Shared Functional Groups Sequence" (5200,9229)

    // In the 2 lines below, remember to switch X/Y when going from physical to pixel coordinates!
    float spacingX = volume.GetWidth() / static_cast<float>(source.GetLevelHeight(0));
    float spacingY = volume.GetHeight() / static_cast<float>(source.GetLevelWidth(0));

    std::string spacing = (boost::lexical_cast<std::string>(spacingX) + '\\' +
                           boost::lexical_cast<std::string>(spacingY));

    std::auto_ptr<DcmItem> item(new DcmItem);

    std::auto_ptr<DcmItem> item2(new DcmItem);
    OrthancWSI::DicomToolbox::SetStringTag(*item2, DCM_SliceThickness, 
                                           boost::lexical_cast<std::string>(volume.GetDepth()));
    OrthancWSI::DicomToolbox::SetStringTag(*item2, DCM_PixelSpacing, spacing);

    std::auto_ptr<DcmItem> item3(new DcmItem);
    OrthancWSI::DicomToolbox::SetStringTag(*item3, DCM_OpticalPathIdentifier, opticalPathId);

    std::auto_ptr<DcmSequenceOfItems> sequence(new DcmSequenceOfItems(DCM_SharedFunctionalGroupsSequence));
    std::auto_ptr<DcmSequenceOfItems> sequence2(new DcmSequenceOfItems(DCM_PixelMeasuresSequence));
    std::auto_ptr<DcmSequenceOfItems> sequence3(new DcmSequenceOfItems(DCM_OpticalPathIdentificationSequence));

    if (!sequence2->insert(item2.release(), false, false).good() ||
        !sequence3->insert(item3.release(), false, false).good() ||
        !item->insert(sequence2.release(), false, false).good() ||
        !item->insert(sequence3.release(), false, false).good() ||
        !sequence->insert(item.release(), false, false).good() ||
        !dataset.insert(sequence.release(), true /* replace */, false).good())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
  }
}


static void EnrichDataset(DcmDataset& dataset,
                          const OrthancWSI::ITiledPyramid& source,
                          OrthancWSI::ImageCompression sourceCompression,
                          const OrthancWSI::DicomizerParameters& parameters,
                          const OrthancWSI::ImagedVolumeParameters& volume)
{
  Orthanc::Encoding encoding = Orthanc::FromDcmtkBridge::DetectEncoding(dataset, Orthanc::Encoding_Latin1);

  if (sourceCompression == OrthancWSI::ImageCompression_Jpeg ||
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
    (OPTION_HELP,    "Display this help and exit")
    (OPTION_VERSION, "Output version information and exit")
    (OPTION_VERBOSE, "Be verbose in logs")
    (OPTION_THREADS,
     boost::program_options::value<int>()->default_value(parameters.GetThreadsCount()), 
     "Number of processing threads to be used")
    (OPTION_OPENSLIDE, boost::program_options::value<std::string>(), 
     "Path to the shared library of OpenSlide "
     "(not necessary if converting from standard hierarchical TIFF)")
    ;

  boost::program_options::options_description source("Options for the source image");
  source.add_options()
    (OPTION_DATASET, boost::program_options::value<std::string>(),
     "Path to a JSON file containing the DICOM dataset")
    (OPTION_SAMPLE_DATASET,
     "Display a minimalistic sample DICOM dataset in JSON format, then exit")
    (OPTION_REENCODE, boost::program_options::value<bool>(),
     "Whether to re-encode each tile (no transcoding, much slower) (Boolean)")
    (OPTION_REPAINT, boost::program_options::value<bool>(),
     "Whether to repaint the background of the image (Boolean)")
    (OPTION_COLOR, boost::program_options::value<std::string>(),
     "Color of the background (e.g. \"255,0,0\")")
    ;

  boost::program_options::options_description pyramid("Options to construct the pyramid");
  pyramid.add_options()
    (OPTION_PYRAMID, boost::program_options::value<bool>()->default_value(false), 
     "Reconstruct the full pyramid (slow) (Boolean)")
    (OPTION_SMOOTH, boost::program_options::value<bool>()->default_value(false), 
     "Apply smoothing when reconstructing the pyramid "
     "(slower, but higher quality) (Boolean)")
    (OPTION_LEVELS, boost::program_options::value<int>(),
     "Number of levels in the target pyramid")
    ;

  boost::program_options::options_description target("Options for the target image");
  target.add_options()
    (OPTION_TILE_WIDTH, boost::program_options::value<int>(),
     "Width of the tiles in the target image")
    (OPTION_TILE_HEIGHT, boost::program_options::value<int>(),
     "Height of the tiles in the target image")
    (OPTION_COMPRESSION, boost::program_options::value<std::string>(), 
     "Compression of the target image (\"none\", \"jpeg\" or \"jpeg2000\")")
    (OPTION_JPEG_QUALITY, boost::program_options::value<int>(),
     "Set quality level for JPEG (0..100)")
    (OPTION_MAX_SIZE, boost::program_options::value<int>()->default_value(10),
     "Maximum size per DICOM instance (in MB), 0 means no limit on the file size")
    (OPTION_FOLDER, boost::program_options::value<std::string>(),
     "Folder where to store the output DICOM instances")
    (OPTION_FOLDER_PATTERN,
     boost::program_options::value<std::string>()->default_value("wsi-%06d.dcm"),
     "Pattern for the files in the output folder")
    ("orthanc", boost::program_options::value<std::string>()->default_value("http://localhost:8042/"),
     "URL to the REST API of the target Orthanc server")
    ;

  boost::program_options::options_description volumeOptions("Description of the imaged volume");
  volumeOptions.add_options()
    (OPTION_IMAGED_WIDTH, boost::program_options::value<float>()->default_value(15),
     "Width of the specimen (in mm)")
    (OPTION_IMAGED_HEIGHT, boost::program_options::value<float>()->default_value(15),
     "Height of the specimen (in mm)")
    (OPTION_IMAGED_DEPTH, boost::program_options::value<float>()->default_value(1),
     "Depth of the specimen (in mm)")
    (OPTION_OFFSET_X, boost::program_options::value<float>()->default_value(20), 
     "X offset the specimen, wrt. slide coordinates origin (in mm)")
    (OPTION_OFFSET_Y, boost::program_options::value<float>()->default_value(40), 
     "Y offset the specimen, wrt. slide coordinates origin (in mm)")
    ;

  boost::program_options::options_description restOptions
    ("HTTP/HTTPS client configuration to access the Orthanc REST API");
  OrthancWSI::ApplicationToolbox::AddRestApiOptions(restOptions);

  boost::program_options::options_description advancedOptions("Advanced options");
  advancedOptions.add_options()
    (OPTION_OPTICAL_PATH, boost::program_options::value<std::string>()->default_value("brightfield"), 
     "Optical path to be automatically added to the DICOM dataset (\"none\" or \"brightfield\")")
    (OPTION_ICC_PROFILE, boost::program_options::value<std::string>(), 
     "Path to the ICC profile to be included. If empty, a default sRGB profile will be added.")
    (OPTION_SAFETY, boost::program_options::value<bool>()->default_value(true), 
     "Whether to do additional checks to verify the source image is supported (might slow down) (Boolean)")
    (OPTION_LOWER_LEVELS, boost::program_options::value<int>(),
     "Number of pyramid levels up to which multithreading "
     "should be applied (only for performance/memory tuning)")
    ;

  boost::program_options::options_description hidden;
  hidden.add_options()
    (OPTION_INPUT, boost::program_options::value<std::string>(),
     "Input file");
  ;

  boost::program_options::options_description allWithoutHidden;
  allWithoutHidden
    .add(generic)
    .add(source)
    .add(pyramid)
    .add(target)
    .add(volumeOptions)
    .add(restOptions)
    .add(advancedOptions);

  boost::program_options::options_description all = allWithoutHidden;
  all.add(hidden);

  boost::program_options::positional_options_description positional;
  positional.add(OPTION_INPUT, 1);

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
      options.count(OPTION_SAMPLE_DATASET))
  {
    std::string sample;
    Orthanc::EmbeddedResources::GetFileResource(sample, Orthanc::EmbeddedResources::SAMPLE_DATASET);

    std::cout << std::endl << sample << std::endl;

    return false;
  }

  if (!error &&
      options.count(OPTION_HELP) == 0 &&
      options.count(OPTION_VERSION) == 0 &&
      options.count(OPTION_INPUT) != 1)
  {
    LOG(ERROR) << "No input file was specified";
    error = true;
  }

  if (error || options.count(OPTION_HELP)) 
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

  if (options.count(OPTION_VERSION)) 
  { 
    OrthancWSI::ApplicationToolbox::PrintVersion(argv[0]);
    return false;
  }

  if (options.count(OPTION_VERBOSE))
  {
    Orthanc::Logging::EnableInfoLevel(true);
  }

  if (options.count(OPTION_OPENSLIDE))
  {
    OrthancWSI::OpenSlideLibrary::Initialize(options[OPTION_OPENSLIDE].as<std::string>());
  }

  if (options.count(OPTION_PYRAMID) &&
      options[OPTION_PYRAMID].as<bool>())
  {
    parameters.SetReconstructPyramid(true);
  }

  if (options.count(OPTION_SMOOTH) &&
      options[OPTION_SMOOTH].as<bool>())
  {
    parameters.SetSmoothEnabled(true);
  }

  if (options.count(OPTION_SAFETY) &&
      options[OPTION_SAFETY].as<bool>())
  {
    parameters.SetSafetyCheck(true);
  }

  if (options.count(OPTION_REENCODE) &&
      options[OPTION_REENCODE].as<bool>())
  {
    parameters.SetForceReencode(true);
  }

  if (options.count(OPTION_REPAINT) &&
      options[OPTION_REPAINT].as<bool>())
  {
    parameters.SetRepaintBackground(true);
  }

  if (options.count(OPTION_TILE_WIDTH) ||
      options.count(OPTION_TILE_HEIGHT))
  {
    int w = 0;
    if (options.count(OPTION_TILE_WIDTH))
    {
      w = options[OPTION_TILE_WIDTH].as<int>();
    }

    int h = 0;
    if (options.count(OPTION_TILE_HEIGHT))
    {
      h = options[OPTION_TILE_HEIGHT].as<int>();
    }

    if (w < 0 || h < 0)
    {
      LOG(ERROR) << "Negative target tile size specified: " << w << "x" << h;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    parameters.SetTargetTileSize(w, h);
  }

  parameters.SetInputFile(options[OPTION_INPUT].as<std::string>());

  if (options.count(OPTION_COLOR))
  {
    uint8_t r, g, b;
    OrthancWSI::ApplicationToolbox::ParseColor(r, g, b, options[OPTION_COLOR].as<std::string>());
    parameters.SetBackgroundColor(r, g, b);
  }

  if (options.count(OPTION_COMPRESSION))
  {
    std::string s = options[OPTION_COMPRESSION].as<std::string>();
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

  if (options.count(OPTION_JPEG_QUALITY))
  {
    parameters.SetJpegQuality(options[OPTION_JPEG_QUALITY].as<int>());
  }

  if (options.count(OPTION_LEVELS))
  {
    parameters.SetPyramidLevelsCount(options[OPTION_LEVELS].as<int>());
  }

  if (options.count(OPTION_LOWER_LEVELS))
  {
    parameters.SetPyramidLowerLevelsCount(options[OPTION_LOWER_LEVELS].as<int>());
  }

  if (options.count(OPTION_THREADS))
  {
    parameters.SetThreadsCount(options[OPTION_THREADS].as<int>());
  }

  if (options.count(OPTION_MAX_SIZE))
  {
    parameters.SetDicomMaxFileSize(options[OPTION_MAX_SIZE].as<int>() * 1024 * 1024);
  }

  if (options.count(OPTION_FOLDER))
  {
    parameters.SetTargetFolder(options[OPTION_FOLDER].as<std::string>());
  }

  if (options.count(OPTION_FOLDER_PATTERN))
  {
    parameters.SetTargetFolderPattern(options[OPTION_FOLDER_PATTERN].as<std::string>());
  }

  OrthancWSI::ApplicationToolbox::SetupRestApi(parameters.GetOrthancParameters(), options);

  if (options.count(OPTION_DATASET))
  {
    parameters.SetDatasetPath(options[OPTION_DATASET].as<std::string>());
  }

  if (options.count(OPTION_IMAGED_WIDTH))
  {
    volume.SetWidth(options[OPTION_IMAGED_WIDTH].as<float>());
  }

  if (options.count(OPTION_IMAGED_HEIGHT))
  {
    volume.SetHeight(options[OPTION_IMAGED_HEIGHT].as<float>());
  }

  if (options.count(OPTION_IMAGED_DEPTH))
  {
    volume.SetDepth(options[OPTION_IMAGED_DEPTH].as<float>());
  }

  if (options.count(OPTION_OFFSET_X))
  {
    volume.SetOffsetX(options[OPTION_OFFSET_X].as<float>());
  }

  if (options.count(OPTION_OFFSET_Y))
  {
    volume.SetOffsetY(options[OPTION_OFFSET_Y].as<float>());
  }

  if (options.count(OPTION_OPTICAL_PATH))
  {
    std::string s = options[OPTION_OPTICAL_PATH].as<std::string>();
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

  if (options.count(OPTION_ICC_PROFILE))
  {
    parameters.SetIccProfilePath(options[OPTION_ICC_PROFILE].as<std::string>());
  }

  return true;
}



OrthancWSI::ITiledPyramid* OpenInputPyramid(OrthancWSI::ImageCompression& sourceCompression,
                                            const std::string& path,
                                            const OrthancWSI::DicomizerParameters& parameters)
{
  LOG(WARNING) << "The input image is: " << path;

  OrthancWSI::ImageCompression format = OrthancWSI::DetectFormatFromFile(path);
  LOG(WARNING) << "File format of the input image: " << EnumerationToString(format);

  switch (format)
  {
    case OrthancWSI::ImageCompression_Png:
    {
      sourceCompression = OrthancWSI::ImageCompression_Unknown;
      return new OrthancWSI::TiledPngImage(path, 
                                           parameters.GetTargetTileWidth(512), 
                                           parameters.GetTargetTileHeight(512));
    }

    case OrthancWSI::ImageCompression_Jpeg:
    {
      sourceCompression = OrthancWSI::ImageCompression_Unknown;
      return new OrthancWSI::TiledJpegImage(path, 
                                            parameters.GetTargetTileWidth(512), 
                                            parameters.GetTargetTileHeight(512));
    }

    case OrthancWSI::ImageCompression_Tiff:
    {
      try
      {
        std::auto_ptr<OrthancWSI::HierarchicalTiff> tiff(new OrthancWSI::HierarchicalTiff(path));
        sourceCompression = tiff->GetImageCompression();
        return tiff.release();
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
    sourceCompression = OrthancWSI::ImageCompression_Unknown;
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
  OrthancWSI::ApplicationToolbox::ShowVersionInLog(argv[0]);

  int exitStatus = 0;

  try
  {
    OrthancWSI::DicomizerParameters  parameters;
    OrthancWSI::ImagedVolumeParameters volume;

    if (ParseParameters(exitStatus, parameters, volume, argc, argv))
    {
      OrthancWSI::ImageCompression sourceCompression;
      std::auto_ptr<OrthancWSI::ITiledPyramid> source;

      source.reset(OpenInputPyramid(sourceCompression, parameters.GetInputFile(), parameters));
      if (source.get() == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
      }

      LOG(WARNING) << "Compression of the individual source tiles: " << OrthancWSI::EnumerationToString(sourceCompression);
      
      // Create the shared DICOM tags
      std::auto_ptr<DcmDataset> dataset(ParseDataset(parameters.GetDatasetPath()));
      EnrichDataset(*dataset, *source, sourceCompression, parameters, volume);

      std::auto_ptr<OrthancWSI::IFileTarget> output(parameters.CreateTarget());
      Recompress(*output, *source, *dataset, parameters, volume, sourceCompression);
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
