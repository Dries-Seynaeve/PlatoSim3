// ---------------------------------------------------------------------------
// cpsfs_fold.cpp -- pixel-integrated camera image straight from the OTF.
//
// See the CameraProjector comment in cpsfs.h for the identity this implements
// and for the measured agreement with the full 512 x 512 path.
//
// Part of the analytic PLATO PSF representation (PLATO-KUL-PL-TN-0030).
// ---------------------------------------------------------------------------

#include "cpsfs.h"

#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

namespace cpsfs {

namespace {

const double TWO_PI = 6.283185307179586476925286766559;

// B[k] for one axis: the exact camera-pixel integration factor, the DFT of a
// box of OVERSAMPLE fine samples.  Matches cpsfs._otf.dirichlet() term by
// term, including its positive exponent and its box starting at sample 0.
void dirichletAxis(std::vector<cdouble> &b)
{
    b.assign(NFINE, cdouble(0.0, 0.0));
    for (int k = 0; k < NFINE; k++)
    {
        cdouble acc(0.0, 0.0);
        for (int j = 0; j < OVERSAMPLE; j++)
        {
            const double a = TWO_PI * double(k) * double(j) / double(NFINE);
            acc += cdouble(std::cos(a), std::sin(a));
        }
        b[k] = acc;
    }
}

// Signed frequency of a raw FFT index, i.e. numpy's fftfreq(NFINE)*NFINE.
// The sign matters: the placement phase is not invariant under k -> k + NFINE
// for a non-integer centroid.
inline double signedFreq(int k)
{
    return (k < NFINE / 2) ? double(k) : double(k - NFINE);
}

}  // namespace


CameraProjector::CameraProjector(const std::shared_ptr<Basis> &basis, int npix,
                                 double diffusion_px)
    : npix_(npix), diffusion_px_(diffusion_px), basis_(basis), cy_(0.0), cx_(0.0)
{
    if (npix <= 0 || NFINE % npix != 0)
    {
        throw std::invalid_argument(
            "CameraProjector: npix must divide NFINE exactly");
    }
    if (diffusion_px < 0.0)
    {
        throw std::invalid_argument(
            "CameraProjector: the charge-diffusion sigma cannot be negative");
    }
    py_.resize(NFINE);
    px_.resize(NFINE);
}


void CameraProjector::setField(Evaluator &ev, const FieldQuery &query)
{
    const std::vector<cdouble> &F = ev.reconstruct(query.coeff);

    std::vector<cdouble> B;
    dirichletAxis(B);

    // Charge diffusion as the exact continuous Gaussian OTF factor.  sigma is
    // supplied in camera pixels, so convert to fine samples.
    const double sig = diffusion_px_ * double(OVERSAMPLE);
    const double gfac = -2.0 * M_PI * M_PI * sig * sig / (double(NFINE) * double(NFINE));

    ky_.clear(); kx_.clear(); ra_.clear(); rb_.clear(); val_.clear();
    ky_.reserve(F.size() / 3); kx_.reserve(F.size() / 3);
    ra_.reserve(F.size() / 3); rb_.reserve(F.size() / 3);
    val_.reserve(F.size() / 3);

    for (int k = 0; k < NFINE; k++)
    {
        const double fy = signedFreq(k);
        for (int l = 0; l < NFINE; l++)
        {
            const cdouble f = F[std::size_t(k) * NFINE + l];
            if (f.real() == 0.0 && f.imag() == 0.0)
            {
                continue;                  // outside the band limit
            }
            const double fx = signedFreq(l);
            cdouble v = f * B[k] * B[l];
            if (sig > 0.0)
            {
                v *= std::exp(gfac * (fy * fy + fx * fx));
            }
            ky_.push_back(k);
            kx_.push_back(l);
            ra_.push_back(k % npix_);
            rb_.push_back(l % npix_);
            val_.push_back(v);
        }
    }

    cy_ = query.cy;
    cx_ = query.cx;
}


void CameraProjector::project(double dy, double dx, int quarterTurns,
                              float *out) const
{
    if (val_.empty())
    {
        throw std::logic_error(
            "CameraProjector::project called before setField");
    }

    // Normalise the quarter-turn count to 0..3.
    int q = quarterTurns % 4;
    if (q < 0)
    {
        q += 4;
    }

    // The image is built in the PSF frame and then rotated into the detector
    // frame, so the requested detector-frame decentre has to be carried back
    // through the inverse rotation.  numpy's rot90 (counter-clockwise) maps an
    // offset (u, v) to (-v, u); the inverse therefore maps (u, v) to (v, -u).
    double dyp = dy, dxp = dx;
    switch (q)
    {
        case 1: dyp =  dx; dxp = -dy; break;
        case 2: dyp = -dy; dxp = -dx; break;
        case 3: dyp = -dx; dxp =  dy; break;
        default: break;
    }

    const double ty = cy_ + dyp;
    const double tx = cx_ + dxp;

    // Separable placement phase, so that each retained OTF entry costs two
    // table lookups and one complex multiply rather than a transcendental.
    for (int k = 0; k < NFINE; k++)
    {
        const double ay = -TWO_PI * signedFreq(k) * ty / double(NFINE);
        const double ax = -TWO_PI * signedFreq(k) * tx / double(NFINE);
        py_[k] = cdouble(std::cos(ay), std::sin(ay));
        px_[k] = cdouble(std::cos(ax), std::sin(ax));
    }

    // Fold the 512 x 512 frequency grid onto npix x npix by residue class.
    const int n2 = npix_ * npix_;
    std::vector<cdouble> G(std::size_t(n2), cdouble(0.0, 0.0));
    const std::size_t m = val_.size();
    for (std::size_t t = 0; t < m; t++)
    {
        G[std::size_t(ra_[t]) * npix_ + rb_[t]] +=
            val_[t] * py_[ky_[t]] * px_[kx_[t]];
    }

    // npix x npix inverse DFT, evaluated directly: for npix = 8 this is 4096
    // complex multiply-accumulates, far below the cost of an FFT call.
    std::vector<double> cam(std::size_t(n2), 0.0);
    const double scale = 1.0 / (double(NFINE) * double(NFINE));
    for (int i = 0; i < npix_; i++)
    {
        for (int j = 0; j < npix_; j++)
        {
            cdouble acc(0.0, 0.0);
            for (int a = 0; a < npix_; a++)
            {
                for (int b = 0; b < npix_; b++)
                {
                    const double ang =
                        TWO_PI * (double(a * i) + double(b * j)) / double(npix_);
                    acc += G[std::size_t(a) * npix_ + b] *
                           cdouble(std::cos(ang), std::sin(ang));
                }
            }
            cam[std::size_t(i) * npix_ + j] = acc.real() * scale;
        }
    }

    // Rotate into the detector frame.  A multiple of 90 degrees on a square
    // array is an index permutation, so this is exact.
    std::vector<double> rot(std::size_t(n2), 0.0);
    for (int i = 0; i < npix_; i++)
    {
        for (int j = 0; j < npix_; j++)
        {
            int di = i, dj = j;
            switch (q)
            {
                case 1: di = npix_ - 1 - j; dj = i;             break;
                case 2: di = npix_ - 1 - i; dj = npix_ - 1 - j; break;
                case 3: di = j;             dj = npix_ - 1 - i; break;
                default: break;
            }
            rot[std::size_t(di) * npix_ + dj] = cam[std::size_t(i) * npix_ + j];
        }
    }

    // Clip and renormalise.  On every case measured the clip removes nothing,
    // but PlatoSim stages downstream of the PSF do not tolerate negative flux,
    // so the invariant is enforced rather than assumed.
    double total = 0.0;
    for (int t = 0; t < n2; t++)
    {
        if (rot[std::size_t(t)] < 0.0)
        {
            rot[std::size_t(t)] = 0.0;
        }
        total += rot[std::size_t(t)];
    }
    const double inv = (total > 0.0) ? 1.0 / total : 0.0;
    for (int t = 0; t < n2; t++)
    {
        out[t] = float(rot[std::size_t(t)] * inv);
    }
}

}  // namespace cpsfs
