#include "DetectorWithCpsfPSF.h"

#include <cmath>
#include <stdexcept>

#include "FileUtilities.h"
#include "Exceptions.h"

/**
 * \brief Constructor.
 *
 * \details
 *
 * The base-class constructor runs first and performs its own configure(),
 * which reads the PSF/AnalyticNonGaussian block.  That block must therefore
 * remain present in the configuration file even for a Cpsf run.  Leaving it in
 * place is deliberate: it keeps the base class untouched, which is what makes
 * this addition non-invasive.  The parametric response it builds is simply
 * never evaluated, because addFluxToMap() is overridden.
 */

DetectorWithCpsfPSF::DetectorWithCpsfPSF(ConfigurationParameters &configParam, HDF5File &hdf5File, Camera &camera, TemperatureGenerator &feeTemperatureGenerator, TemperatureGenerator &detectorTemperatureGenerator, double readoutTimeBeforeNextExposure, double readoutTimeDuringNextExposure)
: DetectorWithAnalyticNonGaussianPSF(configParam, hdf5File, camera, feeTemperatureGenerator, detectorTemperatureGenerator, readoutTimeBeforeNextExposure, readoutTimeDuringNextExposure),
  cpsfNumberOfPixels(cpsfs::NCAM), cpsfFieldCacheStep(0.25), cpsfOutsideFieldLimit(20.0),
  cpsfFieldSignX(1.0), cpsfFieldSignY(1.0), cpsfIncludeFieldDistortion(true),
  cpsfPreloadCoefficients(true),
  cachedFieldX(0.0), cachedFieldY(0.0), cachedFieldValid(false), cpsfOutsideFieldCount(0)
{
    configureCpsf(configParam);
}



/**
 * \brief Read the PSF/Cpsf configuration and open the coefficient table.
 *
 * \param configParam: The configuration parameters.
 */

void DetectorWithCpsfPSF::configureCpsf(ConfigurationParameters &configParam)
{
    // getAbsoluteFilename(), not getString(): the configuration carries paths
    // relative to the project location, and every other model resolves them
    // this way -- see DetectorWithAnalyticNonGaussianPSF.cpp:131.

    cpsfCoefficientFileName = configParam.getAbsoluteFilename("PSF/Cpsf/CoefficientFileName");
    cpsfNumberOfPixels      = configParam.getInteger("PSF/Cpsf/NumberOfPixels");
    cpsfFieldCacheStep      = configParam.getDouble("PSF/Cpsf/FieldCacheStep");
    cpsfOutsideFieldLimit   = configParam.getDouble("PSF/Cpsf/OutsideFieldLimit");
    cpsfFieldSignX          = configParam.getDouble("PSF/Cpsf/FieldSignX");
    cpsfFieldSignY          = configParam.getDouble("PSF/Cpsf/FieldSignY");

    cpsfPreloadCoefficients = configParam.getBoolean("PSF/Cpsf/PreloadCoefficients");

    const bool includeDiffusion = configParam.getBoolean("PSF/Cpsf/IncludeChargeDiffusion");
    const double diffusionWidth = configParam.getDouble("PSF/Cpsf/ChargeDiffusionStrength");

    // The distortion state has to agree with Camera, because the (r, p) handed
    // to addFluxToMap() are distorted exactly when Camera applied the distortion.

    cpsfIncludeFieldDistortion = configParam.getBoolean("Camera/IncludeFieldDistortion");

    if (cpsfNumberOfPixels <= 0 || cpsfs::NFINE % cpsfNumberOfPixels != 0)
    {
        string errorMessage = "DetectorWithCpsfPSF: PSF/Cpsf/NumberOfPixels must divide "
                              + to_string(cpsfs::NFINE) + " exactly";
        Log.error(errorMessage);
        throw IllegalArgumentException(errorMessage);
    }

    if (!FileUtilities::fileExists(cpsfCoefficientFileName))
    {
        throw FileException("DetectorWithCpsfPSF: trying to load the PSF coefficient file ("
                            + cpsfCoefficientFileName + "), but file doesn't exist.");
    }

    Log.info("DetectorWithCpsfPSF: reading the PSF coefficients from " + cpsfCoefficientFileName);

    // The coefficient table is about 760 MiB.  Preloading it keeps every field
    // position resident; with PreloadCoefficients set to no, rows are read on
    // demand through a hyperslab and cached, which for a run that visits only
    // part of the field costs far less memory.
    //
    // The load is guarded because the HDF5 C++ API reports failures by
    // throwing, and an exception escaping a constructor would abort the whole
    // simulator with nothing but the exception type.

    try
    {
        cpsfModel = make_shared<cpsfs::CPSF>(
            cpsfs::CPSF::load(cpsfCoefficientFileName, cpsfs::OUTSIDE_RAISE,
                              cpsfOutsideFieldLimit, cpsfPreloadCoefficients));
    }
    catch (const std::exception &error)
    {
        string errorMessage = string("DetectorWithCpsfPSF: cannot read the PSF coefficients: ")
                              + error.what();
        Log.error(errorMessage);
        throw FileException(errorMessage);
    }

    cpsfEvaluator = cpsfModel->makeEvaluator();
    cpsfProjector = make_shared<cpsfs::CameraProjector>(
        cpsfModel->basis(), cpsfNumberOfPixels,
        includeDiffusion ? diffusionWidth : 0.0);

    cpsfFootprint.assign(size_t(cpsfNumberOfPixels) * cpsfNumberOfPixels, 0.0f);

    Log.info("DetectorWithCpsfPSF: " + to_string(cpsfModel->nNodes()) + " field positions, "
             + to_string(cpsfModel->nParams()) + " coefficients each");
    Log.info("DetectorWithCpsfPSF: sigma of charge diffusion: "
             + to_string(includeDiffusion ? diffusionWidth : 0.0) + " pix");
    Log.info("DetectorWithCpsfPSF: field cache step: " + to_string(cpsfFieldCacheStep) + " deg");
}



