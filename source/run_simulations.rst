Run simulations
===============

To run simulations with PlatoSim, you will have to feed a :ref:`Configuration file <run_input_parameters>` as input to the simulator, possibly together with a few additional :ref:`Supplementary files <run_input_files>`. Launching simulations can be done either from the :ref:`command line <run_simulations_command>` or in :ref:`Python <run_simulations_python>` as explained on this page.

.. raw:: html

   <hr>




.. _run_simulations_command:

*Command line*
--------------
   
Make sure the :doc:`environment variables <run_prerequisites>` are set. To run a simulation from the command line, type:

.. code-block:: shell
		
   platosim <inputFile> <outputFile> [<logFile> <logLevel>]

The log file defaults to ``log.txt`` in the current directory. The log level is an integer value that allows to configure what kind of log messages is written to the log file:

0. messages: ``error``;
1. messages: ``error``, ``warning``;
2. messages: ``error``, ``warning``, ``info``;
3. messages: ``error``, ``warning``, ``info``, ``debug`` (default).

Errors and warnings are also printed on the terminal. To check which version of PlatoSim you are running, use:

.. code-block:: shell

   platosim -version

For example, to run a simulation with your own copy of the configuration file (see :ref:`Where to keep your files <run_prerequisites_storage>`):

.. code-block:: shell

   cd $PLATO_WORKDIR
   platosim my_inputfile.yaml out.hdf5

This generates two files: the HDF5 output file ``out.hdf5`` with the simulated data, and the text file ``log.txt`` with the log messages. PlatoSim refuses to overwrite an existing output file, so remove ``out.hdf5`` before you run the same command again.
   
.. raw:: html

   <hr>



   
.. _run_simulations_python:

*Python*
--------

`A full suite of Jupyter notebook tutorials are made available <https://github.com/IvS-KULeuven/PlatoSim3/tree/develop/docs/tutorials>`_. Below we provide a minimal example of how to get started running PlatoSim using our Python interface. If you're planning on creating your own scripts in Python, then besides reading our Jupyter notebooks, we recommend spending a bit of time investigating the different built in `Python classes (and their utilities) <https://github.com/IvS-KULeuven/PlatoSim3/tree/develop/python/platosim>`_.

We note that the :doc:`PLATOnium toolkit <platonium_overview>` can be used to setup a large scale simulation of imagettes/photometric time series within minutes. This toolkit can be used to generate stellar catalogue from the PIC, generate variable signals, generate instrumental effects, and takes care of the configuration of payload inline with the future observations of PLATO. This toolkit is meant for plug-and-play and in particular to be used on a computing cluster.

.. raw:: html

   <hr>
   
**Create a simulation object**

To run PlatoSim from within Python, you have to import the simulation module:

.. code-block:: python
		
   from platosim.simulation import Simulation

and create a new Simulation object (the name ``sim`` is arbitrary):

.. code-block:: python
		
   sim = Simulation(<runName>,
                    configurationFile = <full path to the YAML configuration file>,
                    outputDir = <full path to the output directory>)

Note that you can use the working directory (i.e. the directory specified in the ``PLATO_WORKDIR``) environment variable to specify the path to the input configuration file or the output directory. To read the content of this environment variable in Python, use:

.. code-block:: python
		
   import os
   workDir = os.getenv("PLATO_WORKDIR")


**Input configuration file**

The ``configurationFile`` parameter is optional and contains the full path to the configuration file that will be used as a reference. If omitted, the default ``$PLATO_PROJECT_HOME/inputfiles/inputfile.yaml`` is used. A copy of this file will be stored in the output directory (with the name ``<runName>.yaml``) and only the copy (and not the original file) will be modified. We will show later how to do this.

It is possible to also set the path to the reference input file as follows:

.. code-block:: python
		
   sim.readConfigurationFile(<full path to the configuration file>)

In that case, all changes to the configuration parameters you may have applied, will be overwritten.


**Output directory**

