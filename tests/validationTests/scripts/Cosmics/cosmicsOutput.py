from test import Test, eprint
import numpy as np





"""
These tests check that the cosmics written to the HDF5 file are complete and
consistent, in the ragged-array layout of /Cosmics/<field>:

  - per cosmic (entryRows, entryColumns, entryAngles, trailLengths, intensities):
    exposure i is field[offsets[i]:offsets[i+1]];
  - per pixel hit by a cosmic (rows, columns, flux):
    exposure i is field[pixelOffsets[i]:pixelOffsets[i+1]].

Two simulations are run with all other effects switched off, identical except for
the cosmics, so that the difference image of an exposure is exactly the flux the
cosmics added. For every field and every exposure the tests check:

  1. structure: both offsets tables have NumExposures + 1 entries, start at 0,
     never decrease, and end at the lengths of the datasets they index;
  2. ranges:    entry and hit pixels lie on the map, angles in [0, 2 pi],
                trail lengths within the TrailLength input range, intensities
                and fluxes positive;
  3. links:     the entry pixel of every cosmic is one of the hit pixels of the
                same exposure;
  4. image:     the difference image equals the stored per-pixel flux.

Checks 3 and 4 tie the per-cosmic and the per-pixel data to each other and to
the image, so they fail when the two are written or indexed inconsistently.
The checks only use general properties of cosmics, not the trail model used
to generate them, so changing that model does not affect these tests.
"""



# For each cosmics field: the map it is stored in (as used by SimFile.getMap)
MAPS = {"SubField":     "Images",
        "SmearingMap":  "SmearingMaps",
        "BiasMapLeft":  "BiasMapsLeft",
        "BiasMapRight": "BiasMapsRight"}

PER_COSMIC = ["entryRows", "entryColumns", "entryAngles", "trailLengths", "intensities"]
PER_PIXEL  = ["rows", "columns", "flux"]





def checkField(simWith, simWithout, field, numExposures, trailRange, mustHaveCosmics):

    """
    Run checks 1 to 4 for one cosmics field. Returns True if all pass and prints
    every failing check (with the exposure and numbers) to stderr.
    """

    failures = []
    fail = lambda message: failures.append(f"{field}: {message}")

    hdf5 = simWith.hdf5file
    if "Cosmics" not in hdf5 or field not in hdf5["Cosmics"]:
        eprint(f"{field}: no /Cosmics/{field} group in the output")
        return False

    group = hdf5["Cosmics"][field]
    missing = [name for name in PER_COSMIC + PER_PIXEL + ["offsets", "pixelOffsets"] if name not in group]
    if missing:
        eprint(f"{field}: missing datasets {missing}")
        return False

    data         = {name: group[name][:] for name in PER_COSMIC + PER_PIXEL}
    offsets      = group["offsets"][:].astype(np.int64)
    pixelOffsets = group["pixelOffsets"][:].astype(np.int64)

    # 1. Structure

    for name, table, fields in [("offsets", offsets, PER_COSMIC), ("pixelOffsets", pixelOffsets, PER_PIXEL)]:
        if len(table) != numExposures + 1:
            fail(f"{name} has {len(table)} entries, expected {numExposures + 1}")
            continue
        if table[0] != 0 or np.any(np.diff(table) < 0):
            fail(f"{name} does not start at 0 or decreases: {table}")
        for f in fields:
            if len(data[f]) != table[-1]:
                fail(f"{f} has {len(data[f])} values, but {name} ends at {table[-1]}")

    if failures:
        for message in failures: eprint(message)
        return False

    if mustHaveCosmics and offsets[-1] == 0:
        fail(f"no cosmics written, although cosmics are included for this field")

    firstNr = simWith.getFirstExposureNr()
    for i in range(numExposures):

        exposureNr = firstNr + i
        cosmic = {f: data[f][offsets[i]:offsets[i+1]] for f in PER_COSMIC}
        pixel  = {f: data[f][pixelOffsets[i]:pixelOffsets[i+1]] for f in PER_PIXEL}

        imageWith    = simWith.getMap(MAPS[field], imageNr=exposureNr).astype(np.float64)
        imageWithout = simWithout.getMap(MAPS[field], imageNr=exposureNr).astype(np.float64)
        shape        = imageWith.shape

        # 2. Ranges (stop checking this field if they fail: the data cannot be indexed)

        numFailures = len(failures)
        if np.any(cosmic["entryRows"] >= shape[0]) or np.any(cosmic["entryColumns"] >= shape[1]):
            fail(f"exposure {exposureNr}: entry pixel outside the {shape} map")
        if np.any(pixel["rows"] >= shape[0]) or np.any(pixel["columns"] >= shape[1]):
            fail(f"exposure {exposureNr}: hit pixel outside the {shape} map")
        if np.any(cosmic["entryAngles"] < 0) or np.any(cosmic["entryAngles"] > 2 * np.pi):
            fail(f"exposure {exposureNr}: entry angle outside [0, 2 pi]")
        if np.any(cosmic["trailLengths"] < trailRange[0]) or np.any(cosmic["trailLengths"] > trailRange[1]):
            fail(f"exposure {exposureNr}: trail length outside {trailRange}")
        if np.any(cosmic["intensities"] <= 0) or np.any(pixel["flux"] <= 0):
            fail(f"exposure {exposureNr}: non-positive intensity or flux")
        if len(failures) > numFailures:
            break

        # Stored per-pixel flux, summed per pixel (a pixel can be hit more than once)

        storedFlux = np.zeros(shape)
        np.add.at(storedFlux, (pixel["rows"].astype(int), pixel["columns"].astype(int)), pixel["flux"])

        # 3. Links: every entry pixel is a hit pixel of the same exposure

        hitPixels = set(zip(pixel["rows"].tolist(), pixel["columns"].tolist()))
        notHit = [(r, c) for r, c in zip(cosmic["entryRows"].tolist(), cosmic["entryColumns"].tolist())
                  if (r, c) not in hitPixels]
        if notHit:
            fail(f"exposure {exposureNr}: {len(notHit)} of {len(cosmic['entryRows'])} entry pixels "
                 f"are not among the hit pixels, e.g. {notHit[:3]}")

        # 4. Image: the difference image must be the stored flux. The images are float32
        #    (about 1e-3 e- resolution at the background level), so allow 0.2 e- per pixel.

        difference = imageWith - imageWithout
        offPixels  = np.abs(difference - storedFlux) > 0.2 + 1e-5 * storedFlux
        if np.any(offPixels):
            r, c = np.argwhere(offPixels)[0]
            fail(f"exposure {exposureNr}: {offPixels.sum()} pixels where the image difference does not "
                 f"match the stored flux, e.g. pixel ({r}, {c}): image {difference[r, c]:.3f} e-, "
                 f"stored {storedFlux[r, c]:.3f} e-")

    for message in failures:
        eprint(message)
    return not failures





