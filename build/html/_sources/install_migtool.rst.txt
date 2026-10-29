Migration tool
==============

New versions of PlatoSim sometimes change the configuration file: parameters are added, removed, or moved to another
block (e.g. ``CCD/FlatfieldNoiseRMS`` became ``CCD/Flatfield/FromRedNoise/FlatfieldNoiseRMS`` in version 3.7.1). A
configuration file from an older version then no longer works, and PlatoSim stops with a configuration error.

This does not affect you if you configure your simulations in Python, starting from the default ``inputfile.yaml``: the
default file always matches your installed version. It does affect you if you keep your own YAML configuration files.
The migration tool updates such a file to the format of your installed version.

.. raw:: html

   <hr>

*Usage*
-------

The migration tool compares your old configuration file with the default configuration file of your installation,
``$PLATO_PROJECT_HOME/inputfiles/inputfile.yaml``. Make sure that ``PLATO_PROJECT_HOME`` is set and that the
environment with PlatoSim's Python packages is active, then run:

.. code-block:: shell

   python $PLATO_PROJECT_HOME/python/platosim/scripts/migtool.py -v -o new_inputfile.yaml old_inputfile.yaml

If you ran the PLATOnium ``setup.sh`` script, the tool is also available as the command ``migtool``. The options are:

.. list-table::
   :widths: 25 75

   * - ``old_inputfile.yaml``
     - Your old configuration file.
   * - ``-o new_inputfile.yaml``
     - The file to which the updated configuration is written. Without this option, the result is printed on the
       screen.
   * - ``-v``
     - Print the changes that are made (recommended).
   * - ``-h``
     - Print a help message.

.. raw:: html

   <hr>

*Checking the result*
---------------------

With ``-v``, every change is printed on a line that starts with ``DONE`` or ``CHECK``:

* ``DONE`` means that a new parameter or block was added (with its default value), or that an obsolete one was
  removed.
* ``CHECK`` means that you should check the parameter yourself. The most common case is a parameter whose default value
  changed, for example:

  .. code-block:: text

     CHECK - Value changed for CCD.ReadoutNoise from 38.7 to 40.0

  Your own value (here 38.7) is kept, since you probably chose it on purpose. Decide whether you want to keep it or use
  the new default.

.. warning::

   * A parameter that was moved or renamed is treated as an obsolete parameter plus a new one. Your old value is
     removed and the new parameter gets its **default** value. Copy your value over by hand if you need it.
   * The comments in your configuration file are lost, because the YAML reader does not keep them.
