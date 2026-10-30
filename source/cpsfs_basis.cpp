// cpsfs_basis.cpp -- the multiresolution OTF lattice, the cubic-spline
// resampling operator, and the coefficient-to-PSF evaluator.

#include "cpsfs.h"

#include <cmath>
#include <cstring>
#include <sstream>

#ifdef CPSFS_FFT_DOUBLE
#include <fftw3.h>
#define CPSFS_FFTW_REAL        double
#define CPSFS_FFTW_COMPLEX     fftw_complex
#define CPSFS_FFTW_PLAN        fftw_plan
#define CPSFS_FFTW_ALLOC       fftw_alloc_complex
#define CPSFS_FFTW_FREE        fftw_free
#define CPSFS_FFTW_PLAN_DFT_2D fftw_plan_dft_2d
#define CPSFS_FFTW_EXECUTE     fftw_execute
#define CPSFS_FFTW_DESTROY     fftw_destroy_plan
#else
#include <fftw3.h>
#define CPSFS_FFTW_REAL        float
#define CPSFS_FFTW_COMPLEX     fftwf_complex
#define CPSFS_FFTW_PLAN        fftwf_plan
#define CPSFS_FFTW_ALLOC       fftwf_alloc_complex
#define CPSFS_FFTW_FREE        fftwf_free
#define CPSFS_FFTW_PLAN_DFT_2D fftwf_plan_dft_2d
#define CPSFS_FFTW_EXECUTE     fftwf_execute
#define CPSFS_FFTW_DESTROY     fftwf_destroy_plan
#endif

