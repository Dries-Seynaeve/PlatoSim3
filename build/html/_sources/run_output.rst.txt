Output files
============

The output files of PlatoSim is in a so-called `HDF5 format <https://www.hdfgroup.org/solutions/hdf5/>`_. Which
groups are present depends on the configuration: most of them can be switched on or off in the :ref:`ControlHDF5Content
<run_input_parameters_hdf5>` block of the configuration file, and some only exist when the corresponding effect is
included. The structure of an output file is the following:

.. code-block:: text

   /
   ├── Version                     Attribute GitVersion: the PlatoSim version that produced the file
   ├── InputParameters/            Copy of all configuration parameters (one sub-group per YAML block)
   │
   │   Maps for every exposure (3D datasets: exposure × rows × columns)
   ├── Images/subfield             Subfield pixel maps                       [ADU]
   ├── SmearingMaps/smearing       Smearing (overscan) maps                  [ADU]
   ├── BiasMapsLeft/bias           Bias (prescan) maps of the left CCD half  [ADU]
   ├── BiasMapsRight/bias          Bias (prescan) maps of the right CCD half [ADU]
   ├── ThroughputMaps/throughput   Throughput maps (QE, vignetting, contamination, ...)
   ├── SubPixelImages/images       Sub-pixel maps (only if WriteSubPixelImages = yes)
   │
   │   Maps that do not change over time
   ├── BackgroundMap/              Sky background (a single value if the background is constant)
   ├── Flatfield/PRNU              Flatfield map (and IRNU for the mapped PSF)
   ├── BadPixelMap/                Bad pixel map of the full CCD and of the subfield
   ├── CTI/                        Trap density maps at BOL and EOL for each trap species
   ├── PSF/                        PSF images (only if requested)
   ├── Straylight/Moon             Stray light from the Moon (only if IncludeStrayLight = yes)
   │
   │   Time series and source information
   ├── TransmissionEfficiency/     Transmission efficiency for each exposure
   ├── ACS/                        Time series of the platform pointing and jitter (yaw, pitch, roll)
   ├── Telescope/                  Time series of the camera pointing and drift (yaw, pitch, roll)
   ├── StarCatalog/                Sky, focal-plane, and pixel coordinates of all stars on the subfield
   ├── StarPositions/              Positions and fluxes of the stars for each exposure
   ├── PointLikeGhostPositions/    Same for the point-like ghosts
   ├── ExtendedGhostPositions/     Same for the extended ghosts
   ├── Cosmics/                    Cosmic hits: SubField, SmearingMap, BiasMapLeft, BiasMapRight
   └── Photometry/                 Masks and light curves (only if IncludePhotometry = yes)

A few notes on these groups:

* ``InputParameters`` contains a copy of the configuration parameters from the YAML file, including the random seeds
  that were actually used (also when a seed was set to ``-1``). The ``Version`` group stores the Git version of the
  software, should you need it.
* The pixel, smearing, bias, and throughput maps of all exposures are stored in a single 3D dataset per map type. The
  first index is the exposure number, counted from the first simulated exposure (``ObservingParameters/BeginExposureNr``).
* The layout of ``StarPositions``, ``PointLikeGhostPositions``, and ``ExtendedGhostPositions`` depends on the
  :ref:`GroupByExposure <run_input_parameters_hdf5>` setting: with ``GroupByExposure = yes`` there is one sub-group per
  exposure (``Exposure0000000``, ...), otherwise one sub-group per star (``starID0000001``, ...).
* The ``StarCatalog`` group contains all stars that were on the subfield during any exposure. The ``starIDs`` map to the
  identifiers in the input star catalogue.
* Since not every exposure has the same number of cosmic hits, ``Cosmics`` stores them as flat (ragged) arrays, with one
  sub-group for each map (``SubField``, ``SmearingMap``, ``BiasMapLeft``, and ``BiasMapRight``). The values for the
  cosmic hits of exposure :math:`i` (``entryRows``, ``entryColumns``, ``trailLengths``, ``entryAngles``, and
  ``intensities``) are ``field[offsets[i]:offsets[i+1]]``. The values for the pixels hit by a cosmic of exposure
  :math:`i` (``rows``, ``columns``, and ``flux``) are ``field[pixelOffsets[i]:pixelOffsets[i+1]]``. This description
  is also stored as an attribute of each ``Cosmics`` sub-group.

.. note::

   This layout was introduced in PlatoSim 3.8.0. Older output files store one dataset per exposure (e.g.
   ``Images/image000000``) and one cosmics sub-group per exposure. Use the ``SimFile`` class described below, which
   handles the new layout, rather than hard-coding the paths in your scripts.

We recommend you to use the command-line tool `argos <https://pypi.org/project/argos/>`_ to inspect the output files for
quick-look purposes. Argos is part of the ``tools`` dependency group of PlatoSim's Python installation (see
:ref:`Python packages <install_source_python>`). Open an HDF5 file with:

.. code-block:: shell

   argos <outputFileName>.hdf5

To extract information from the HDF5 file we strongly recommend you to use PlatoSim's ``SimFile`` class, which is also
returned by ``Simulation.run()``:

.. code-block:: python

   from platosim.simfile import SimFile

   simfile = SimFile("<outputFileName>.hdf5")
   image   = simfile.getImage(0)                 # Subfield pixel map of the first exposure
   stars   = simfile.getStarCatalog(df=True)     # Star catalogue as a pandas DataFrame
   cosmics = simfile.getCosmicsInfo(0, df=True)  # Cosmic hits on the subfield in the first exposure

We show how these tools work in our `Python Tutorials <https://github.com/IvS-KULeuven/PlatoSim3/tree/develop/docs/tutorials>`_.
