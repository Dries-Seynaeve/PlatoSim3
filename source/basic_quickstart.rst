Quickstart
==========

This page takes you from nothing to your first simulation in a few minutes, using the :doc:`Conda installation
<install_conda>`. Each step is explained in more detail on that page.

**What you need:** a Linux or macOS computer (on Windows, use `WSL <https://learn.microsoft.com/en-us/windows/wsl/install>`_)
with `Conda <https://conda-forge.org/download/>`_ (4.8 or more recent) and `uv
<https://docs.astral.sh/uv/getting-started/installation/>`_ (0.6.7 or more recent) installed.

**1. Install PlatoSim** in a new Conda environment called ``platosim``:

.. code-block:: shell

   conda create -n platosim python=3.12
   conda activate platosim
   conda config --env --add channels conda-forge
   conda install -c https://plato:miSotalP@jenkins.miricle.org/platosim/ platosim

**2. Install the Python packages.** This also creates a working directory, ``$HOME/platosim_workdir``, for your own
files:

.. code-block:: shell

   mkdir -p $HOME/platosim_workdir
   cd $HOME/platosim_workdir
   curl -LO https://raw.githubusercontent.com/IvS-KULeuven/PlatoSim3/master/pyproject.toml
   uv pip install -r pyproject.toml

**3. Set the environment variables.** They are stored in the Conda environment, so you only do this once:

.. code-block:: shell

   conda env config vars set PLATO_PROJECT_HOME=$CONDA_PREFIX PLATO_WORKDIR=$HOME/platosim_workdir PYTHONPATH=$CONDA_PREFIX/python
   conda deactivate
   conda activate platosim

**4. Run a test simulation** from the command line. This simulates 10 exposures of a small part of a CCD with the
default configuration:

.. code-block:: shell

   cd $PLATO_WORKDIR
   platosim $PLATO_PROJECT_HOME/inputfiles/inputfile.yaml test.hdf5

This creates ``test.hdf5`` (the simulated images) and ``log.txt`` (the log messages). Or run the same simulation from
Python:

.. code-block:: python

   import os
   from platosim.simulation import Simulation

   sim = Simulation("test_python", outputDir=os.getenv("PLATO_WORKDIR"))
   simfile = sim.run()
   image = simfile.getImage(0)    # the first simulated image, as a 2D array

**Next steps**

* Learn how to :doc:`run and configure simulations <run_simulations>`, and what is in the :doc:`output file
  <run_output>`.
* Work through our `Python tutorials <https://github.com/IvS-KULeuven/PlatoSim3/tree/master/docs/tutorials>`_.
* For large simulation projects with many cameras, have a look at :doc:`PLATOnium <platonium_overview>`. Install its
  extra packages with ``uv pip install -r pyproject.toml --group picsim --group varsim --group platonium`` and follow
  :doc:`PLATOnium's prerequisites <platonium_prerequisites>`.
* By default, PlatoSim uses an analytic PSF. To use a realistic (mapped) PSF instead, download one of our :ref:`PSF files
  <run_input_data>` (note: each file is about 9 GB).
