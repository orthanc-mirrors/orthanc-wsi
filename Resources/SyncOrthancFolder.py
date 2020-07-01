#!/usr/bin/python

#
# This maintenance script updates the content of the "Orthanc" folder
# to match the latest version of the Orthanc source code.
#

import multiprocessing
import os
import stat
import urllib2

TARGET = os.path.join(os.path.dirname(__file__), 'Orthanc')
PLUGIN_SDK_VERSION = '1.0.0'
REPOSITORY = 'https://hg.orthanc-server.com/%s/raw-file'

FILES = [
    ('orthanc', 'OrthancFramework/Resources/CMake/DownloadOrthancFramework.cmake', '.'),
    ('orthanc', 'OrthancFramework/Resources/Toolchains/LinuxStandardBaseToolchain.cmake', '.'),
    ('orthanc', 'OrthancFramework/Resources/Toolchains/MinGW-W64-Toolchain32.cmake', '.'),
    ('orthanc', 'OrthancFramework/Resources/Toolchains/MinGW-W64-Toolchain64.cmake', '.'),
    ('orthanc', 'OrthancFramework/Resources/Toolchains/MinGWToolchain.cmake', '.'),

    ('orthanc', 'OrthancServer/Plugins/Samples/Common/OrthancPluginCppWrapper.cpp', 'Plugins'),
    ('orthanc', 'OrthancServer/Plugins/Samples/Common/OrthancPluginCppWrapper.h', 'Plugins'),
    ('orthanc', 'OrthancServer/Plugins/Samples/Common/OrthancPluginException.h', 'Plugins'),

    ('orthanc-stone', 'Framework/Toolbox/OrthancDatasets/DicomDatasetReader.cpp', 'Stone'),
    ('orthanc-stone', 'Framework/Toolbox/OrthancDatasets/DicomDatasetReader.h', 'Stone'),
    ('orthanc-stone', 'Framework/Toolbox/OrthancDatasets/DicomPath.cpp', 'Stone'),
    ('orthanc-stone', 'Framework/Toolbox/OrthancDatasets/DicomPath.h', 'Stone'),
    ('orthanc-stone', 'Framework/Toolbox/OrthancDatasets/FullOrthancDataset.cpp', 'Stone'),
    ('orthanc-stone', 'Framework/Toolbox/OrthancDatasets/FullOrthancDataset.h', 'Stone'),
    ('orthanc-stone', 'Framework/Toolbox/OrthancDatasets/IDicomDataset.h', 'Stone'),
    ('orthanc-stone', 'Framework/Toolbox/OrthancDatasets/IOrthancConnection.cpp', 'Stone'),
    ('orthanc-stone', 'Framework/Toolbox/OrthancDatasets/IOrthancConnection.h', 'Stone'),
    ('orthanc-stone', 'Framework/Toolbox/OrthancDatasets/OrthancHttpConnection.cpp', 'Stone'),
    ('orthanc-stone', 'Framework/Toolbox/OrthancDatasets/OrthancHttpConnection.h', 'Stone'),
]

SDK = [
    'orthanc/OrthancCPlugin.h',
]


def Download(x):
    repository = x[0]
    branch = x[1]
    source = x[2]
    target = os.path.join(TARGET, x[3])
    print target

    try:
        os.makedirs(os.path.dirname(target))
    except:
        pass

    url = '%s/%s/%s' % (REPOSITORY % repository, branch, source)

    with open(target, 'w') as f:
        try:
            f.write(urllib2.urlopen(url).read())
        except:
            print('ERROR: %s' % url)
            raise


commands = []

for f in FILES:
    commands.append([ f[0],
                      'default',
                      f[1],
                      os.path.join(f[2], os.path.basename(f[1])) ])

for f in SDK:
    commands.append([
        'orthanc',
        'Orthanc-%s' % PLUGIN_SDK_VERSION, 
        'Plugins/Include/%s' % f,
        'Sdk-%s/%s' % (PLUGIN_SDK_VERSION, f) 
    ])


pool = multiprocessing.Pool(10)  # simultaneous downloads
pool.map(Download, commands)
