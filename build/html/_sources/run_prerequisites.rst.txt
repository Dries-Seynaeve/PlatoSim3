Prerequisites
=============

.. raw:: html

   <hr>

.. _run_prerequisites_export:

*Environment variables*
-----------------------

PlatoSim uses environment variables, so that no paths have to be hard-coded in configuration files, scripts, or
tutorials. If you followed the :doc:`Conda installation <install_conda>` or the :doc:`source installation
<install_source>`, you have already set them; this section explains what they do.

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Variable
     - Meaning
   * - ``PLATO_PROJECT_HOME``
     - The directory where PlatoSim is installed: ``$CONDA_PREFIX`` for a Conda installation, or the ``PlatoSim3``
       directory for a source installation. **Required.** PlatoSim reads its default input files from
       ``$PLATO_PROJECT_HOME/inputfiles``, and the Python interface runs the simulator from the ``bin/`` (Conda) or
       ``build/`` (source) sub-directory.
   * - ``PLATO_WORKDIR``
     - Your working directory, where you keep your own configuration files and simulations. Choose a directory
       *outside* the installation directory. Used by PLATOnium and several Python tools, and convenient in your own
       scripts.
   * - ``PYTHONPATH``
     - Only for a Conda installation: must include ``$PLATO_PROJECT_HOME/python``, so that Python finds the ``platosim``
       package. A source installation with uv does not need it.
   * - ``PATH``
     - Only for a source installation: must include ``$PLATO_PROJECT_HOME/build``, so that your shell finds the
       ``platosim`` command. A Conda installation adds the command to your ``PATH`` automatically.

**Conda installation.** Store the variables in your Conda environment; they are then set every time you activate it:

.. code-block:: shell

   conda activate platosim
   conda env config vars set PLATO_PROJECT_HOME=$CONDA_PREFIX PLATO_WORKDIR=$HOME/platosim_workdir PYTHONPATH=$CONDA_PREFIX/python
   conda deactivate
   conda activate platosim

**Source installation.** Add the following lines to your shell start-up file (``~/.bashrc`` for bash, the default on
Linux; ``~/.zshrc`` for zsh, the default on macOS), with the full path to your ``PlatoSim3`` directory, and open a new
terminal:

.. code-block:: shell

   export PLATO_PROJECT_HOME=$HOME/PlatoSim3
   export PLATO_WORKDIR=$HOME/platosim_workdir
   export PATH=$PLATO_PROJECT_HOME/build:$PATH

.. warning::

   Do not put ``export PLATO_PROJECT_HOME=$CONDA_PREFIX`` in your shell start-up file. ``$CONDA_PREFIX`` is read when
   the terminal starts, before you activate your PlatoSim environment, so it points to the wrong environment. Use
   ``conda env config vars set`` as shown above instead.

**Check the result.** The first two commands should print the paths you chose, and the last two should run without
errors (for a source installation, activate the environment first with ``source $PLATO_PROJECT_HOME/.venv/bin/activate``):

.. code-block:: shell

   echo $PLATO_PROJECT_HOME
   echo $PLATO_WORKDIR
   platosim -version
   python -c "import platosim"

.. raw:: html

   <hr>

.. _run_prerequisites_storage:

*Where to keep your files*
--------------------------

Keep all your own files, such as configuration files and simulations, in your working directory (``$PLATO_WORKDIR``),
never inside the installation directory:

* **Conda installation:** installing or updating PlatoSim replaces the whole installation directory, so your files
  there would be lost.
* **Source installation:** files you change or add inside ``PlatoSim3`` get in the way when you update the code with
  ``git pull``. Only change files there if you intend to :ref:`contribute them <install_source_contribute>`.

To make your own configuration file, copy the default one and edit the copy:

.. code-block:: shell

   cp $PLATO_PROJECT_HOME/inputfiles/inputfile.yaml $PLATO_WORKDIR/my_inputfile.yaml

The default file always matches your installed version. You can also download it from GitHub, but make sure to take it
from the same branch as your installation.
