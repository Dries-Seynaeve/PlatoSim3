// cpsfs.h -- analytic representation of the PLATO ray-traced PSFs, C++ port.
//
// This is a faithful translation of the Python package `cpsfs` (modules
// `_otf.py`, `model.py` and the two helpers of `photometry.py` that the
// integration subset needs).  It reads the same coefficient files and is
// validated against the Python reference to machine precision; see README.md
// and the `--selftest` mode of the command-line tool.
//
// Dependencies, all of them already linked by PlatoSim3:
//   * HDF5 C++ API      (libhdf5_cpp)  -- reading the coefficient tables
//   * FFTW, single      (libfftw3f)    -- the 512x512 inverse transform
//   * Armadillo         (libarmadillo) -- the pseudo-inverse of the 49x15
//                                        field-interpolation design matrix
//
// Conventions, identical to the Python and to PLATO-KUL-PL-TN-0030:
//   * The PSF lives on a 512 x 512 grid of FINE samples which covers
//     8 x 8 CAMERA pixels, so 64 fine samples per camera pixel.
//   * Field position is a Cartesian pair of field angles in DEGREES along
//     focal-plane x and y (`xfp_deg`, `yfp_deg`); it is the undistorted
//     grid, related to undistorted focal-plane millimetres by
//     mm = 247.52 * tan(angle).  This is the same relation PlatoSim3's
//     PointSpreadFunction::generateDistortionMap() already uses.
//   * A decentre (`dy`, `dx`) is a displacement of the STAR on a fixed
//     camera-pixel grid, expressed in FINE SAMPLES (64 per camera pixel).
//   * The PSF sums to exactly 1 over the 512 x 512 grid.
//
// Thread safety: `CPSF` is immutable once loaded and its `coefficients_at`
// is safe to call concurrently.  `Evaluator` owns scratch buffers and an
// FFTW plan and is NOT safe to share; make one per thread.  The convenience
// methods `CPSF::psf_at` and friends use an internal evaluator and are
// therefore single-threaded -- use an explicit `Evaluator` if you need
// concurrency.

#ifndef CPSFS_H
#define CPSFS_H

#include <array>
#include <complex>
#include <cstddef>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cpsfs {

// ---------------------------------------------------------------------------
// Fixed geometry of the delivered products.
// ---------------------------------------------------------------------------

const int NFINE = 512;       // fine samples across the PSF array
const int OVERSAMPLE = 64;   // fine samples per camera pixel
const int NCAM = 8;          // camera pixels across the PSF array

typedef std::complex<float> cfloat;
typedef std::complex<double> cdouble;

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

// Thrown when a field position cannot be served.  `outside_footprint` is
// true when the position has too few sampled neighbours (outside the
// footprint or in one of its cut corners); `beyond_limit` is true when the
// field radius exceeds `outside_limit`.
class FieldError : public std::runtime_error
{
    public:
        FieldError(const std::string &what, bool outsideFootprint,
                   bool beyondLimit)
            : std::runtime_error(what),
              outside_footprint(outsideFootprint),
              beyond_limit(beyondLimit) {}

        bool outside_footprint;
        bool beyond_limit;
};

// ---------------------------------------------------------------------------
// Out-of-footprint policy, mirroring the Python `outside=` argument.
// ---------------------------------------------------------------------------

enum Outside
{
    OUTSIDE_RAISE    = 0,  // throw FieldError (default, and the only safe one)
    OUTSIDE_DEGRADE  = 1,  // widen the stencil, then lower the degree
    OUTSIDE_NEAREST  = 2   // as DEGRADE, then clamp to the nearest sampled node
};

// What the field interpolation actually did, for a single query.
struct FallbackInfo
{
    enum Kind { NONE = 0, DEGRADED = 1, CLAMPED = 2 };

    FallbackInfo() : kind(NONE), stencil(0), degree(0), distance_deg(0.0) {}