namespace cpsfs {

namespace {

const int NPAD = 12;        // SciPy's pre-padding depth for mode="nearest"
const double TWO_PI = 6.283185307179586476925286766559;

// FFT frequency index: fftfreq(NFINE) * NFINE.
inline int freqOf(int i) { return (i < NFINE / 2) ? i : i - NFINE; }

// Non-negative remainder, matching Python's % on negative operands.
inline int posmod(int a, int m) { int r = a % m; return (r < 0) ? r + m : r; }

// Wrap a signed frequency to an array index.  NFINE is a power of two.
inline int wrapIndex(int k) { return k & (NFINE - 1); }

}  // namespace

// ===========================================================================
// Basis
// ===========================================================================

Basis::Basis(double rho, double rmax, int smax)
    : rho_(rho), rmax_(rmax), smax_(smax), n_params_(0)
{
    if (!(rho_ > 0.0) || !(rmax_ > 0.0) || smax_ < 1)
    {
        throw std::invalid_argument("cpsfs::Basis: rho, rmax must be positive "
                                    "and smax at least 1");
    }

    // Frequency radius on the 512 x 512 grid, FFT order.  The delivered
    // products use the default radial zone ordering, for which the Python's
    // `Q` and `_R` are the same array.
    Q_.resize(static_cast<size_t>(NFINE) * NFINE);
    for (int i = 0; i < NFINE; ++i)
    {
        const double ky = freqOf(i);
        for (int j = 0; j < NFINE; ++j)
        {
            const double kx = freqOf(j);
            Q_[static_cast<size_t>(i) * NFINE + j] = std::sqrt(ky * ky + kx * kx);
        }
    }

    // Zones, finest first: step s doubles until smax, the outermost zone
    // always reaching rmax.
    {
        double r0 = 0.0;
        int s = 1;
        while (r0 < rmax_)
        {
            double r1 = std::min(rmax_, rho_ * s);
            if (s == smax_) r1 = rmax_;
            Zone z; z.a = r0; z.b = r1; z.s = s;
            zones_.push_back(z);
            r0 = r1;
            s = std::min(smax_, s * 2);
        }
    }

    // Which lattice points each zone owns, and where each lands in the
    // coefficient vector.
    slot_.resize(zones_.size());
    sizes_.resize(zones_.size());
    for (size_t z = 0; z < zones_.size(); ++z)
    {
        const int s = zones_[z].s;
        const double a = zones_[z].a, b = zones_[z].b;
        slot_[z].assign(static_cast<size_t>(NFINE) * NFINE, -1);
        int count = 0;
        for (int i = 0; i < NFINE; ++i)
        {
            if (posmod(freqOf(i), s) != 0) continue;
            for (int j = 0; j < NFINE; ++j)
            {
                if (posmod(freqOf(j), s) != 0) continue;
                const size_t f = static_cast<size_t>(i) * NFINE + j;
                const double q = Q_[f];
                if (q >= a && q < b && q < rmax_) slot_[z][f] = count++;
            }
        }
        sizes_[z] = count;
        n_params_ += count;
    }
}

// ===========================================================================
// SplinePlan
// ===========================================================================

SplinePlan::SplinePlan(int s)
    : s_(s), n_(NFINE / s), npadded_(NFINE / s + 2 * NPAD)
{
    if (s < 1 || NFINE % s != 0)
        throw std::invalid_argument("cpsfs::SplinePlan: step must divide 512");

    // Target coordinates: the Python computes tgt = (kk - ax[0]) / s with
    // ax[0] = -(NFINE/2).  The padding shifts them by NPAD.
    const double ax0 = -(NFINE / 2);
    i0_.resize(NFINE);
    w_.resize(NFINE);
    for (int k = 0; k < NFINE; ++k)
    {
        const double t = (freqOf(k) - ax0) / static_cast<double>(s) + NPAD;
        const double fl = std::floor(t);
        const double f = t - fl;
        i0_[k] = static_cast<int>(fl) - 1;
        const double f2 = f * f, f3 = f2 * f;
        w_[k][0] = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
        w_[k][1] = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
        w_[k][2] = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
        w_[k][3] = f3 / 6.0;
    }

    // Thomas elimination for the tridiagonal prefilter.  The kernel is
    // [1,4,1]/6 and the half-sample-symmetric boundary (c[-1] = c[0],
    // c[N] = c[N-1]) adds 1/6 to the first and last diagonal entries.
    const double off = 1.0 / 6.0;
    const int N = npadded_;
    cp_.resize(N);
    inv_.resize(N);
    double diag = 4.0 / 6.0 + off;             // first row
    inv_[0] = 1.0 / diag;
    cp_[0] = off * inv_[0];
    for (int i = 1; i < N; ++i)
    {
        diag = (i == N - 1) ? (4.0 / 6.0 + off) : (4.0 / 6.0);
        const double m = diag - off * cp_[i - 1];
        inv_[i] = 1.0 / m;
        cp_[i] = off * inv_[i];
    }
}

void SplinePlan::prefilterRows(cdouble *a, int nrows) const
{
    const double off = 1.0 / 6.0;
    const int N = npadded_;
    for (int r = 0; r < nrows; ++r)
    {
        cdouble *row = a + static_cast<size_t>(r) * N;
        // forward sweep
        row[0] *= inv_[0];
        for (int i = 1; i < N; ++i)
            row[i] = (row[i] - off * row[i - 1]) * inv_[i];
        // back substitution
        for (int i = N - 2; i >= 0; --i)
            row[i] -= cp_[i] * row[i + 1];
    }
}

void SplinePlan::evalRows(const cdouble *a, int nrows, cdouble *out) const
{
    const int N = npadded_;
    for (int r = 0; r < nrows; ++r)
    {
        const cdouble *row = a + static_cast<size_t>(r) * N;
        cdouble *dst = out + static_cast<size_t>(r) * NFINE;
        for (int k = 0; k < NFINE; ++k)
        {
            const std::array<double, 4> &w = w_[k];
            const int i0 = i0_[k];
            cdouble acc(0.0, 0.0);
            for (int t = 0; t < 4; ++t)
            {
                int idx = i0 + t;
                if (idx < 0) idx = 0;
                else if (idx >= N) idx = N - 1;
                acc += w[t] * row[idx];
            }
            dst[k] = acc;
        }
    }
}

void SplinePlan::apply(const cdouble *in, cdouble *out,
                       std::vector<cdouble> &scratchA,
                       std::vector<cdouble> &scratchB) const
{
    const int N = npadded_;
    scratchA.assign(static_cast<size_t>(N) * N, cdouble(0.0, 0.0));
    scratchB.resize(static_cast<size_t>(N) * NFINE);

    // Place the lattice values and replicate-pad both axes, which is what
    // numpy.pad(..., mode="edge") does, corners included.
    for (int p = 0; p < n_; ++p)
    {
        cdouble *dst = &scratchA[static_cast<size_t>(p + NPAD) * N];
        const cdouble *src = in + static_cast<size_t>(p) * n_;
        for (int q = 0; q < n_; ++q) dst[q + NPAD] = src[q];
        for (int q = 0; q < NPAD; ++q) dst[q] = dst[NPAD];
        for (int q = NPAD + n_; q < N; ++q) dst[q] = dst[NPAD + n_ - 1];
    }
    for (int p = 0; p < NPAD; ++p)
        std::memcpy(&scratchA[static_cast<size_t>(p) * N],
                    &scratchA[static_cast<size_t>(NPAD) * N],
                    sizeof(cdouble) * N);
    for (int p = NPAD + n_; p < N; ++p)
        std::memcpy(&scratchA[static_cast<size_t>(p) * N],
                    &scratchA[static_cast<size_t>(NPAD + n_ - 1) * N],
                    sizeof(cdouble) * N);

    // Prefilter along the fast axis, then along the slow axis.  The two
    // one-dimensional filters commute, so the order does not matter.
    prefilterRows(&scratchA[0], N);
    for (int q = 0; q < N; ++q)          // gather a column, filter, scatter
    {
        std::vector<cdouble> col(N);
        for (int p = 0; p < N; ++p) col[p] = scratchA[static_cast<size_t>(p) * N + q];
        prefilterRows(&col[0], 1);
        for (int p = 0; p < N; ++p) scratchA[static_cast<size_t>(p) * N + q] = col[p];
    }

    // Evaluate along the fast axis: (N x N) -> (N x NFINE).
    evalRows(&scratchA[0], N, &scratchB[0]);

    // Evaluate along the slow axis: (N x NFINE) -> (NFINE x NFINE).
    for (int i = 0; i < NFINE; ++i)
    {
        const std::array<double, 4> &w = w_[i];
        const int i0 = i0_[i];
        int t0 = i0, t1 = i0 + 1, t2 = i0 + 2, t3 = i0 + 3;
        if (t0 < 0) t0 = 0; else if (t0 >= N) t0 = N - 1;
        if (t1 < 0) t1 = 0; else if (t1 >= N) t1 = N - 1;
        if (t2 < 0) t2 = 0; else if (t2 >= N) t2 = N - 1;
        if (t3 < 0) t3 = 0; else if (t3 >= N) t3 = N - 1;
        const cdouble *r0 = &scratchB[static_cast<size_t>(t0) * NFINE];
        const cdouble *r1 = &scratchB[static_cast<size_t>(t1) * NFINE];
        const cdouble *r2 = &scratchB[static_cast<size_t>(t2) * NFINE];
        const cdouble *r3 = &scratchB[static_cast<size_t>(t3) * NFINE];
        cdouble *dst = out + static_cast<size_t>(i) * NFINE;
        for (int j = 0; j < NFINE; ++j)
            dst[j] = w[0] * r0[j] + w[1] * r1[j] + w[2] * r2[j] + w[3] * r3[j];
    }
}

std::vector<double> SplinePlan::matrix() const
{
    std::vector<double> S(static_cast<size_t>(NFINE) * n_, 0.0);
    std::vector<cdouble> line(npadded_);
    for (int j = 0; j < n_; ++j)
    {
        std::fill(line.begin(), line.end(), cdouble(0.0, 0.0));
        line[j + NPAD] = cdouble(1.0, 0.0);
        for (int q = 0; q < NPAD; ++q) line[q] = line[NPAD];
        for (int q = NPAD + n_; q < npadded_; ++q) line[q] = line[NPAD + n_ - 1];
        prefilterRows(&line[0], 1);
        std::vector<cdouble> ev(NFINE);
        evalRows(&line[0], 1, &ev[0]);
        for (int k = 0; k < NFINE; ++k)
            S[static_cast<size_t>(k) * n_ + j] = ev[k].real();
    }
    return S;
}

// ===========================================================================
// Evaluator
// ===========================================================================

struct Evaluator::FFT
{
    FFT()
    {
        buf = CPSFS_FFTW_ALLOC(static_cast<size_t>(NFINE) * NFINE);
        if (!buf) throw std::runtime_error("cpsfs: FFTW allocation failed");
        plan = CPSFS_FFTW_PLAN_DFT_2D(NFINE, NFINE, buf, buf,
                                      FFTW_BACKWARD, FFTW_ESTIMATE);
        if (!plan)
        {
            CPSFS_FFTW_FREE(buf);
            throw std::runtime_error("cpsfs: FFTW planning failed");
        }
    }
    ~FFT()
    {
        CPSFS_FFTW_DESTROY(plan);
        CPSFS_FFTW_FREE(buf);
    }
    CPSFS_FFTW_COMPLEX *buf;
    CPSFS_FFTW_PLAN plan;
};

Evaluator::Evaluator(const std::shared_ptr<Basis> &basis) : basis_(basis)
{
    const size_t nn = static_cast<size_t>(NFINE) * NFINE;
    fhat_.resize(nn);
    fz_a_.resize(nn);
    fz_b_.resize(nn);
    for (size_t z = 0; z < basis_->zones().size(); ++z)
    {
        const int s = basis_->zones()[z].s;
        if (s > 1 && plans_.find(s) == plans_.end())
            plans_[s] = std::shared_ptr<SplinePlan>(new SplinePlan(s));
    }
    fft_.reset(new FFT());
}

Evaluator::~Evaluator() {}

void Evaluator::zoneGrid(int z, const std::vector<cdouble> &coeff,
                         const std::vector<cdouble> *fprev,
                         std::vector<cdouble> &g)
{
    const std::vector<Basis::Zone> &zones = basis_->zones();
    const int s = zones[z].s;
    const int n = NFINE / s;
    g.assign(static_cast<size_t>(n) * n, cdouble(0.0, 0.0));
    std::vector<char> filled(static_cast<size_t>(n) * n, 0);

    // Offsets of each zone's slice inside the coefficient vector.
    std::vector<int> off(zones.size(), 0);
    for (size_t k = 1; k < zones.size(); ++k)
        off[k] = off[k - 1] + basis_->sizes()[k - 1];

    const double rmax = basis_->rmax();
    for (int zz = 0; zz <= z; ++zz)          // this zone and all finer ones
    {
        const int ss = zones[zz].s;
        if (s % ss) continue;
        const double a = zones[zz].a, b = zones[zz].b;
        for (int i = 0; i < n; ++i)
        {
            const int ay = -(NFINE / 2) + i * s;
            if (posmod(ay, ss) != 0) continue;
            const int iy = wrapIndex(ay);
            for (int j = 0; j < n; ++j)
            {
                const int ax = -(NFINE / 2) + j * s;
                if (posmod(ax, ss) != 0) continue;
                const int ix = wrapIndex(ax);
                const double q = basis_->Q_[static_cast<size_t>(iy) * NFINE + ix];
                const double rs = std::sqrt(static_cast<double>(ay) * ay +
                                            static_cast<double>(ax) * ax);
                if (!(q >= a && q < b && rs < rmax)) continue;
                const int slot = basis_->slot_[zz][static_cast<size_t>(iy) * NFINE + ix];
                g[static_cast<size_t>(i) * n + j] =
                    (slot >= 0) ? coeff[off[zz] + slot] : cdouble(0.0, 0.0);
                filled[static_cast<size_t>(i) * n + j] = 1;
            }
        }
    }

    // Lattice points this zone does not own take the coarser zone's
    // already-interpolated value.
    if (fprev)
    {
        for (int i = 0; i < n; ++i)
        {
            const int iy = wrapIndex(-(NFINE / 2) + i * s);
            for (int j = 0; j < n; ++j)
            {
                if (filled[static_cast<size_t>(i) * n + j]) continue;
                const int ix = wrapIndex(-(NFINE / 2) + j * s);
                g[static_cast<size_t>(i) * n + j] =
                    (*fprev)[static_cast<size_t>(iy) * NFINE + ix];
            }
        }
    }
}

const std::vector<cdouble> &Evaluator::reconstruct(const std::vector<cdouble> &coeff)
{
    if (static_cast<int>(coeff.size()) != basis_->nParams())
    {
        std::ostringstream os;
        os << "cpsfs::Evaluator::reconstruct: got " << coeff.size()
           << " coefficients, basis has " << basis_->nParams();
        throw std::invalid_argument(os.str());
    }

    const size_t nn = static_cast<size_t>(NFINE) * NFINE;
    std::fill(fhat_.begin(), fhat_.end(), cdouble(0.0, 0.0));

    const std::vector<Basis::Zone> &zones = basis_->zones();
    const std::vector<cdouble> *fprev = 0;
    for (int z = static_cast<int>(zones.size()) - 1; z >= 0; --z)
    {
        const int s = zones[z].s;
        const double b = zones[z].b;
        zoneGrid(z, coeff, fprev, g_);

        std::vector<cdouble> &cur = (z % 2 == 0) ? fz_a_ : fz_b_;
        if (s == 1)
        {
            std::fill(cur.begin(), cur.end(), cdouble(0.0, 0.0));
            for (int i = 0; i < NFINE; ++i)
            {
                const int iy = wrapIndex(-(NFINE / 2) + i);
                for (int j = 0; j < NFINE; ++j)
                {
                    const int ix = wrapIndex(-(NFINE / 2) + j);
                    cur[static_cast<size_t>(iy) * NFINE + ix] =
                        g_[static_cast<size_t>(i) * NFINE + j];
                }
            }
        }
        else
        {
            plans_[s]->apply(&g_[0], &cur[0], scratchA_, scratchB_);
        }

        for (size_t f = 0; f < nn; ++f)
            if (basis_->Q_[f] < b) fhat_[f] = cur[f];
        fprev = &cur;
    }

    const double rmax = basis_->rmax();
    for (size_t f = 0; f < nn; ++f)
        if (basis_->Q_[f] >= rmax) fhat_[f] = cdouble(0.0, 0.0);
    fhat_[0] = cdouble(1.0, 0.0);     // unit integral, imposed not fitted
    return fhat_;
}

template <typename T>
void Evaluator::psfInto(const std::vector<cdouble> &coeff, double cy, double cx,
                        bool nonneg, std::vector<T> &out)
{
    const std::vector<cdouble> &F = reconstruct(coeff);

    // Phase ramp, factorised across the two axes.
    std::vector<cdouble> ey(NFINE), ex(NFINE);
    for (int i = 0; i < NFINE; ++i)
    {
        const double py = -TWO_PI * freqOf(i) * cy / NFINE;
        const double px = -TWO_PI * freqOf(i) * cx / NFINE;
        ey[i] = cdouble(std::cos(py), std::sin(py));
        ex[i] = cdouble(std::cos(px), std::sin(px));
    }

    CPSFS_FFTW_COMPLEX *buf = fft_->buf;
    for (int i = 0; i < NFINE; ++i)
    {
        const cdouble a = ey[i];
        for (int j = 0; j < NFINE; ++j)
        {
            const size_t f = static_cast<size_t>(i) * NFINE + j;
            const cdouble v = F[f] * a * ex[j];
            buf[f][0] = static_cast<CPSFS_FFTW_REAL>(v.real());
            buf[f][1] = static_cast<CPSFS_FFTW_REAL>(v.imag());
        }
    }
    CPSFS_FFTW_EXECUTE(fft_->plan);

    const size_t nn = static_cast<size_t>(NFINE) * NFINE;
    const double scale = 1.0 / static_cast<double>(nn);
    out.resize(nn);
    if (!nonneg)
    {
        for (size_t f = 0; f < nn; ++f)
            out[f] = static_cast<T>(buf[f][0] * scale);
        return;
    }
    // Clip the small negative wing values and renormalise, so that the
    // delivered PSF is non-negative and still sums to one.  The sum is
    // accumulated in double whatever the output type.
    double sum = 0.0;
    for (size_t f = 0; f < nn; ++f)
    {
        const double v = buf[f][0] * scale;
        const double c = (v > 0.0) ? v : 0.0;
        out[f] = static_cast<T>(c);
        sum += c;
    }
    const T inv = static_cast<T>(1.0 / sum);
    for (size_t f = 0; f < nn; ++f) out[f] *= inv;
}

void Evaluator::psf(const std::vector<cdouble> &coeff, double cy, double cx,
                    bool nonneg, std::vector<float> &out)
{
    psfInto(coeff, cy, cx, nonneg, out);
}

void Evaluator::psf(const std::vector<cdouble> &coeff, double cy, double cx,
                    bool nonneg, std::vector<double> &out)
{
    psfInto(coeff, cy, cx, nonneg, out);
}

// ===========================================================================
// Binning helpers
// ===========================================================================

std::array<float, NCAM * NCAM> binToCamera(const std::vector<float> &fine)
{
    if (fine.size() != static_cast<size_t>(NFINE) * NFINE)
        throw std::invalid_argument("cpsfs::binToCamera: expected 512x512");
    std::array<float, NCAM * NCAM> cam;
    for (int ci = 0; ci < NCAM; ++ci)
    {
        for (int cj = 0; cj < NCAM; ++cj)
        {
            double acc = 0.0;
            for (int i = 0; i < OVERSAMPLE; ++i)
            {
                const float *row = &fine[(static_cast<size_t>(ci) * OVERSAMPLE + i) *
                                         NFINE + static_cast<size_t>(cj) * OVERSAMPLE];
                for (int j = 0; j < OVERSAMPLE; ++j) acc += row[j];
            }
            cam[static_cast<size_t>(ci) * NCAM + cj] = static_cast<float>(acc);
        }
    }
    return cam;
}

std::vector<double> blockSums(const std::array<float, NCAM * NCAM> &cam, int npix)
{
    if (npix < 1 || npix > NCAM)
        throw std::invalid_argument("cpsfs::blockSums: npix outside 1..8");
    const int m = NCAM - npix + 1;
    std::vector<double> out(static_cast<size_t>(m) * m, 0.0);
    for (int i = 0; i < m; ++i)
    {
        for (int j = 0; j < m; ++j)
        {
            double acc = 0.0;
            for (int p = 0; p < npix; ++p)
                for (int q = 0; q < npix; ++q)
                    acc += cam[static_cast<size_t>(i + p) * NCAM + (j + q)];
            out[static_cast<size_t>(i) * m + j] = acc;
        }
    }
    return out;
}

}  // namespace cpsfs