/**
 * \brief Undistorted field angle from the polar coordinates of addFluxToMap().
 *
 * \param r_deg:   Gnomonic radial distance from the optical axis [degrees].
 * \param p_rad:   Position angle in the focal plane [radians].
 * \param xFPmm:          Returned undistorted focal-plane x-coordinate [mm].
 * \param yFPmm:          Returned undistorted focal-plane y-coordinate [mm].
 * \param xFieldAngleDeg: Returned undistorted field angle along x [degrees].
 * \param yFieldAngleDeg: Returned undistorted field angle along y [degrees].
 *
 * The millimetre position is returned alongside the angle because millimetres
 * are the focal-plane unit used everywhere else in PlatoSim; only the
 * coefficient table is indexed by angle.
 */

void DetectorWithCpsfPSF::fieldAngleFromPolar(double r_deg, double p_rad,
                                              double &xFPmm, double &yFPmm,
                                              double &xFieldAngleDeg, double &yFieldAngleDeg) const
{
    // Camera::getFocalLength() already returns millimetres: the configuration
    // value is in metres but Camera multiplies by 1000 when it builds the
    // parameter (Camera.cpp:320/328).  Scaling it again here is not harmless --
    // distortedToUndistortedFocalPlaneCoordinates() normalises by Camera's own
    // focal length, so a mismatched scale drives its normalised radius to a few
    // hundred, where the r^7 term of the distortion polynomial diverges.

    const double focalLength = camera.getFocalLength();            // [mm]

    // Back to distorted focal-plane millimetres.  r arrives in degrees, so the
    // conversion to radians here is load-bearing.

    const double rho = focalLength * tan(deg2rad(r_deg));
    double x = rho * cos(p_rad);
    double y = rho * sin(p_rad);

    // Undo the distortion that Camera applied before calling addFlux().

    if (cpsfIncludeFieldDistortion)
    {
        tie(x, y) = camera.distortedToUndistortedFocalPlaneCoordinates(x, y);
    }

    xFPmm = x;
    yFPmm = y;

    xFieldAngleDeg = cpsfFieldSignX * rad2deg(atan(x / focalLength));
    yFieldAngleDeg = cpsfFieldSignY * rad2deg(atan(y / focalLength));
}



/**
 * \brief Bake the optical transfer function of a field cell into the projector.
 *
 * \details
 *
 * Everything that depends on the field position but not on the sub-pixel
 * placement -- the coefficient interpolation and the reconstruction of the
 * optical transfer function -- is done here, once per field cell, rather than
 * once per star.  The reconstruction is about 86 per cent of the cost of a
 * full PSF evaluation, so for a crowded subfield this is what makes the run
 * affordable.  The price is that the field position is quantised to
 * PSF/Cpsf/FieldCacheStep; at 0.25 deg that is well below the 0.5 deg spacing
 * of the sampled grid.
 *
 * \param xFieldAngleDeg: Undistorted field angle along x [degrees].
 * \param yFieldAngleDeg: Undistorted field angle along y [degrees].
 */

