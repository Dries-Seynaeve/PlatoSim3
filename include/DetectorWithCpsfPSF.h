#ifndef DETECTORWITHCPSFPSF_H
#define DETECTORWITHCPSFPSF_H

#include <memory>
#include <string>

#include "DetectorWithAnalyticNonGaussianPSF.h"
#include "cpsfs.h"

using namespace std;

/**
 * \brief Detector that projects the analytic representation of the ray-traced
 *        PLATO PSFs (PLATO-KUL-PL-TN-0030).
 *
 * \details
 *
 * This derives from DetectorWithAnalyticNonGaussianPSF and overrides exactly
 * one method, addFluxToMap().  Everything else -- addFlux() and its subfield
 * bookkeeping, integrateLight(), the flatfield, the photometry, the HDF5
 * output -- is inherited unchanged, so the existing analytic model keeps
 * working bit for bit and this class is reached only when PSF/Model is set to
 * the new value.
 *
 * What the override replaces is the shape of the projected PSF.  Instead of
 * evaluating a parametric analytic response, it evaluates the measured PSF of
 * the ray traces at the star's own field position, from a coefficient table.
 * The output is a pixel-integrated npix x npix image obtained directly from
 * the optical transfer function, without the 512 x 512 inverse transform --
 * which is possible precisely because addFluxToMap() writes the PIXEL map, at
 * camera-pixel resolution.
 *
 * Coordinates.  addFluxToMap() receives the star's polar position as (r, p),
 * with r in DEGREES -- addFlux() applies rad2deg() to
 * Camera::getGnomonicRadialDistanceFromOpticalAxis(), which itself returns
 * radians -- and p in radians.  Both refer to the DISTORTED focal plane, since
 * Camera::exposeDetectorWithStars() applies the distortion before calling
 * addFlux().  The coefficient table is indexed by the UNDISTORTED field angle,
 * so the override converts back:
 *
 *   1. x_dist = f tan(r) cos(p),  y_dist = f tan(r) sin(p)      [mm]
 *   2. (x_und, y_und) = Camera::distortedToUndistortedFocalPlaneCoordinates(),
 *      when Camera/IncludeFieldDistortion is true
 *   3. field angles atan(x_und / f), atan(y_und / f)            [deg]
 *
 * NOTE ON NAMING.  Throughout PlatoSim, xFP and yFP are focal-plane coordinates
 * in MILLIMETRES, and that meaning is kept here.  The coefficient table is
 * indexed by field ANGLE instead, so the quantities derived in step 3 are named
 * xFieldAngleDeg and yFieldAngleDeg rather than xfp/yfp, and log messages state
 * the millimetre position first.  (Within cpsfs itself the stored grid is
 * called xfp_deg / yfp_deg, which is why the two conventions have to be kept
 * apart at this boundary.)
 *
 * Round-tripping this chain against the sampled grid agrees to 1.9e-13 deg.
 * Note that omitting the degrees-to-radians conversion in step 1 is not a
 * subtle error: it misplaces the field position by up to 78.9 deg.
 *
 * REVIEW POINT.  The relative orientation of the cpsfs field grid and
 * PlatoSim's focal-plane axes is a property of how the Zemax ray traces were
 * exported, which cannot be settled from either code base alone: cpsfs.lookup
 * carries XFP_DISTORTED_SIGN = -1 for the distorted x component of the stored
 * grid.  FieldSignX and FieldSignY therefore expose the choice explicitly and
 * default to +1.  Confirm them against a known field position before trusting
 * absolute astrometry.
 */

class DetectorWithCpsfPSF: public DetectorWithAnalyticNonGaussianPSF
{
    public:

        DetectorWithCpsfPSF(ConfigurationParameters &configParam, HDF5File &hdf5File, Camera &camera, TemperatureGenerator &feeTemperatureGenerator, TemperatureGenerator &detectorTemperatureGenerator, double readoutTimeBeforeNextExposure, double readoutTimeDuringNextExposure);

        /**
         * \brief Add the ray-traced PSF of this field position to the pixel map.
         *
         * \param map:     Pixel map to which the flux is added [e-/pixel].
         * \param row0:    Row    of the star in the subfield, real-valued, integers
         *                 at pixel edges [pixels].
         * \param column0: Column of the star in the subfield, same convention [pixels].
         * \param r:       Gnomonic radial distance from the optical axis [degrees].
         * \param p:       Position angle in the focal plane [radians].
         * \param flux:    Total flux of the star over this time step [photons].
         *
         * \return Whether the footprint overlaps the map at all.  This reproduces
         *         the behaviour of the base-class implementation, which returns
         *         false only when the footprint lies entirely outside the map --
         *         not, as its comment states, whether the entire PSF is on the map.
         */
        bool addFluxToMap(arma::Mat<float>& map, double row0, double column0, double r, double p, double flux) override;

    protected:

        void configureCpsf(ConfigurationParameters &configParam);

        /// Undistorted focal-plane position [mm] and the corresponding field
        /// angles [deg], from the polar coordinates handed to addFluxToMap().
        void fieldAngleFromPolar(double r_deg, double p_rad,
                                 double &xFPmm, double &yFPmm,
                                 double &xFieldAngleDeg, double &yFieldAngleDeg) const;

        /// Re-bake the projector when the star has moved to a new field cache cell.
        void refreshField(double xFieldAngleDeg, double yFieldAngleDeg);

        shared_ptr<cpsfs::CPSF> cpsfModel;             // Coefficient table and field interpolation
        shared_ptr<cpsfs::Evaluator> cpsfEvaluator;    // Scratch buffers and FFT plan
        shared_ptr<cpsfs::CameraProjector> cpsfProjector;

        string cpsfCoefficientFileName;   // Path of the cpsf_coeffs_*.h5 file
        int cpsfNumberOfPixels;           // Footprint of the ray-traced PSF [pixels]
        double cpsfFieldCacheStep;        // Field-angle quantisation of the OTF cache [deg]
        double cpsfOutsideFieldLimit;     // Beyond this field radius the model raises [deg]
        double cpsfFieldSignX;            // Orientation of the field grid w.r.t. PlatoSim's x axis
        double cpsfFieldSignY;            // Orientation of the field grid w.r.t. PlatoSim's y axis
        bool cpsfIncludeFieldDistortion;  // Mirrors Camera/IncludeFieldDistortion
        bool cpsfPreloadCoefficients;     // Hold the whole table in memory, or read rows on demand

        double cachedFieldX, cachedFieldY;    // Centre of the cached field cell [deg]
        bool cachedFieldValid;
        long cpsfOutsideFieldCount;           // Stars refused because they lie outside the model

        vector<float> cpsfFootprint;          // npix * npix scratch, row-major
};

#endif
