Overview
========

Running PlatoSim always follows the same three steps:

1. **Install PlatoSim**, either :doc:`with Conda <install_conda>` or :doc:`from source <install_source>` (see the
   :doc:`installation overview <install_overview>`). When you update PlatoSim, rebuild the code (source installation
   only) and update the Python packages as described on those pages.
2. **Set the environment variables** that tell PlatoSim where it is installed and where your own files are (see
   :doc:`Prerequisites <run_prerequisites>`).
3. **Run a simulation**, from the command line or from Python (see :doc:`Run simulations <run_simulations>`). A
   simulation is configured with a YAML :doc:`input file <run_input>` and writes its results to an HDF5 :doc:`output
   file <run_output>`.

The default configuration runs out of the box: you do not need to download extra data. Realistic (mapped) PSFs are
optional and can be downloaded separately (see :ref:`PSF data packages <run_input_data>`).