    Kind   kind;
    int    stencil;        // DEGRADED: the stencil that was actually used
    int    degree;         // DEGRADED: the degree that was actually used
    double distance_deg;   // CLAMPED: distance to the node that was used
};

// ---------------------------------------------------------------------------
// Coefficients and centroid at one field position.
// ---------------------------------------------------------------------------

struct FieldQuery
{
    std::vector<cdouble> coeff;   // n_params coefficients, model ordering
    double cy;                    // centroid row,    fine samples
    double cx;                    // centroid column, fine samples
    FallbackInfo fallback;
};

// ---------------------------------------------------------------------------
// The multiresolution OTF lattice: geometry of the coefficient vector and
// the reconstruction of the 512 x 512 centred OTF from it.
// ---------------------------------------------------------------------------

class Basis
{
    public:
        // `rho`, `rmax` and `smax` come from the coefficient file attributes.
        Basis(double rho, double rmax, int smax);

        int nParams() const { return n_params_; }
        double rho()  const { return rho_; }
        double rmax() const { return rmax_; }
        int    smax() const { return smax_; }

        // Zone list, finest first: (q_lo, q_hi, lattice step).
        struct Zone { double a; double b; int s; };
        const std::vector<Zone> &zones() const { return zones_; }
        const std::vector<int>  &sizes() const { return sizes_; }

    private:
        friend class Evaluator;

        double rho_, rmax_;
        int smax_, n_params_;
        std::vector<Zone> zones_;
        std::vector<int> sizes_;
        std::vector<double> Q_;            // NFINE*NFINE frequency radius
        // For each zone, a NFINE*NFINE table mapping a flat frequency index
        // to its position inside that zone's slice of the coefficient
        // vector, or -1.  This replaces the Python scatter into a temporary
        // 512 x 512 array.
        std::vector<std::vector<int> > slot_;
};

// ---------------------------------------------------------------------------
// Separable cubic-spline resampling of a step-s lattice onto the full
// 512 x 512 frequency grid.
//
// This reproduces scipy.ndimage.map_coordinates(order=3, mode="nearest")
// exactly, by the three steps that implementation actually performs:
//   1. replicate-pad the input by 12 samples on each side;
//   2. prefilter -- invert the discrete cubic B-spline kernel [1,4,1]/6
//      with a half-sample-symmetric boundary, which is a tridiagonal solve;
//   3. evaluate with the four cubic B-spline weights, indices clamped.
// Because the target coordinates form a tensor product, steps 2 and 3 are
// applied one axis at a time.  Agreement with SciPy was measured at
// 3.3e-16 on the operator and 1.1e-19 on a reconstructed PSF.
// ---------------------------------------------------------------------------

class SplinePlan
{
    public:
        // `s` is the lattice step; the source lattice is n x n with
        // n = NFINE / s.
        explicit SplinePlan(int s);

        int step() const { return s_; }
        int n()    const { return n_; }
        int nPadded() const { return npadded_; }

        // out (NFINE x NFINE, row-major) = S * in * S^T, with `in` the
        // n x n lattice values (row-major).  Scratch is supplied by the
        // caller so that the operation allocates nothing.
        void apply(const cdouble *in, cdouble *out,
                   std::vector<cdouble> &scratchA,
                   std::vector<cdouble> &scratchB) const;

        // The dense NFINE x n operator, for validation only.
        std::vector<double> matrix() const;

    private:
        // In-place: replicate-pad each row of `a` (nrows x npadded_, with
        // the n valid samples already at offset NPAD) and prefilter it.
        void prefilterRows(cdouble *a, int nrows) const;
        // Evaluate each row of `a` (nrows x npadded_) at the NFINE targets.
        void evalRows(const cdouble *a, int nrows, cdouble *out) const;

        int s_, n_, npadded_;
        std::vector<int> i0_;                     // NFINE first tap index
        std::vector<std::array<double, 4> > w_;   // NFINE four weights
        std::vector<double> cp_;                  // Thomas elimination
        std::vector<double> inv_;                 // Thomas pivots
};

