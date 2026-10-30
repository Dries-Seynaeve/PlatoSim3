#
# install.sh runs automatically this script. It can be done manually running (from the same folder than install.sh):
# $ python ./dependencies/installscripts/install_zlib.py
#

import os,sys,shutil,subprocess


# Specify the dependency package name

packageName = "zlib-1.3.2"

# Specify build and install folders

currentWorkingDir = os.getcwd()
buildDir         = currentWorkingDir + "/dependencies/Downloads/"
parentInstallDir = currentWorkingDir + "/dependencies/Installs/"
installDir       = parentInstallDir + packageName


# Check if /dependencies/Installs directory exists

if not os.path.isdir(parentInstallDir):
    os.mkdir(parentInstallDir)


# If there is an older version of the install dir, make sure to remove it first.

shutil.rmtree(installDir, ignore_errors=True)

# Print a banner

print("\n\n\n")
print("===============")
print("Installing zlib")
print("===============")
print("\n")

# Build, test, and install package.
# configure builds both the static (libz.a) and the shared library (libz.so / libz.dylib).
# 'make test' builds the libraries and runs zlib's own test programs against them.
# If the tests fail, the script will exit with an error code.

installProcedure = "cd {build} &&                                   \
                    tar -xzvf {package}.tar.gz &&                   \
                    cd {package} &&                                 \
                    ./configure --prefix={install} &&               \
                    make -j {num_threads} &&                        \
                    make test &&                                    \
                    make install".format(build=buildDir,
                                         package=packageName,
                                         install=installDir,
                                         num_threads=os.environ.get("INSTALL_NUM_THREADS", "4"))

process = subprocess.run(installProcedure, shell=True)
if process.returncode != 0:
    sys.exit(1)



# After installation in the install folder, remove the decompressed package folder in
# the Downloads dir so that only the .tar.gz file remains in the Downloads dir.

shutil.rmtree(buildDir+packageName, ignore_errors=True)
