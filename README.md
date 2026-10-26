# PlatoSim's Sphinx documentation

## Setting up Sphinx

The Sphinx packages are listed in the `docs` dependency group of `pyproject.toml` on the `develop` branch
(Sphinx 9 and the `renku-sphinx-theme`). Install them in your PlatoSim checkout of `develop` with:

	uv sync --group docs

and activate that environment (`source .venv/bin/activate`) before running `make` on this branch. Alternatively, run
Sphinx without installing anything:

	uvx --from 'sphinx>=9.1,<10' --with 'renku-sphinx-theme>=0.5.2,<0.6' sphinx-build -b html source build/html

* [The Sphinx documentation](https://www.sphinx-doc.org/en/master/index.html).

## How to update the documentation page:

Changes needs to be made to the files within `source/`. Note all figures are placed within the `figures/` directory. 

If you just want to see the changes in a locally you first need to:

	make html; firefox build/html/index.html

We here use firefox as a browser to visualise the html pages.

When you're happy with the changes simply run:

	make github

This will both create the html files and copy them to root. Note that this does not delete the html files of pages
you removed from `source/`; delete those (and their `_sources/` copies) by hand. Now commit your changes:

	git add *
	git commit -m "<message-of-update>"

Note that the above command add all files that have changed. Now push it to your fork:

	git push origin sphinx
	
Compare your fork `sphinx` with the upstream branch `sphinx` and merge the branches.

Wait 1-5 min for the webpage to change. 

## How to change GitHub documentation branch

You can always update the branch for which the GitHub looks for the documentation:

* Go to `Settings` and `Pages`
* Under Branch, change to your new branch.