// ---------------------------------------------------------------------------
// Scratch buffers and FFTW plan for turning coefficients into a PSF.
// One per thread.
// ---------------------------------------------------------------------------

class Evaluator
{
    public:
        explicit Evaluator(const std::shared_ptr<Basis> &basis);
        ~Evaluator();

        // Centred OTF on the 512 x 512 frequency grid, FFT order.
        const std::vector<cdouble> &reconstruct(const std::vector<cdouble> &coeff);

        // PSF on the fine grid, placed at (cy, cx) in fine samples.
        // The float overload is the one a simulator wants (PlatoSim3 carries
        // PSFs as arma::fmat); the double overload exists so that validation
        // against the Python measures the algorithm rather than the rounding
        // of the output type.
        void psf(const std::vector<cdouble> &coeff, double cy, double cx,
                 bool nonneg, std::vector<float> &out);
        void psf(const std::vector<cdouble> &coeff, double cy, double cx,
                 bool nonneg, std::vector<double> &out);

    private:
        Evaluator(const Evaluator &);
        Evaluator &operator=(const Evaluator &);

        template <typename T>
        void psfInto(const std::vector<cdouble> &coeff, double cy, double cx,
                     bool nonneg, std::vector<T> &out);

        void zoneGrid(int z, const std::vector<cdouble> &coeff,
                      const std::vector<cdouble> *fprev,
                      std::vector<cdouble> &g);

        std::shared_ptr<Basis> basis_;
        std::map<int, std::shared_ptr<SplinePlan> > plans_;
        std::vector<cdouble> fhat_, fz_a_, fz_b_, scratchA_, scratchB_, g_;
        struct FFT;                       // hides the FFTW types
        std::unique_ptr<FFT> fft_;
};

// ---------------------------------------------------------------------------
// The model: coefficient table on the 0.5 degree field grid, plus the local
// weighted least-squares interpolation between its nodes.
// ---------------------------------------------------------------------------

class CoeffStore;

class CPSF
{
    public:
        // Reads a cpsf_coeffs_*.h5 file.  With `preload` true the whole
        // coefficient table is held in memory (795 MB for the delivered
        // rho = 64 products); with it false, rows are read on demand
        // through a hyperslab and cached.
        static CPSF load(const std::string &path,
                         Outside outside = OUTSIDE_RAISE,
                         double outside_limit = 20.0,
                         bool preload = true);

        // --- the analytic function -----------------------------------------

        // Coefficients and centroid at an arbitrary field position.
        // `stencil` is the half-width of the node neighbourhood in grid
        // steps (3 -> 7x7 nodes spanning 3 degrees); `degree` is the total
        // polynomial degree, (degree+1)(degree+2)/2 terms; `weight` is the
        // inverse-distance exponent that turns the fit into a moving
        // least-squares interpolant, exact at the nodes.  Pass weight <= 0
        // for the unweighted smoother.
        FieldQuery coefficients_at(double xfp_deg, double yfp_deg,
                                   int stencil = 3, int degree = 4,
                                   double weight = 2.0) const;

        // PSF on the 512 x 512 fine grid, row-major, summing to 1.
        // `nonneg` clips the small negative wing values and renormalises,
        // which is the Python default.
        std::vector<float> psf_at(double xfp_deg, double yfp_deg,
                                  double dy = 0.0, double dx = 0.0,
                                  bool nonneg = true) const;

        // 8 x 8 camera image, row-major.
        std::array<float, NCAM * NCAM>
        camera_image_at(double xfp_deg, double yfp_deg,
                        double dy = 0.0, double dx = 0.0,
                        bool nonneg = true) const;

        // Ensquared energy over npix x npix whole camera pixels.  The block
        // is identified by its lower-left camera-pixel index; pass
        // block_i = block_j = -1 for the brightest block.
        double ensquared_energy_at(double xfp_deg, double yfp_deg,
                                   double dy = 0.0, double dx = 0.0,
                                   int npix = 2,
                                   int block_i = -1, int block_j = -1,
                                   bool nonneg = true) const;

