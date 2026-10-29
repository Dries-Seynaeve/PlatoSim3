Troubleshooting
===============

.. raw:: html

   <hr>

.. _basic_troubleshooting_common:

*Common problems*
-----------------

**The build stops with an error about the CMake version.**
   The bundled HDF5 library needs CMake 3.26 or more recent. Check your version with ``cmake --version`` and see the
   :ref:`requirements <install_overview_requirements>`.

**The build takes a very long time.**
   Compiling the bundled libraries, in particular HDF5, takes a while. Compile more files in parallel, e.g. with 8
   cores: ``./install.sh -j 8`` (see :ref:`Step 3 <install_source_dependencies>`).

**command not found: platosim**
   For a source installation, add the ``build/`` directory to your ``PATH`` (see :ref:`Step 5
   <install_source_variables>`). For a Conda installation, activate your environment with ``conda activate platosim``.

**KeyError: 'PLATO_PROJECT_HOME', or PlatoSim cannot find its input files.**
   The environment variable ``PLATO_PROJECT_HOME`` is not set, or points to the wrong directory. Check it with
   ``echo $PLATO_PROJECT_HOME`` (see :doc:`Prerequisites <run_prerequisites>`).

**ModuleNotFoundError: No module named 'platosim' (or another package).**
   Activate the environment in which you installed the Python packages (``conda activate platosim``, or
   ``source .venv/bin/activate`` in the ``PlatoSim3`` directory) and check that the installation of the Python packages
   finished without errors.

**PlatoSim stops with "Output file ... already exists".**
   PlatoSim never overwrites an output file. Remove the file, choose another name, or use
   ``sim.run(removeOutputFile=True)`` in Python.

**PlatoSim stops with a configuration error.**
   Your configuration file probably comes from an older version of PlatoSim. Update it with the :doc:`migration tool
   <install_migtool>`, or start again from a copy of the default ``inputfile.yaml``.

More causes of failed simulations are listed under :ref:`Error codes <run_simulations_error>`.

.. raw:: html

   <hr>

.. _basic_troubleshooting_issue:

*Report a problem*
------------------

If your problem is not listed above, please open an issue in our `GitHub issue tracker
<https://github.com/IvS-KULeuven/PlatoSim3/issues>`_ rather than e-mailing one of the developers. This helps us keep
track of open problems and lets other users find the answer too. First check whether your problem has already been
reported.

.. figure:: ../figures/github_issueTracking.png
   :align: center
   :width: 700
   :alt: The Issues tab of the PlatoSim repository on GitHub, with the green New issue button.

Click the green ``New issue`` button to open a new issue:

.. figure:: ../figures/github_newIssue.png
   :align: center
   :width: 700
   :alt: The form to open a new issue on GitHub.

To help us solve the problem quickly, please include:

* A short title and a clear description of the problem, and what you already tried to solve it.
* Whether it is a bug, a question, or a feature request.
* Your PlatoSim version (the output of ``platosim -version``) and whether you installed it with Conda or from source.
* Your configuration file and the log file. Run the simulation with log level ``3`` (the default), which includes the
  debug messages.
* For problems with the installation or with running PlatoSim:

  * your operating system and its version;
  * the versions of your compiler (``gcc --version`` or ``clang --version``) and CMake (``cmake --version``);
  * the versions of Python (``python --version``) and uv (``uv --version``);
  * the full error output (see :ref:`The build fails? <install_source_dependencies>`).
