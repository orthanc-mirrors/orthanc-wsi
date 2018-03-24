#!/usr/bin/python

#
# This maintenance script updates the content of the "Orthanc" folder
# to match the latest version of the Orthanc source code.
#

import multiprocessing
import os
import stat
import urllib2

TARGET = os.path.join(os.path.dirname(__file__), '..', 'Resources', 'Orthanc')
PLUGIN_SDK_VERSION = '1.0.0'
REPOSITORY = 'http://bitbucket.org/sjodogne/orthanc/raw'

FILES = [
    'NEWS',
    'Core/Cache/LeastRecentlyUsedIndex.h',
    'Core/ChunkedBuffer.cpp',
    'Core/ChunkedBuffer.h',
    'Core/DicomFormat/DicomArray.cpp',
    'Core/DicomFormat/DicomArray.h',
    'Core/DicomFormat/DicomElement.h',
    'Core/DicomFormat/DicomMap.cpp',
    'Core/DicomFormat/DicomMap.h',
    'Core/DicomFormat/DicomTag.cpp',
    'Core/DicomFormat/DicomTag.h',
    'Core/DicomFormat/DicomValue.cpp',
    'Core/DicomFormat/DicomValue.h',
    'Core/DicomParsing/ITagVisitor.h',
    'Core/DicomParsing/FromDcmtkBridge.cpp',
    'Core/DicomParsing/FromDcmtkBridge.h',
    'Core/DicomParsing/ToDcmtkBridge.cpp',
    'Core/DicomParsing/ToDcmtkBridge.h',
    'Core/Endianness.h',
    'Core/EnumerationDictionary.h',
    'Core/Enumerations.cpp',
    'Core/Enumerations.h',
    'Core/HttpClient.cpp',
    'Core/HttpClient.h',
    'Core/ICommand.h',
    'Core/IDynamicObject.h',
    'Core/Images/IImageWriter.cpp',
    'Core/Images/IImageWriter.h',
    'Core/Images/Image.cpp',
    'Core/Images/Image.h',
    'Core/Images/ImageAccessor.cpp',
    'Core/Images/ImageAccessor.h',
    'Core/Images/ImageBuffer.cpp',
    'Core/Images/ImageBuffer.h',
    'Core/Images/ImageProcessing.cpp',
    'Core/Images/ImageProcessing.h',
    'Core/Images/JpegErrorManager.cpp',
    'Core/Images/JpegErrorManager.h',
    'Core/Images/JpegReader.cpp',
    'Core/Images/JpegReader.h',
    'Core/Images/JpegWriter.cpp',
    'Core/Images/JpegWriter.h',
    'Core/Images/PixelTraits.h',
    'Core/Images/PngReader.cpp',
    'Core/Images/PngReader.h',
    'Core/Images/PngWriter.cpp',
    'Core/Images/PngWriter.h',
    'Core/Logging.cpp',
    'Core/Logging.h',
    'Core/MultiThreading/BagOfTasks.h',
    'Core/MultiThreading/BagOfTasksProcessor.cpp',
    'Core/MultiThreading/BagOfTasksProcessor.h',
    'Core/MultiThreading/Semaphore.cpp',
    'Core/MultiThreading/Semaphore.h',
    'Core/MultiThreading/SharedMessageQueue.cpp',
    'Core/MultiThreading/SharedMessageQueue.h',
    'Core/OrthancException.h',
    'Core/PrecompiledHeaders.cpp',
    'Core/PrecompiledHeaders.h',
    'Core/SharedLibrary.cpp',
    'Core/SharedLibrary.h',
    'Core/SystemToolbox.cpp',
    'Core/SystemToolbox.h',
    'Core/TemporaryFile.cpp',
    'Core/TemporaryFile.h',
    'Core/Toolbox.cpp',
    'Core/Toolbox.h',
    'Core/WebServiceParameters.cpp',
    'Core/WebServiceParameters.h',
    'Plugins/Samples/Common/DicomDatasetReader.cpp',
    'Plugins/Samples/Common/DicomDatasetReader.h',
    'Plugins/Samples/Common/DicomPath.cpp',
    'Plugins/Samples/Common/DicomPath.h',
    'Plugins/Samples/Common/DicomTag.cpp',
    'Plugins/Samples/Common/DicomTag.h',
    'Plugins/Samples/Common/ExportedSymbols.list',
    'Plugins/Samples/Common/FullOrthancDataset.cpp',
    'Plugins/Samples/Common/FullOrthancDataset.h',
    'Plugins/Samples/Common/IDicomDataset.h',
    'Plugins/Samples/Common/IOrthancConnection.cpp',
    'Plugins/Samples/Common/IOrthancConnection.h',
    'Plugins/Samples/Common/OrthancHttpConnection.cpp',
    'Plugins/Samples/Common/OrthancHttpConnection.h',
    'Plugins/Samples/Common/OrthancPluginConnection.cpp',
    'Plugins/Samples/Common/OrthancPluginConnection.h',
    'Plugins/Samples/Common/OrthancPluginCppWrapper.cpp',
    'Plugins/Samples/Common/OrthancPluginCppWrapper.h',
    'Plugins/Samples/Common/OrthancPluginException.h',
    'Plugins/Samples/Common/SimplifiedOrthancDataset.cpp',
    'Plugins/Samples/Common/SimplifiedOrthancDataset.h',
    'Plugins/Samples/Common/VersionScript.map',
    'Resources/CMake/AutoGeneratedCode.cmake',
    'Resources/CMake/BoostConfiguration.cmake',
    'Resources/CMake/Compiler.cmake',
    'Resources/CMake/DcmtkConfiguration.cmake',
    'Resources/CMake/DownloadPackage.cmake',
    'Resources/CMake/JsonCppConfiguration.cmake',
    'Resources/CMake/LibCurlConfiguration.cmake',
    'Resources/CMake/LibIconvConfiguration.cmake',
    'Resources/CMake/LibJpegConfiguration.cmake',
    'Resources/CMake/LibPngConfiguration.cmake',
    'Resources/CMake/OpenSslConfiguration.cmake',
    'Resources/CMake/UuidConfiguration.cmake',
    'Resources/CMake/VisualStudioPrecompiledHeaders.cmake',
    'Resources/CMake/ZlibConfiguration.cmake',
    'Resources/EmbedResources.py',
    'Resources/LinuxStandardBaseToolchain.cmake',
    'Resources/MinGW-W64-Toolchain32.cmake',
    'Resources/MinGW-W64-Toolchain64.cmake',
    'Resources/MinGWToolchain.cmake',
    'Resources/Patches/boost-1.65.1-linux-standard-base.patch',
    'Resources/Patches/curl-7.57.0-cmake.patch',
    'Resources/Patches/dcmtk-3.6.0-dulparse-vulnerability.patch',
    'Resources/Patches/dcmtk-3.6.0-mingw64.patch',
    'Resources/Patches/dcmtk-3.6.0-speed.patch',
    'Resources/Patches/dcmtk-3.6.2-linux-standard-base.patch',
    'Resources/Patches/dcmtk-3.6.2-cmath.patch',
    'Resources/ThirdParty/VisualStudio/stdint.h',
    'Resources/ThirdParty/base64/base64.cpp',
    'Resources/ThirdParty/base64/base64.h',
    'Resources/ThirdParty/patch/NOTES.txt',
    'Resources/ThirdParty/patch/msys-1.0.dll',
    'Resources/ThirdParty/patch/patch.exe',
    'Resources/ThirdParty/patch/patch.exe.manifest',
    'Resources/WindowsResources.py',
    'Resources/WindowsResources.rc',
]

SDK = [
    'orthanc/OrthancCPlugin.h',
]   

EXE = [
    'Resources/EmbedResources.py',
    'Resources/WindowsResources.py',
]


def Download(x):
    branch = x[0]
    source = x[1]
    target = os.path.join(TARGET, x[2])
    print target

    try:
        os.makedirs(os.path.dirname(target))
    except:
        pass

    url = '%s/%s/%s' % (REPOSITORY, branch, source)

    try:
        with open(target, 'w') as f:
            f.write(urllib2.urlopen(url).read())
    except:
        print('Cannot download file %s' % url)
        raise


commands = []

for f in FILES:
    commands.append([ 'default', f, f ])

for f in SDK:
    commands.append([ 
        'Orthanc-%s' % PLUGIN_SDK_VERSION, 
        'Plugins/Include/%s' % f,
        'Sdk-%s/%s' % (PLUGIN_SDK_VERSION, f) 
    ])


pool = multiprocessing.Pool(10)  # simultaneous downloads
pool.map(Download, commands)


for exe in EXE:
    path = os.path.join(TARGET, exe)
    st = os.stat(path)
    os.chmod(path, st.st_mode | stat.S_IEXEC)