        // --- the sampled grid ----------------------------------------------

        int nNodes() const { return n_nodes_; }
        int nParams() const { return n_params_; }
        const std::vector<double> &xfpDeg() const { return xfp_deg_; }
        const std::vector<double> &yfpDeg() const { return yfp_deg_; }

        // Index of the sampled node nearest a field position.
        int nearestNode(double xfp_deg, double yfp_deg) const;

        // The real focal-plane position of each sampled node, distortion
        // included, in millimetres.  Signs follow cpsfs.lookup: x_mm
        // increases with xfp_deg.
        const std::vector<double> &xfpDistortedMm() const { return xmm_; }
        const std::vector<double> &yfpDistortedMm() const { return ymm_; }

        // Inverse of the distortion: the field angle whose spot lands at a
        // given distorted focal-plane position.  Obtained by a local
        // weighted least-squares fit of the angle grid against the
        // millimetre grid over the neighbourhood of the nearest node, so it
        // is exact at the nodes.  Both arguments and the field angles are
        // in millimetres and degrees respectively.
        void angleFromDistortedMm(double x_mm, double y_mm,
                                  double &xfp_deg, double &yfp_deg,
                                  int stencil = 3, int degree = 2,
                                  double weight = 2.0) const;

        // Index of the sampled node whose distorted position is nearest.
        int nearestNodeMm(double x_mm, double y_mm) const;

        // Coefficients and centroid of one sampled node, no interpolation.
        FieldQuery atNode(int node) const;

        // --- metadata and diagnostics --------------------------------------

        const std::shared_ptr<Basis> &basis() const { return basis_; }
        double step() const { return step_; }
        const std::map<std::string, std::string> &meta() const { return meta_; }

        long fallbacksDegraded() const { return fb_degraded_; }
        long fallbacksClamped()  const { return fb_clamped_; }
        void resetFallbacks() { fb_degraded_ = fb_clamped_ = 0; }

        Outside outside() const { return outside_; }
        double outsideLimit() const { return outside_limit_; }
        void setOutside(Outside o) { outside_ = o; }
        void setOutsideLimit(double v) { outside_limit_ = v; }

        // A per-thread evaluator bound to this model's basis.
        std::shared_ptr<Evaluator> makeEvaluator() const;

    private:
        CPSF();

        std::vector<int> neighbours(int a0, int e0, int stencil) const;
        bool degrade(int a0, int e0, int stencil, int degree,
                     std::vector<int> &nb, int &stOut, int &dgOut) const;
        FieldQuery nodeQuery(int node) const;

        std::shared_ptr<Basis> basis_;
        std::shared_ptr<CoeffStore> store_;
        mutable std::shared_ptr<Evaluator> evaluator_;

        int n_nodes_, n_params_;
        double step_;
        std::vector<double> xfp_deg_, yfp_deg_, cy_, cx_, xmm_, ymm_;
        double xmin_, ymin_;
        int na_, ne_;
        std::vector<int> grid_;              // na_ * ne_ node index or -1
        std::map<std::string, std::string> meta_;

        Outside outside_;
        double outside_limit_;
        mutable long fb_degraded_, fb_clamped_;
};

// ---------------------------------------------------------------------------
// Small helpers, the C++ counterparts of cpsfs.photometry and cpsfs.energy.
// ---------------------------------------------------------------------------

// Sum each 64 x 64 block of fine samples into one camera pixel.
std::array<float, NCAM * NCAM> binToCamera(const std::vector<float> &fine);

// Sums over every npix x npix block of camera pixels, indexed by the
// lower-left pixel.  Returns an (NCAM-npix+1)^2 row-major array.
std::vector<double> blockSums(const std::array<float, NCAM * NCAM> &cam,
                              int npix);