void DetectorWithCpsfPSF::refreshField(double xFieldAngleDeg, double yFieldAngleDeg)
{
    const double step = (cpsfFieldCacheStep > 0.0) ? cpsfFieldCacheStep : 0.0;

    double cellX = xFieldAngleDeg;
    double cellY = yFieldAngleDeg;
    if (step > 0.0)
    {
        cellX = step * floor(xFieldAngleDeg / step + 0.5);
        cellY = step * floor(yFieldAngleDeg / step + 0.5);
    }

    if (cachedFieldValid && cellX == cachedFieldX && cellY == cachedFieldY)
    {
        return;
    }

    cpsfs::FieldQuery query = cpsfModel->coefficients_at(cellX, cellY);
    cpsfProjector->setField(*cpsfEvaluator, query);

    cachedFieldX = cellX;
    cachedFieldY = cellY;
    cachedFieldValid = true;
}



/**
 * \brief Add the ray-traced PSF of this field position to the pixel map.
 *
 * \details
 *
 * The footprint is anchored by rounding the star's pixel coordinates, so that
 * the residual sub-pixel decentre stays within half a camera pixel, which is
 * the range over which the representation was validated.  With the footprint
 * placed at sy = round(row0) - npix/2, the centre of the projected PSF lands at
 * map coordinate row0 exactly, in the convention that integers sit at pixel
 * edges -- the same convention the base class uses.
 */

bool DetectorWithCpsfPSF::addFluxToMap(arma::Mat<float>& map, double row0, double column0, double r, double p, double flux)
{
    const int npix = cpsfNumberOfPixels;

    const int sy = int(lround(row0))    - npix / 2;
    const int sx = int(lround(column0)) - npix / 2;

    if (sx + npix <= 0 || sx >= (int)map.n_cols || sy + npix <= 0 || sy >= (int)map.n_rows)
    {
        return false;
    }

    // Field position of this star, and the optical transfer function there.

    double xFPmm, yFPmm, xFieldAngleDeg, yFieldAngleDeg;
    fieldAngleFromPolar(r, p, xFPmm, yFPmm, xFieldAngleDeg, yFieldAngleDeg);

    try
    {
        refreshField(xFieldAngleDeg, yFieldAngleDeg);
    }
    catch (const cpsfs::FieldError &error)
    {
        // The star lies outside the field covered by the ray traces.  Its flux
        // is not added, which is reported rather than passed over silently.

        // Report the inputs as well as the derived angle.  A field angle that
        // is wildly outside the +/-17.75 deg sampled square -- in particular one
        // saturating near +/-90 deg -- is not a star at the edge of the field
        // but a broken coordinate conversion, and the only way to tell the two
        // apart afterwards is to see (r, p) and the millimetre position next to
        // the angle.  The millimetre position comes first because that is the
        // focal-plane quantity the rest of PlatoSim reports.

        cpsfOutsideFieldCount++;
        Log.warning("DetectorWithCpsfPSF: no PSF available for the star at undistorted"
                    " focal-plane (xFP, yFP) = (" + to_string(xFPmm) + ", "
                    + to_string(yFPmm) + ") mm, i.e. field angles ("
                    + to_string(xFieldAngleDeg) + ", " + to_string(yFieldAngleDeg)
                    + ") deg, derived from r = " + to_string(r) + " deg, p = "
                    + to_string(rad2deg(p)) + " deg, at subfield (row, column) = ("
                    + to_string(row0) + ", " + to_string(column0)
                    + "); the flux of this star is not added. Occurrences so far: "
                    + to_string(cpsfOutsideFieldCount));
        return false;
    }

    // Sub-pixel decentre, in fine samples of the 512-point grid.

    const double dy = (row0    - double(lround(row0)))    * double(cpsfs::OVERSAMPLE);
    const double dx = (column0 - double(lround(column0))) * double(cpsfs::OVERSAMPLE);

    // Orientation of the CCD.  The base class carries it as rotationAnglePsf in
    // radians, and DetectorWithMappedPSF rotates its PSF by MINUS that angle, so
    // the same sign is used here.  The orientations in use are multiples of 90
    // degrees to better than a second of arc -- the largest departure over the
    // four CCDs is 0.75", which displaces the PSF by 1.5e-5 pixel across the
    // footprint -- so rounding to a quarter turn costs nothing measurable and,
    // unlike ArrayOperations::rotateArray, involves no interpolation.

    const int quarterTurns = -int(lround(rotationAnglePsf / (PI / 2.0)));

    cpsfProjector->project(dy, dx, quarterTurns, cpsfFootprint.data());

    // Accumulate, clipping to the bounds of the map exactly as the base class does.

    for (int y = max(0, sy); y < min((int)map.n_rows, sy + npix); y++)
    {
        for (int x = max(0, sx); x < min((int)map.n_cols, sx + npix); x++)
        {
            map.at(y, x) += cpsfFootprint[size_t(y - sy) * npix + (x - sx)] * flux;
        }
    }

    return true;
}
