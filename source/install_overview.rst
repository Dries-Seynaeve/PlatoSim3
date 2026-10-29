Overview
========

There are two ways to install PlatoSim. Pick one:

.. list-table::
   :header-rows: 1
   :widths: 22 39 39

   * -
     - :doc:`With Conda <install_conda>`
     - :doc:`From source <install_source>`
   * - **Choose this if**
     - you want to *use* PlatoSim.
     - you want to change the code, contribute, or Conda is not available for your system.
   * - **What you get**
     - A pre-built ``platosim`` executable from our build server (Jenkins).
     - The full source code, which you build on your own machine.
   * - **Effort**
     - A few commands, a few minutes.
     - More steps; compiling the bundled libraries takes a while.

If you are not sure, start with the :doc:`Quickstart <basic_quickstart>`, which uses Conda.

.. warning::

   PlatoSim runs on Linux and macOS. **Windows is not supported.** On Windows 10 or 11, install PlatoSim inside the
   `Windows Subsystem for Linux (WSL) <https://learn.microsoft.com/en-us/windows/wsl/install>`_ and follow the Linux
   instructions there.

   Pre-built Conda packages exist for Linux (x86-64) and for Apple Silicon and Intel Macs. The **Intel Mac** package is
   deprecated and will be dropped in a future release; on an Intel Mac, consider installing from source.

.. raw:: html

   <hr>

.. _install_overview_requirements:

*Requirements*
--------------

Make sure the following software is installed before you start. The versions are the *minimum* versions; newer
versions work as well.

**For both installation routes**

.. list-table::
   :header-rows: 1
   :widths: 25 20 55

   * - Software
     - Minimum version
     - Notes
   * - Python
     - 3.12
     - We recommend Python 3.12; Python 3.13 also works. Python 3.14 and newer are not supported yet, because some
       packages are not yet available for them.
   * - `uv <https://docs.astral.sh/uv/>`_
     - 0.6.7
     - Installs PlatoSim's Python packages. Check your version with ``uv --version``.

**Only for the Conda route**

.. list-table::
   :header-rows: 1
   :widths: 25 20 55

   * - Software
     - Minimum version
     - Notes
   * - Conda
     - any recent version
     - Provided by `Miniforge <https://conda-forge.org/download/>`_, Miniconda, or Anaconda.

**Only for the source route**

.. list-table::
   :header-rows: 1
   :widths: 25 20 55

   * - Software
     - Minimum version
     - Notes
   * - `Git <https://git-scm.com/>`_
     - any recent version
     - To download the source code.
   * - C and C++ compiler
     - gcc 5.1 or clang 3.4
     - The compiler must support C++14. Check with ``gcc --version`` or ``clang --version``.
   * - `CMake <https://cmake.org/>`_
     - **3.26**
     - Required by the bundled HDF5 library. Check with ``cmake --version``. Older Linux distributions (e.g. Ubuntu
       22.04 and Debian 12) ship an older CMake; install a newer one with ``pip install cmake`` or from
       `cmake.org <https://cmake.org/download/>`_.
   * - Make, tar
     - any recent version
     - Standard on Linux and macOS (on macOS, install the Xcode command line tools: ``xcode-select --install``).
   * - Python 3
     - any 3.x
     - The scripts that install the bundled libraries are written in Python.
   * - Disk space
     - 2 GB
     - For the source code, the compiled libraries, and the Python environment. A mapped PSF file needs another 9 GB,
       and the validation suite another 11 GB.
   * - BLAS and LAPACK
     - optional
     - Make PlatoSim faster. Pre-installed on macOS; on Linux install them with your package manager (e.g.
       ``sudo apt install libopenblas-dev liblapack-dev``).

.. note::

   The source installation was last tested on Linux with gcc 16.2, CMake 4.4, GNU Make 4.4, Python 3.12, and uv 0.13.
