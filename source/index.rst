Welcome to PlatoSim's documentation!
====================================

The PLATO Simulator (PlatoSim) is an end-to-end software tool, designed to perform realistic simulations of the
expected observations of the PLATO mission. PlatoSim simulates time series of CCD images by modelling the stellar
field, the spacecraft systematics, the jitter of the spacecraft, the camera optics, the CCD and its electronics, and
all important natural sources of noise.

.. image:: ../figures/logo_platosim.png
   :align: center
   :width: 600

PlatoSim is used by the PLATO Mission Consortium (PMC) for performance assessments of the mission and for scientific
studies related to its core and complementary science programmes.

**Where to start?**

- **New to PlatoSim?** Follow the :doc:`Quickstart <basic_quickstart>` to install PlatoSim and run your first
  simulation in a few minutes.
- **Installing or updating:** see the :doc:`installation overview <install_overview>` for the requirements and the two
  installation routes.
- **Running simulations:** see :doc:`Running PlatoSim <run_overview>`. We recommend to use PlatoSim's Python
  interface, which is explained in our `Python tutorials
  <https://github.com/IvS-KULeuven/PlatoSim3/tree/develop/docs/tutorials>`_.
- **Many cameras, imagettes, or light curves:** have a look at the :doc:`PLATOnium <platonium_overview>` toolkit.
- **Something not working?** See :doc:`Troubleshooting <basic_troubleshooting>`.
- **How does it work?** See the :doc:`Architecture <basic_architecture>` and the :doc:`PlatoSim paper
  <basic_acknowledgements>`, which you should also cite when you use PlatoSim.

.. raw:: html

   <hr>


Contents
========

.. toctree::
   :maxdepth: 1
   :caption: General:

   basic_quickstart
   basic_architecture
   basic_troubleshooting
   basic_acknowledgements
   
.. toctree::
   :maxdepth: 1
   :caption: Installing & Updating:

   install_overview
   install_conda
   install_source
   install_migtool
   
.. toctree::
   :maxdepth: 1
   :caption: Running PlatoSim

   run_overview
   run_prerequisites
   run_simulations
   run_input
   run_output
   
.. toctree::
   :maxdepth: 1
   :caption: PLATOnium toolkit

   platonium_overview
   platonium_prerequisites
   platonium_tutorials
   platonium_pipeline_v2

   
	     