class CosmicsOutputBase(Test):

    """
    Common set-up: a small subfield without stars and several exposures. Each test
    chooses in which maps cosmics are included (`includeIn`), which fields it
    checks (`fields`) and a cosmic hit rate (`hitRate`) high enough that every
    exposure has cosmics in every checked field.
    """

    fields   = []
    includeIn = []          # Any of "SubField", "SmearingMap", "BiasMap"
    hitRate  = 300          # [events / cm^2 / s]

    def setAllEffects(self):

        super().setAllEffects()

        self.numExposures = 4
        self.sim["ObservingParameters/NumExposures"] = self.numExposures
        self.sim["SubField/NumRows"]    = 60
        self.sim["SubField/NumColumns"] = 60

        # Point to an empty part of the sky (as in cosmics.py), so the images only
        # contain the constant background and the cosmics

        self.sim["Platform/Orientation/Angles/DecPointing"] = - self.sim["Platform/Orientation/Angles/DecPointing"]

        self.sim["Sky/Cosmics/CosmicHitRate"] = self.hitRate
        self.sim["ControlHDF5Content/WriteCosmics"] = "yes"


    def includeCosmics(self, include):

        for area in ["SubField", "SmearingMap", "BiasMap"]:
            self.sim["Sky/IncludeCosmicsIn" + area] = "yes" if (include and area in self.includeIn) else "no"


    def runSimulation(self):

        self.includeCosmics(False)
        self.simFileWithoutCosmics = self.sim.run(removeOutputFile = True)

        self.includeCosmics(True)
        self.simFileWithCosmics = self.sim.run(removeOutputFile = True)


    def compare(self):

        trailRange = tuple(float(x) for x in self.sim["Sky/Cosmics/TrailLength"])
        results = [checkField(self.simFileWithCosmics, self.simFileWithoutCosmics, field,
                              self.numExposures, trailRange, mustHaveCosmics=True)
                   for field in self.fields]
        return all(results)





class CosmicsOutputSubField(CosmicsOutputBase):

    """
    Cosmics in the subfield (image area).
    """

    fields    = ["SubField"]
    includeIn = ["SubField"]

    def setNr(self):
        self.nr = "046"





class CosmicsOutputSmearingMap(CosmicsOutputBase):

    """
    Cosmics in the smearing map, together with the subfield as in a normal run.
    """

    fields    = ["SmearingMap"]
    includeIn = ["SubField", "SmearingMap"]

    def setNr(self):
        self.nr = "047"





class CosmicsOutputBiasMaps(CosmicsOutputBase):

    """
    Cosmics in the left and right bias maps only.

    A bias-map row is only exposed while it is read out (about 1 ms), so at
    realistic hit rates a bias map is almost never hit. A very high hit rate
    gives a few hits per exposure; cosmics are left out of the subfield and the
    smearing map, which would otherwise get millions of hits.
    """

    fields    = ["BiasMapLeft", "BiasMapRight"]
    includeIn = ["BiasMap"]
    hitRate   = 5.0e6

    def setNr(self):
        self.nr = "048"





if __name__ == "__main__":
    for test in [CosmicsOutputSubField(), CosmicsOutputSmearingMap(), CosmicsOutputBiasMaps()]:
        print(type(test).__name__, test.run())
