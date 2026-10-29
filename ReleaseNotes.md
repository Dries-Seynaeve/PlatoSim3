* Changelog PlatoSim 3.8.0



** Improvements

*** *Breaking change:* New layout of the HDF5 output file. The pixel, smearing, bias, throughput, and sub-pixel maps of all exposures are now stored in one 3D dataset per map type (`Images/subfield`, `SmearingMaps/smearing`, `BiasMapsLeft/bias`, `BiasMapsRight/bias`, `ThroughputMaps/throughput`, and `SubPixelImages/images`) instead of one dataset per exposure. The cosmics are stored as ragged arrays with `offsets` and `pixelOffsets` tables, in one sub-group per map (`SubField`, `SmearingMap`, `BiasMapLeft`, and `BiasMapRight`), and no longer depend on `GroupByExposure`. The `SimFile` class has been updated accordingly; scripts that read the HDF5 file directly need to be adapted.

*** *Breaking change:* The Python package management moved from Poetry to uv. `pyproject.toml` now uses the `[project]` table and `[dependency-groups]` (`picsim`, `varsim`, `platonium`, `pipeline`, `tools`, and `docs`). The lock file `uv.lock` is not tracked, so users resolve their own environment.

*** PlatoSim now requires Python >= 3.12 (and < 3.14).

*** Updated the HDF5 library from v1.12.0 to v2.2.0, and made `HDF5File.cpp` C++14 compliant.

*** Updated the Armadillo library from v6.500.4 to v15.6.0, and adapted its install script for Linux.

*** Updated the Faddeeva and yaml-cpp libraries to their latest versions.

*** `referenceFrames.skyToPixelCoordinates()` and `referenceFrames.getCCDandPixelCoordinates()` now accept vector input, as does the mapped distortion. Stars more than 90 degrees from the optical axis are now rejected.

*** Adapted `plot.plotSubfieldAnimation()` to the new HDF5 layout.

*** The pipeline in `platonium.py` now calls `gen_aflux_ts` with `--post-corr-bg`, stores the COB L0 and L1 files for both the nominal and extended masks, and includes updates to work with the latest pipeline.

*** `platonium.py` now accepts a target catalogue without a contaminant catalogue.

*** `setup.sh` now also exports `PYTHONPATH`.

*** `validation.switchOffEffects()` now also switches off the bad pixel map.

*** `statsmodels` is only imported in the function of `statistics.py` that needs it.

*** Unpinned the versions of `notebook`, `jupyter-client`, `traitlets`, and `tornado`, and added `setuptools` and `ipympl` to the dependencies.

*** Updated the tutorial and technical Jupyter notebooks (PIC, GO proposals, K-dwarf oscillators, Spikey, and Moonwalk).

*** Removed Poetry support and the tracked `uv.lock` file.

*** Removed the Python dependencies `holoviews` and `statsmodels`.

*** Removed the deprecated argument `normal` of `referenceFrames.skyToPixelCoordinates()`.

*** Removed the old dependency tarballs `hdf5-1.12.0.tar.gz` and `armadillo-6.500.4.tgz`.



** Deprecations

*** The Intel Mac (`osx-64`) Conda package is deprecated and will be dropped in a future release. Installing it shows a warning. Intel Mac users can move to an Apple Silicon Mac or Linux, or install PlatoSim from source.



** Bug fixes

*** Fixed issue #1191: Removed a duplicate call to `applyThroughputEfficiency()` for the analytic Gaussian PSF, which applied the throughput efficiency twice.

*** Bugfix of the readout time, which used the number of bias prescan rows instead of the number of bias prescan columns (both in `Simulation.cpp` and `simulation.py`).

*** Bugfix of the flatfield from file (`CCD/Flatfield/Source = FromFile`) for the mapped PSF, which expected a map at sub-pixel level. All PSF models now use the same pixel-level PRNU file.

*** Bugfix of the cosmics, where the flux of the first pixel of a cosmic trail was counted twice.

*** Bugfix of the cosmics in the bias maps, where the entry columns were drawn over the width of the subfield instead of the width of the bias map.

*** `platosim` now exits with an error code and a log message instead of aborting (and dumping core) when a simulation fails.

*** Bugfix of `picsim --vizier` when using the Gaia G magnitude.

*** Bugfixes in `picsim.py`, `varsim.py`, `varsource.py`, `starquery.py`, `plot.py`, and `simfile.py` after the upgrade to Python 3.12.

*** Bugfix of the target IDs in `platonium.py`.

*** Bugfix in `platonium.py` when copying pipeline output to existing directories.

*** Use raw strings for LaTeX in matplotlib labels to avoid warnings with Python 3.12.

*** Corrected typo "Inuaguration" -> "Inauguration" in `picsim.py`, `varsim.py`, and `varsource.py`.



** New features/functionality

*** Added a bad pixel map, following a Bernoulli distribution: new configuration parameters `CCD/BadPixelMap/includeBadPixelMap`, `CCD/BadPixelMap/badPixelParameter`, `RandomSeeds/BadPixelSeed`, and `ControlHDF5Content/WriteBadPixelMap`. The map is written to the HDF5 group `BadPixelMap`.

*** Added the option `--pipe_zarr_output` to `platonium.py` to store the pipeline output in zarr format.

*** Added the script `scripts/yamlio.py` to compare two YAML files. `platonium.py -v 3` now writes a `.yaml_diff` file with the differences between the input and output configuration.

*** Added the script `scripts/starcat2picsim.py` to convert a user ascii/csv catalogue into a PLATOnium target catalogue.

*** Added `simfile.showExtendedImage()`, `simfile.saveMapsToFITS()`, `simfile.getCosmicsGroup()`, and `simfile.getCosmicsOfExposure()`.

*** Added `starquery.fetch_gaia_info()`, `plot.plot_aitoff()`, a plot function for bright stars in `platonium.py`, and new plot functions in `mocka.py`.

*** Added validation tests for the cosmics in the new HDF5 layout.

*** Added new Jupyter notebooks for GO proposals, PIC, K-dwarf oscillators, target observability, and the Moonwalk paper.