Also the ``outputDir`` parameter is optional. This contains the full path to the output directory and defaults to the current working directory. The output directory can also be set as follows:

.. code-block:: python
		
   sim.outputDir = <full path to the output directory>

In this directory, the following information will be stored when running the simulation:

  * ``<runName>.yaml``: copy of the input file with the chosen configuration parameters (copied from the reference configuration file and possibly modified, as shown below);
  * ``<runName>.hdf5``: resulting exposures (images, PSF, etc.);
  * ``<runName>.log``: log file (to report any problems).

   
**Modify configuration parameters**

Change a configuration parameter by giving its full path in the YAML file, with the names of the blocks separated by slashes:

.. code-block:: python

   sim["ObservingParameters/NumExposures"] = 100
   sim["Platform/Orientation/Angles/RAPointing"] = 90.0

Read the current value in the same way, e.g. ``print(sim["ObservingParameters/NumExposures"])``, or print all parameters with ``print(sim)``. The changes are stored in the copy ``<runName>.yaml`` in the output directory; the original configuration file is never modified. If you load another configuration file with ``readConfigurationFile()``, all changes you made are lost.

.. warning::

   Check the spelling of the parameter path carefully. If a path does not exist, PlatoSim prints ``ERROR: no node with the name ... found in input yaml file`` but does **not** stop: the simulation then runs with the default value. Also make sure the path ends at a parameter, not at a block: ``sim["Camera/FocalLength"] = 0.25`` replaces the whole ``FocalLength`` block, whereas ``sim["Camera/FocalLength/ConstantValue"] = 0.25`` changes the value.

**Normal and fast cameras**

The default configuration file describes a normal camera (N-CAM) with its *as required* performance at Beginning Of Life (BOL). To switch between camera types, and to account for ageing, use:

.. code-block:: python

   sim.useNormalCamera(performance=False, timeFromBOL=0)
   sim.useFastCamera(passband="blue", performance=False, timeFromBOL=0)   # or passband="red"

These methods update the parameters that differ between the camera types (e.g. ``Fluxm0``, passband, cycle time, camera group, and quantum efficiency). The optional arguments are:

* ``performance``: ``"required"``, ``"expected"``, or ``"designed"`` to use the corresponding values from the Mission Parameter Database for the detector gain and noise, transmission, and quantum efficiency. With the default ``False``, these parameters are left unchanged.
* ``timeFromBOL``: time since the Beginning Of Life of the mission, **in seconds**, used to age the detector noise (only together with ``performance``). For example, ``timeFromBOL=2*365.25*86400`` for two years into the mission.

**Run the simulation**

The simulation can be launched with the following command:

.. code-block:: python

   simfile = sim.run()

The method returns a ``SimFile`` object to read the HDF5 output file (see :doc:`Output files <run_output>`). By setting the optional input parameter ``removeOutputFile`` to ``True``, any previous simulated data with the same name within a given folder will be overwritten. The ``logLevel`` (default ``3``) can be set as on the command line:

.. code-block:: python
		
   simfile = sim.run(removeOutputFile=True, logLevel=2)

If PlatoSim fails, ``run()`` raises an exception that contains the exit code.

.. raw:: html

   <hr>



   


.. _run_simulations_error:

*Error codes*
-------------
		
If PlatoSim fails, it stops with a non-zero exit code and writes the reason to the log file (and to the terminal). Please have a look at the log messages before raising a new GitHub issue. The most frequent causes are:

- **The HDF5 output file already exists**: PlatoSim never overwrites an output file. Remove it and try again (or use ``sim.run(removeOutputFile=True)`` in Python).
- **Configuration error**: a parameter is missing, misspelled, or has the wrong type in the YAML file. This typically happens with a configuration file from an older PlatoSim version; use the :doc:`migration tool <install_migtool>` to update it.
- **Missing input file**: a :ref:`supplementary file <run_input_files>` (e.g. a PSF file) cannot be found. Check the path, which is either absolute or relative to the :ref:`project location <run_input_parameters_general>`.
