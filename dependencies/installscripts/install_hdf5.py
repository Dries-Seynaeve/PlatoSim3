#
# install.sh runs automatically this script. It can be done manually running (from the same folder than install.sh):
# $ python ./dependencies/installscripts/install_hdf5.py
#

import sys
import os
import shutil
import subprocess


# Specify the dependency package name

packageName = "hdf5-2.2.0"

# Specify build and install folders

currentWorkingDir = os.getcwd()
buildDir         = currentWorkingDir + "/dependencies/Downloads/"
parentInstallDir = currentWorkingDir + "/dependencies/Installs/"
installDir       = parentInstallDir + packageName


# Check if /dependencies/Installs directory exists

if not os.path.isdir(parentInstallDir):
    os.mkdir(parentInstallDir)

# Remove a possible older version, and create a fresh one

shutil.rmtree(installDir, ignore_errors=True)
os.mkdir(installDir)

# Print a banner

print("\n\n\n")
print("===============")
print("Installing HDF5")
print("===============")
print("\n")

# Build and install package

installProcedure = "cd {build} &&                                   \
                    tar -xzvf {package}.tgz &&                      \
                    cd hdf5 &&                                      \
                    rm -rf build &&                                 \
                    mkdir build &&                                  \
                    cd build &&                                     \
                    cmake .. -DCMAKE_INSTALL_PREFIX={install} -DHDF5_BUILD_CPP_LIB=ON &&    \
                    cmake --build . &&                              \
                    cmake --install .".format(build=buildDir,
                                              package=packageName,
                                              install=installDir)

process = subprocess.run(installProcedure, shell=True, text=True)
if process.returncode != 0:
    sys.exit(1)

# After installation in the install folder, remove the decompressed package folder in the build dir
# so that only the .tgz file remains in the Downloads folder.

shutil.rmtree(buildDir+"hdf5", ignore_errors=True)