// ---------------------------------------------------------------------------
// Pixel-integrated camera image straight from the OTF.
//
// A simulator that only needs the PSF at camera-pixel resolution does not need
// the 512 x 512 inverse transform at all.  Writing B for the Dirichlet kernel
// of the pixel integration, the npix x npix pixel-integrated image of a PSF
// whose OTF is F, placed at (cy, cx) fine samples, is exactly
//
//   cam[i][j] = (1/NFINE^2) * Re sum_{a,b} G[a][b] exp(+2 pi i (a i + b j)/npix)
//   G[a][b]   = sum_{k = a mod npix} sum_{l = b mod npix}
//                   F[k][l] B[k] B[l] exp(-2 pi i (k cy + l cx)/NFINE)
//
// i.e. multiply by the pixel-integration kernel and the placement phase, fold
// the 512 x 512 frequency grid onto npix x npix by residue class, and take an
// npix x npix inverse DFT.  Agreement with the full path was measured at
// 5.6e-17 of peak, with the sum exactly 1.
//
// Two things ride along for free, because both are multiplicative on the OTF:
//
//   * charge diffusion, as the exact continuous Gaussian factor
//     exp(-2 pi^2 sigma^2 (k^2 + l^2) / NFINE^2) with sigma in fine samples.
//     Measured against a real-space Gaussian convolution of the same PSF:
//     1.1e-16 of peak once the real-space kernel is given adequate support.
//
//   * the field-position dependence, since everything above except the phase
//     depends only on the field position.  setField() bakes that part once;
//     project() then costs an npix x npix DFT over the sparse support (about
//     26 per cent of the grid is non-zero), which is roughly 70 times cheaper
//     than reconstructing the OTF again.
//
// The quarter-turn argument of project() carries the CCD orientation.  On a
// square array a multiple of 90 degrees is an index permutation, so unlike
// ArrayOperations::rotateArray -- which falls back to bilinear interpolation
// for any angle that is not exactly a multiple of pi/2 -- this costs no
// accuracy.  The decentre is rotated with it so that the requested offset is
// the offset in DETECTOR coordinates.
// ---------------------------------------------------------------------------

class CameraProjector
{
    public:
        // `npix` is the footprint in whole camera pixels (8 for the delivered
        // ray traces).  `diffusion_px` is the Gaussian charge-diffusion sigma
        // in CAMERA PIXELS, matching PlatoSim's ChargeDiffusionStrength; pass
        // 0 to disable.
        CameraProjector(const std::shared_ptr<Basis> &basis, int npix,
                        double diffusion_px);

        // Bake the OTF of one field position, including the pixel-integration
        // and charge-diffusion factors.  `ev` is used only during this call.
        void setField(Evaluator &ev, const FieldQuery &query);

        // Project the baked field position at a sub-pixel decentre, given in
        // fine samples of the 512 grid (64 per camera pixel) and in DETECTOR
        // coordinates.  `quarterTurns` rotates by that many multiples of 90
        // degrees counter-clockwise.  `out` is npix*npix floats, row-major.
        //
        // Negative pixels are clipped and the result renormalised to sum 1.
        // On every case measured the clip is a no-op -- the faintest camera
        // pixel sat at +4e-6 of peak -- but it guarantees the invariant that
        // downstream PlatoSim stages depend on.
        void project(double dy, double dx, int quarterTurns, float *out) const;

        int npix() const { return npix_; }
        // Number of non-zero OTF entries retained by the last setField().
        std::size_t support() const { return val_.size(); }

    private:
        int npix_;
        double diffusion_px_;
        std::shared_ptr<Basis> basis_;

        // Sparse support of the baked OTF.  `ky_`/`kx_` are raw frequency
        // indices into the 512-point axes, used to look up the separable
        // placement phase; `ra_`/`rb_` are their residue classes mod npix,
        // precomputed because they do not change between projections.
        std::vector<int> ky_, kx_, ra_, rb_;
        std::vector<cdouble> val_;
        double cy_, cx_;                  // centroid of the baked field point

        // Per-axis placement phase, rebuilt once per projection.
        mutable std::vector<cdouble> py_, px_;
};

}  // namespace cpsfs

#endif  // CPSFS_H
