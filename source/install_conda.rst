For Users (via Conda)
=====================

This is the easiest way to install PlatoSim if you only want to *use* it. You install a pre-built version of PlatoSim
into a Conda environment, and then add PlatoSim's Python packages to the same environment with uv. Check the
:ref:`requirements <install_overview_requirements>` first.

.. note::

   Pre-built packages exist for Linux (x86-64) and for Apple Silicon and Intel Macs. The **Intel Mac** (``osx-64``)
   package is deprecated and will be dropped in a future release; installing it shows a warning. Intel Mac users can
   move to an Apple Silicon Mac or Linux, or :doc:`install PlatoSim from source <install_source>`.

.. raw:: html

   <hr>

.. _install_conda_prerequisites:

*Step 1: Install Conda and uv*
------------------------------

**Conda.** If you do not have Conda yet, install `Miniforge <https://conda-forge.org/download/>`_ (or Miniconda, or
Anaconda). You need Conda 4.8 or more recent; check with ``conda --version``.

**uv.** Install uv (0.6.7 or more recent) by following `uv's documentation
<https://docs.astral.sh/uv/getting-started/installation/>`_, for example:

.. code-block:: shell

   curl -LsSf https://astral.sh/uv/install.sh | sh

Open a new terminal afterwards, so that the ``conda`` and ``uv`` commands are found.

.. raw:: html

   <hr>

.. _install_conda_installing:

*Step 2: Install PlatoSim*
--------------------------

Create a new Conda environment called ``platosim`` with Python 3.12, and activate it:

.. code-block:: shell

   conda create -n platosim python=3.12
   conda activate platosim

Add the conda-forge channel to this environment (only needed once per environment):

.. code-block:: shell

   conda config --env --add channels conda-forge

Install PlatoSim. We recommend the ``master`` branch, which holds the latest stable release. List ``conda-forge``
first, so that the libraries PlatoSim needs (HDF5, Armadillo, ...) come from conda-forge:

.. code-block:: shell

   conda install -c conda-forge -c https://plato:miSotalP@jenkins.miricle.org/platosim/ platosim

.. admonition:: Other versions

   * For the latest development version (``develop`` branch), use the channel
     ``https://plato:miSotalP@jenkins.miricle.org/platosim.devel/`` instead.
   * For a specific version, replace ``platosim`` at the end of the command by ``platosim=<version>``, e.g.
     ``platosim=3.8.0``. Older versions were built against libraries from the PlatoSim channel itself; if installing
     one fails, swap the order of the two ``-c`` options.
   * To keep several versions side by side, install each one in its own Conda environment (e.g.
     ``platosim_master`` and ``platosim_develop``).

.. raw:: html

   <hr>

.. _install_conda_python:

*Step 3: Install the Python packages*
-------------------------------------

Conda installs the simulator itself, but not the Python packages that PlatoSim's Python interface needs. These are
listed in the file ``pyproject.toml``. Make sure your ``platosim`` environment is still active, then download the
``pyproject.toml`` file of the branch you installed and install the packages with uv:

.. code-block:: shell

   mkdir -p $HOME/platosim_workdir
   cd $HOME/platosim_workdir
   curl -LO https://raw.githubusercontent.com/IvS-KULeuven/PlatoSim3/master/pyproject.toml
   uv pip install -r pyproject.toml

If you installed the ``develop`` branch, replace ``master`` by ``develop`` in the download link. Because the Conda
environment is active, uv installs the packages into that environment. Check the result with ``uv pip list``.

This installs everything you need to run the `Jupyter notebook tutorials
<https://github.com/IvS-KULeuven/PlatoSim3/tree/master/docs/tutorials>`_ and most of PlatoSim's Python modules. Extra
packages are organised in *dependency groups* (see the :ref:`list of groups <install_source_groups>`). Install them
with ``--group``, for example for the HDF5 viewer argos:

.. code-block:: shell

   uv pip install -r pyproject.toml --group tools

.. raw:: html

   <hr>

.. _install_conda_variables:

*Step 4: Set the environment variables*
---------------------------------------

PlatoSim needs a few :ref:`environment variables <run_prerequisites_export>`. Store them in your Conda environment, so
that they are set automatically every time you activate it. The working directory ``$HOME/platosim_workdir`` is an
example; you can choose any directory outside the Conda environment.

.. code-block:: shell

   conda env config vars set PLATO_PROJECT_HOME=$CONDA_PREFIX PLATO_WORKDIR=$HOME/platosim_workdir PYTHONPATH=$CONDA_PREFIX/python
   conda deactivate
   conda activate platosim

.. raw:: html

   <hr>

.. _install_conda_verify:

*Step 5: Check the installation*
--------------------------------

With the ``platosim`` environment active, both of the following commands should run without errors:

.. code-block:: shell

   platosim -version
   python -c "import platosim"

You are ready to :doc:`run your first simulation <run_simulations>`.

.. raw:: html

   <hr>

.. _install_conda_updating:

*Updating*
----------

To update to the latest version of the ``master`` branch, activate your environment and run:

.. code-block:: shell

   conda update --force-reinstall -c conda-forge -c https://plato:miSotalP@jenkins.miricle.org/platosim/ platosim

(use the ``platosim.devel`` channel for the ``develop`` branch). A new version may need different Python packages, so
download the ``pyproject.toml`` file again and repeat ``uv pip install -r pyproject.toml`` (see :ref:`Step 3
<install_conda_python>`).

.. warning::

   Updating replaces everything inside the Conda environment. Keep your own configuration files and simulations in
   your working directory, not in ``$CONDA_PREFIX``.

.. raw:: html

   <hr>

.. _install_conda_jenkins:

*A word about Jenkins*
----------------------

We use `Jenkins <https://www.jenkins.io/>`_ to build PlatoSim automatically. Each time code is merged into the
``master`` or ``develop`` branch on GitHub, Jenkins builds the new version and, if the build succeeds, makes it
available through the Conda channels above. You can follow the status of the builds on the `Jenkins page
<https://jenkins.miricle.org/view/Platosim/>`_.

.. image:: ../figures/github_jenkins.png
   :align: center
   :width: 1000
   :alt: Developers push code to GitHub, Jenkins builds it automatically, and users install the latest or a specific version and report issues.
