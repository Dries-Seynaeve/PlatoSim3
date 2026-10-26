Quickstart
==========

The easiest way to install PlatoSim is through our :doc:`Conda installation <install_conda>` using the following set of terminal commands to install the ``master`` branch:

.. code-block:: shell

   conda create -n platosim python=3.12
   conda activate platosim
   conda config --add channels conda-forge
   conda install -c https://plato:miSotalP@jenkins.miricle.org/platosim/ platosim

Create a working directory for which you want to store all your future PlatoSim projects (e.g. using
``platosim_workdir`` as our project folder and create a first project called ``quickstart``):

.. code-block:: shell

   mkdir -p $HOME/platosim_workdir/quickstart

Export the following paths (and add them to your bash file for future sessions):

.. code-block:: shell

   export PLATO_PROJECT_HOME=$CONDA_PREFIX
   export PLATO_WORKDIR=$HOME/platosim_workdir
   export PYTHONPATH=$PYTHONPATH:$PLATO_PROJECT_HOME/python

Install the Python packages with `uv <https://docs.astral.sh/uv/>`_ (see :ref:`Python packages <install_conda_python>`
for details), using the `pyproject.toml <https://github.com/IvS-KULeuven/PlatoSim3/blob/master/pyproject.toml>`_ file
of the branch you installed:

.. code-block:: shell

   cd $CONDA_PREFIX/python
   wget https://raw.githubusercontent.com/IvS-KULeuven/PlatoSim3/master/pyproject.toml
   uv pip install -r pyproject.toml

PlatoSim can also use a realistic (mapped) PSF, which is not included in the default distribution because of its size.
If you want to use it, download it from our FTP site:

.. code-block:: shell

   wget -P $PLATO_PROJECT_HOME/inputfiles ftp://plato:miSotalP@ftp.ster.kuleuven.be/PSF_Focus_0mu.hdf5

A simple test simulation from the command line looks like:

.. code-block:: shell

   platosim $PLATO_PROJECT_HOME/inputfiles/inputfile.yaml $PLATO_WORKDIR/quickstart/test.hdf5

A simple test simulation from Python looks like:

.. code-block:: python

   import os
   from platosim.simulation import Simulation
   odir = os.getenv('PLATO_WORKDIR') + '/quickstart'
   sim = Simulation('test', outputDir=odir)
   simfile = sim.run()

We also recommend using :doc:`Platonium <platonium_overview>`, a user-friendly PlatoSim toolkit with lots of extra
functionalities that help you create and run simulation projects very efficiently. Install its extra packages with
``uv pip install -r pyproject.toml --group picsim --group varsim --group platonium`` and follow :doc:`Platonium's prerequisites
<platonium_prerequisites>`.
