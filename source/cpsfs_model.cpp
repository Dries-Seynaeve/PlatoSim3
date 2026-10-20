// cpsfs_model.cpp -- reading a coefficient file and interpolating it over
// the field.

#include "cpsfs.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>

#include <H5Cpp.h>
#include <armadillo>

namespace cpsfs {

namespace {

// Attribute readers that cope with the types h5py writes.
std::string attrAsString(const H5::Attribute &a)
{
    const H5T_class_t cls = a.getTypeClass();
    if (cls == H5T_STRING)
    {
        std::string s;
        a.read(a.getStrType(), s);
        return s;
    }
    if (cls == H5T_FLOAT)
    {
        double v = 0.0;
        a.read(H5::PredType::NATIVE_DOUBLE, &v);
        std::ostringstream os; os << v; return os.str();
    }
    if (cls == H5T_INTEGER)
    {
        long long v = 0;
        a.read(H5::PredType::NATIVE_LLONG, &v);
        std::ostringstream os; os << v; return os.str();
    }
    return std::string();
}

double attrAsDouble(const H5::H5Object &obj, const std::string &name)
{
    H5::Attribute a = obj.openAttribute(name);
    const H5T_class_t cls = a.getTypeClass();
    if (cls == H5T_FLOAT)
    {
        double v = 0.0;
        a.read(H5::PredType::NATIVE_DOUBLE, &v);
        return v;
    }
    if (cls == H5T_INTEGER)
    {
        long long v = 0;
        a.read(H5::PredType::NATIVE_LLONG, &v);
        return static_cast<double>(v);
    }
    throw std::runtime_error("cpsfs: attribute '" + name + "' is not numeric");
}

std::vector<double> readDoubleDataset(H5::H5File &f, const std::string &name,
                                      size_t expected)
{
    H5::DataSet d = f.openDataSet(name);
    H5::DataSpace sp = d.getSpace();
    hsize_t dims[2] = {0, 0};
    const int nd = sp.getSimpleExtentNdims();
    sp.getSimpleExtentDims(dims);
    size_t total = dims[0] * ((nd > 1) ? dims[1] : 1);
    if (expected && total != expected)
    {
        std::ostringstream os;
        os << "cpsfs: dataset '" << name << "' has " << total
           << " elements, expected " << expected;
        throw std::runtime_error(os.str());
    }
    std::vector<double> out(total);
    try
    {
        d.read(&out[0], H5::PredType::NATIVE_DOUBLE);
    }
    catch (const H5::Exception &error)
    {
        throw std::runtime_error("cpsfs: HDF5 failed while reading dataset '"
                                 + name + "' -- in " + error.getFuncName()
                                 + ": " + error.getDetailMsg());
    }
    return out;
}

// The compound layout h5py uses for complex64: two float members, r and i.
H5::CompType complexFloatType()
{
    H5::CompType t(sizeof(cfloat));
    t.insertMember("r", 0, H5::PredType::NATIVE_FLOAT);
    t.insertMember("i", sizeof(float), H5::PredType::NATIVE_FLOAT);
    return t;
}


// Every filter a dataset was written with has to be available in the HDF5
// library doing the reading, or the data cannot be decoded.  The coefficient
// tables are written with gzip (deflate), which is optional in an HDF5 build.
// When it is missing, the file opens, the dataset opens, its metadata reads
// back correctly, and only the call that fetches the data fails -- as an
// H5::DataSetIException with no indication of the cause.  Checking up front
// turns that into a message that names the filter.
void requireFiltersAvailable(const H5::DataSet &d, const std::string &dataset,
                             const std::string &path)
{
    const hid_t plist = H5Dget_create_plist(d.getId());
    if (plist < 0)
    {
        return;                 // nothing we can usefully say
    }

    const int nfilters = H5Pget_nfilters(plist);
    for (int i = 0; i < nfilters; i++)
    {
        unsigned int flags = 0, filter_config = 0;
        size_t cd_nelmts = 0;
        char name[128];
        name[0] = '\0';
        const H5Z_filter_t id = H5Pget_filter2(plist, unsigned(i), &flags,
                                               &cd_nelmts, NULL,
                                               sizeof(name), name,
                                               &filter_config);
        if (id < 0)
        {
            continue;
        }
        if (H5Zfilter_avail(id) <= 0)
        {
            H5Pclose(plist);
            std::ostringstream os;
            os << "cpsfs: dataset '" << dataset << "' in " << path
               << " was written with HDF5 filter " << int(id);
            if (name[0] != '\0')
            {
                os << " (" << name << ")";
            }
            os << ", which is not available in this HDF5 build, so the data"
                  " cannot be decoded. Filter 1 is gzip/deflate: an HDF5 built"
                  " without zlib cannot read these tables. Either rebuild HDF5"
                  " with zlib support, or rewrite the coefficient file"
                  " uncompressed.";
            throw std::runtime_error(os.str());
        }
    }
    H5Pclose(plist);
}

}  // namespace

// ===========================================================================
// CoeffStore -- the 4224 x 23525 complex64 table
// ===========================================================================

class CoeffStore
{
    public:
        CoeffStore(const std::string &path, bool preload)
            : file_(path, H5F_ACC_RDONLY), preload_(preload), path_(path)
        {
            try
            {
                dset_ = file_.openDataSet("coeff");
                H5::DataSpace sp = dset_.getSpace();
                if (sp.getSimpleExtentNdims() != 2)
                    throw std::runtime_error("cpsfs: 'coeff' is not 2-D");
                hsize_t dims[2];
                sp.getSimpleExtentDims(dims);
                nrows_ = static_cast<int>(dims[0]);
                ncols_ = static_cast<int>(dims[1]);

                requireFiltersAvailable(dset_, "coeff", path_);

                if (preload_)
                {
                    all_.resize(static_cast<size_t>(nrows_) * ncols_);

                    // Read in blocks of whole chunk rows rather than in one
                    // call.  The table is about 760 MiB; a single read asks
                    // HDF5 to convert all of it in one operation, whereas
                    // blocks keep the library's working set small and make any
                    // failure report the row range it happened on.
                    const int block = 264;          // 8 chunk rows of 33
                    for (int row0 = 0; row0 < nrows_; row0 += block)
                    {
                        const int nrows = std::min(block, nrows_ - row0);
                        readRowBlock(row0, nrows,
                                     &all_[static_cast<size_t>(row0) * ncols_]);
                    }
                }
            }
            catch (const H5::Exception &error)
            {
                throw std::runtime_error(hdf5Message(error, "coeff"));
            }
        }

        int nrows() const { return nrows_; }
        int ncols() const { return ncols_; }

        // Coefficients of one node.  With preload this is a pointer into the
        // resident table and is safe to call concurrently; without it, the
        // row is read through a hyperslab and cached, and the call is
        // serialised by a mutex (HDF5 itself is not thread-safe).
        const cfloat *row(int i) const
        {
            if (i < 0 || i >= nrows_)
                throw std::out_of_range("cpsfs: node index out of range");
            if (preload_) return &all_[static_cast<size_t>(i) * ncols_];

            std::lock_guard<std::mutex> lock(mutex_);
            std::map<int, std::vector<cfloat> >::const_iterator it = cache_.find(i);
            if (it != cache_.end()) return &it->second[0];

            std::vector<cfloat> buf(ncols_);
            try
            {
                readRowBlock(i, 1, &buf[0]);
            }
            catch (const H5::Exception &error)
            {
                throw std::runtime_error(hdf5Message(error, "coeff"));
            }
            cache_[i] = buf;
            return &cache_[i][0];
        }

    private:
        CoeffStore(const CoeffStore &);
        CoeffStore &operator=(const CoeffStore &);

        // Read `nrows` consecutive rows into `dst`, which must hold
        // nrows * ncols_ elements.
        void readRowBlock(int row0, int nrows, cfloat *dst) const
        {
            hsize_t offset[2] = {static_cast<hsize_t>(row0), 0};
            hsize_t count[2]  = {static_cast<hsize_t>(nrows),
                                 static_cast<hsize_t>(ncols_)};
            H5::DataSpace file_space = dset_.getSpace();
            file_space.selectHyperslab(H5S_SELECT_SET, count, offset);
            H5::DataSpace mem_space(2, count);
            dset_.read(dst, complexFloatType(), mem_space, file_space);
        }

        // Turn an HDF5 C++ exception into something a user can act on.  The
        // library's own exceptions carry the failing function and a detail
        // message, but neither reaches the terminal when the exception is left
        // uncaught -- it simply aborts the process.
        std::string hdf5Message(const H5::Exception &error,
                                const std::string &dataset) const
        {
            std::ostringstream os;
            os << "cpsfs: HDF5 failed while reading dataset '" << dataset
               << "' from " << path_ << " -- in " << error.getFuncName()
               << ": " << error.getDetailMsg();
            return os.str();
        }

        mutable H5::H5File file_;
        mutable H5::DataSet dset_;
        bool preload_;
        std::string path_;
        int nrows_, ncols_;
        std::vector<cfloat> all_;
        mutable std::map<int, std::vector<cfloat> > cache_;
        mutable std::mutex mutex_;
};

// ===========================================================================
// CPSF
// ===========================================================================

CPSF::CPSF()
    : n_nodes_(0), n_params_(0), step_(0.5), xmin_(0.0), ymin_(0.0),
      na_(0), ne_(0), outside_(OUTSIDE_RAISE), outside_limit_(20.0),
      fb_degraded_(0), fb_clamped_(0)
{
}

CPSF CPSF::load(const std::string &path, Outside outside,
                double outside_limit, bool preload)
{
    CPSF m;
    m.outside_ = outside;
    m.outside_limit_ = outside_limit;

    {
        // A plain readability check, deliberately not H5Fis_hdf5(): that is
        // deprecated in HDF5 2.x and prints the library's error stack before
        // any message of ours is seen.
        std::ifstream probe(path.c_str(), std::ios::binary);
        if (!probe.good())
        {
            throw std::runtime_error(
                "cpsfs: cannot open " + path + ". The coefficient file name has"
                " to resolve to a readable path; a relative name in the"
                " configuration must be made absolute by the caller.");
        }
    }

    H5::H5File f(path, H5F_ACC_RDONLY);

    for (int k = 0; k < f.getNumAttrs(); ++k)
    {
        H5::Attribute a = f.openAttribute(k);
        m.meta_[a.getName()] = attrAsString(a);
    }

    const double rho = attrAsDouble(f, "rho");
    const double rmax = attrAsDouble(f, "rmax");
    const int smax = static_cast<int>(attrAsDouble(f, "smax"));
    m.basis_.reset(new Basis(rho, rmax, smax));

    if (!f.nameExists("grid") || !H5::Group(f.openGroup("grid")).nameExists("xfp_deg"))
    {
        throw std::runtime_error(
            path + " predates the xfp/yfp grid convention. Upgrade it with "
            "cpsfs.build.upgrade_grid(coeff_path, input_path).");
    }

    m.xfp_deg_ = readDoubleDataset(f, "grid/xfp_deg", 0);
    m.n_nodes_ = static_cast<int>(m.xfp_deg_.size());
    m.yfp_deg_ = readDoubleDataset(f, "grid/yfp_deg", m.xfp_deg_.size());

    H5::Group gg = f.openGroup("grid");
    if (gg.nameExists("xfp_distorted_mm"))
    {
        m.xmm_ = readDoubleDataset(f, "grid/xfp_distorted_mm", m.xfp_deg_.size());
        m.ymm_ = readDoubleDataset(f, "grid/yfp_distorted_mm", m.xfp_deg_.size());
    }

    std::vector<double> cen = readDoubleDataset(f, "centroid",
                                                2 * m.xfp_deg_.size());
    m.cy_.resize(m.n_nodes_);
    m.cx_.resize(m.n_nodes_);
    for (int i = 0; i < m.n_nodes_; ++i)
    {
        m.cy_[i] = cen[2 * static_cast<size_t>(i)];
        m.cx_[i] = cen[2 * static_cast<size_t>(i) + 1];
    }

    m.step_ = 0.5;
    if (f.attrExists("grid_step_deg")) m.step_ = attrAsDouble(f, "grid_step_deg");
    f.close();

    m.store_.reset(new CoeffStore(path, preload));
    m.n_params_ = m.store_->ncols();
    if (m.store_->nrows() != m.n_nodes_)
        throw std::runtime_error("cpsfs: 'coeff' and 'grid' disagree on the "
                                 "number of field points");
    if (m.n_params_ != m.basis_->nParams())
    {
        std::ostringstream os;
        os << "cpsfs: the file holds " << m.n_params_ << " coefficients per "
           << "field point but the basis built from rho=" << rho << ", rmax="
           << rmax << ", smax=" << smax << " has " << m.basis_->nParams();
        throw std::runtime_error(os.str());
    }

    // Index of the regular field grid.
    m.xmin_ = *std::min_element(m.xfp_deg_.begin(), m.xfp_deg_.end());
    m.ymin_ = *std::min_element(m.yfp_deg_.begin(), m.yfp_deg_.end());
    std::vector<int> ia(m.n_nodes_), ie(m.n_nodes_);
    int amax = 0, emax = 0;
    for (int i = 0; i < m.n_nodes_; ++i)
    {
        ia[i] = static_cast<int>(std::lround((m.xfp_deg_[i] - m.xmin_) / m.step_));
        ie[i] = static_cast<int>(std::lround((m.yfp_deg_[i] - m.ymin_) / m.step_));
        amax = std::max(amax, ia[i]);
        emax = std::max(emax, ie[i]);
    }
    m.na_ = amax + 1;
    m.ne_ = emax + 1;
    m.grid_.assign(static_cast<size_t>(m.na_) * m.ne_, -1);
    for (int i = 0; i < m.n_nodes_; ++i)
        m.grid_[static_cast<size_t>(ia[i]) * m.ne_ + ie[i]] = i;

    return m;
}

std::shared_ptr<Evaluator> CPSF::makeEvaluator() const
{
    return std::shared_ptr<Evaluator>(new Evaluator(basis_));
}

int CPSF::nearestNode(double xfp_deg, double yfp_deg) const
{
    int best = -1;
    double bd = std::numeric_limits<double>::max();
    for (int i = 0; i < n_nodes_; ++i)
    {
        const double dx = xfp_deg_[i] - xfp_deg, dy = yfp_deg_[i] - yfp_deg;
        const double d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

int CPSF::nearestNodeMm(double x_mm, double y_mm) const
{
    if (xmm_.empty())
        throw std::runtime_error("cpsfs: this coefficient file carries no "
                                 "grid/xfp_distorted_mm, so focal-plane "
                                 "millimetres cannot be resolved");
    int best = -1;
    double bd = std::numeric_limits<double>::max();
    for (int i = 0; i < n_nodes_; ++i)
    {
        const double dx = xmm_[i] - x_mm, dy = ymm_[i] - y_mm;
        const double d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void CPSF::angleFromDistortedMm(double x_mm, double y_mm,
                                double &xfp_deg, double &yfp_deg,
                                int stencil, int degree, double weight) const
{
    const int k0 = nearestNodeMm(x_mm, y_mm);
    const int a0 = static_cast<int>(std::lround((xfp_deg_[k0] - xmin_) / step_));
    const int e0 = static_cast<int>(std::lround((yfp_deg_[k0] - ymin_) / step_));

    const int nterms = (degree + 1) * (degree + 2) / 2;
    std::vector<int> nb;
    for (int st = stencil; st <= 12; ++st)
    {
        nb = neighbours(a0, e0, st);
        if (static_cast<int>(nb.size()) >= nterms + 3) break;
    }
    if (static_cast<int>(nb.size()) < nterms + 3)
    {
        xfp_deg = xfp_deg_[k0];
        yfp_deg = yfp_deg_[k0];
        return;
    }

    // Scale the millimetre offsets by the grid step so that the design
    // matrix is conditioned like the one used in the field interpolation.
    const double scale = step_ * 4.3205;      // mm per grid step, nominal
    const int m = static_cast<int>(nb.size());
    std::vector<double> ddx(m), ddy(m), d(m);
    for (int k = 0; k < m; ++k)
    {
        ddx[k] = (xmm_[nb[k]] - x_mm) / scale;
        ddy[k] = (ymm_[nb[k]] - y_mm) / scale;
        d[k] = std::sqrt(ddx[k] * ddx[k] + ddy[k] * ddy[k]);
    }

    std::vector<double> sw;
    if (weight > 0.0)
    {
        const int hit = static_cast<int>(
            std::min_element(d.begin(), d.end()) - d.begin());
        if (d[hit] < 1e-12)
        {
            xfp_deg = xfp_deg_[nb[hit]];
            yfp_deg = yfp_deg_[nb[hit]];
            return;
        }
        sw.resize(m);
        for (int k = 0; k < m; ++k) sw[k] = std::pow(d[k], -0.5 * weight);
    }

    arma::mat V(m, nterms);
    for (int k = 0; k < m; ++k)
    {
        int c = 0;
        for (int p = 0; p <= degree; ++p)
        {
            const double xp = std::pow(ddx[k], p);
            for (int q = 0; q <= degree - p; ++q, ++c)
                V(k, c) = xp * std::pow(ddy[k], q);
        }
        if (!sw.empty()) V.row(k) *= sw[k];
    }
    arma::mat P;
    if (!arma::pinv(P, V))
        throw std::runtime_error("cpsfs: the distortion-inversion design "
                                 "matrix could not be pseudo-inverted");

    xfp_deg = yfp_deg = 0.0;
    for (int k = 0; k < m; ++k)
    {
        const double u = P(0, k) * (sw.empty() ? 1.0 : sw[k]);
        xfp_deg += u * xfp_deg_[nb[k]];
        yfp_deg += u * yfp_deg_[nb[k]];
    }
}

std::vector<int> CPSF::neighbours(int a0, int e0, int stencil) const
{
    std::vector<int> nb;
    // Both ends are clipped to the grid, and the stop indices are clamped
    // at zero.  That clamp matters: a negative stop would be read as
    // counting back from the end of the grid, returning a large band of
    // unrelated nodes instead of none, which is how a query well outside
    // the footprint can bypass the guards below and come back with a
    // silently extrapolated answer.  The Python had exactly that defect
    // and it is fixed there too.
    const int alo = std::max(0, a0 - stencil), ahi = std::min(na_ - 1, a0 + stencil);
    const int elo = std::max(0, e0 - stencil), ehi = std::min(ne_ - 1, e0 + stencil);
    if (alo > ahi || elo > ehi) return nb;
    for (int ii = alo; ii <= ahi; ++ii)
        for (int jj = elo; jj <= ehi; ++jj)
        {
            const int k = grid_[static_cast<size_t>(ii) * ne_ + jj];
            if (k >= 0) nb.push_back(k);
        }
    return nb;
}

bool CPSF::degrade(int a0, int e0, int stencil, int degree,
                   std::vector<int> &nb, int &stOut, int &dgOut) const
{
    const int candidates[6] = {stencil, 4, 6, 8, 10, 12};
    for (int c = 0; c < 6; ++c)
    {
        const int st = candidates[c];
        if (st < stencil) continue;
        std::vector<int> cand = neighbours(a0, e0, st);
        if (cand.empty()) continue;
        for (int dg = degree; dg >= 0; --dg)
        {
            const int nterms = (dg + 1) * (dg + 2) / 2;
            if (static_cast<int>(cand.size()) >= nterms + 3)
            {
                nb = cand; stOut = st; dgOut = dg;
                return true;
            }
        }
    }
    return false;
}

FieldQuery CPSF::nodeQuery(int node) const
{
    FieldQuery q;
    q.coeff.resize(n_params_);
    const cfloat *r = store_->row(node);
    for (int i = 0; i < n_params_; ++i)
        q.coeff[i] = cdouble(r[i].real(), r[i].imag());
    q.cy = cy_[node];
    q.cx = cx_[node];
    return q;
}

FieldQuery CPSF::atNode(int node) const { return nodeQuery(node); }

FieldQuery CPSF::coefficients_at(double xfp_deg, double yfp_deg,
                                 int stencil, int degree, double weight) const
{
    const int a0 = static_cast<int>(std::lround((xfp_deg - xmin_) / step_));
    const int e0 = static_cast<int>(std::lround((yfp_deg - ymin_) / step_));

    std::vector<int> nb = neighbours(a0, e0, stencil);
    int deg = degree;
    int nterms = (deg + 1) * (deg + 2) / 2;
    FallbackInfo fb;

    if (static_cast<int>(nb.size()) < nterms + 3)
    {
        if (outside_ == OUTSIDE_RAISE)
        {
            std::ostringstream os;
            os << "cpsfs: only " << nb.size() << " usable neighbours for degree "
               << deg << " at xfp=" << xfp_deg << ", yfp=" << yfp_deg
               << " deg. This position is outside the sampled footprint, or in "
               << "one of its cut corners. Load with OUTSIDE_NEAREST to fall "
               << "back instead of throwing -- the result is then NOT a valid "
               << "model of the optics there.";
            throw FieldError(os.str(), true, false);
        }
        if (outside_limit_ > 0.0)
        {
            const double rq = std::sqrt(xfp_deg * xfp_deg + yfp_deg * yfp_deg);
            if (rq > outside_limit_)
            {
                std::ostringstream os;
                os << "cpsfs: field radius " << rq << " deg exceeds "
                   << "outside_limit=" << outside_limit_ << " deg at xfp="
                   << xfp_deg << ", yfp=" << yfp_deg << ". The outermost "
                   << "sampled field point is at 18.5 deg, so this request is "
                   << "well outside anything the model can stand behind.";
                throw FieldError(os.str(), true, true);
            }
        }
        int st = stencil;
        if (degrade(a0, e0, stencil, degree, nb, st, deg))
        {
            nterms = (deg + 1) * (deg + 2) / 2;
            ++fb_degraded_;
            fb.kind = FallbackInfo::DEGRADED;
            fb.stencil = st;
            fb.degree = deg;
        }
        else
        {
            if (outside_ == OUTSIDE_DEGRADE)
            {
                std::ostringstream os;
                os << "cpsfs: no usable neighbours at any stencil for xfp="
                   << xfp_deg << ", yfp=" << yfp_deg << " deg; load with "
                   << "OUTSIDE_NEAREST to clamp to the nearest sampled point";
                throw FieldError(os.str(), true, false);
            }
            const int k = nearestNode(xfp_deg, yfp_deg);
            ++fb_clamped_;
            FieldQuery q = nodeQuery(k);
            q.fallback.kind = FallbackInfo::CLAMPED;
            q.fallback.distance_deg =
                std::sqrt((xfp_deg_[k] - xfp_deg) * (xfp_deg_[k] - xfp_deg) +
                          (yfp_deg_[k] - yfp_deg) * (yfp_deg_[k] - yfp_deg));
            return q;
        }
    }

    const int m = static_cast<int>(nb.size());
    std::vector<double> ddx(m), ddy(m), d(m);
    for (int k = 0; k < m; ++k)
    {
        ddx[k] = (xfp_deg_[nb[k]] - xfp_deg) / step_;
        ddy[k] = (yfp_deg_[nb[k]] - yfp_deg) / step_;
        d[k] = std::sqrt(ddx[k] * ddx[k] + ddy[k] * ddy[k]);
    }

    // Moving least squares: the inverse-distance row weighting makes the fit
    // an interpolant, exact at the nodes.  A query that lands on a node is
    // answered from the table, which is both exact and what the weighting
    // tends to in the limit.
    std::vector<double> sw;
    if (weight > 0.0)
    {
        const int hit = static_cast<int>(
            std::min_element(d.begin(), d.end()) - d.begin());
        if (d[hit] < 1e-9)
        {
            FieldQuery q = nodeQuery(nb[hit]);
            q.fallback = fb;
            return q;
        }
        sw.resize(m);
        for (int k = 0; k < m; ++k) sw[k] = std::pow(d[k], -0.5 * weight);
    }

    // Design matrix: terms dx^p dy^q ordered p = 0..deg, q = 0..deg-p,
    // which is the Python's ordering.
    arma::mat V(m, nterms);
    for (int k = 0; k < m; ++k)
    {
        int c = 0;
        for (int p = 0; p <= deg; ++p)
        {
            const double xp = std::pow(ddx[k], p);
            for (int q = 0; q <= deg - p; ++q, ++c)
                V(k, c) = xp * std::pow(ddy[k], q);
        }
        if (!sw.empty()) V.row(k) *= sw[k];
    }

    // Only the constant term of the local polynomial is wanted -- it is the
    // value at the query point -- so only the first row of the pseudo-inverse
    // is needed.  Folding the row weights back in gives one weight per node.
    arma::mat P;
    if (!arma::pinv(P, V))
        throw std::runtime_error("cpsfs: the field-interpolation design matrix "
                                 "could not be pseudo-inverted");
    std::vector<double> u(m);
    for (int k = 0; k < m; ++k)
        u[k] = P(0, k) * (sw.empty() ? 1.0 : sw[k]);

    FieldQuery out;
    out.coeff.assign(n_params_, cdouble(0.0, 0.0));
    out.cy = out.cx = 0.0;
    out.fallback = fb;
    for (int k = 0; k < m; ++k)
    {
        const double uk = u[k];
        const cfloat *r = store_->row(nb[k]);
        for (int i = 0; i < n_params_; ++i)
            out.coeff[i] += uk * cdouble(r[i].real(), r[i].imag());
        out.cy += uk * cy_[nb[k]];
        out.cx += uk * cx_[nb[k]];
    }
    return out;
}

std::vector<float> CPSF::psf_at(double xfp_deg, double yfp_deg,
                                double dy, double dx, bool nonneg) const
{
    FieldQuery q = coefficients_at(xfp_deg, yfp_deg);
    if (!evaluator_) evaluator_ = makeEvaluator();
    std::vector<float> out;
    evaluator_->psf(q.coeff, q.cy + dy, q.cx + dx, nonneg, out);
    return out;
}

std::array<float, NCAM * NCAM>
CPSF::camera_image_at(double xfp_deg, double yfp_deg,
                      double dy, double dx, bool nonneg) const
{
    return binToCamera(psf_at(xfp_deg, yfp_deg, dy, dx, nonneg));
}

double CPSF::ensquared_energy_at(double xfp_deg, double yfp_deg,
                                 double dy, double dx, int npix,
                                 int block_i, int block_j, bool nonneg) const
{
    const std::array<float, NCAM * NCAM> cam =
        camera_image_at(xfp_deg, yfp_deg, dy, dx, nonneg);
    const std::vector<double> bs = blockSums(cam, npix);
    const int m = NCAM - npix + 1;
    if (block_i < 0 || block_j < 0)
        return *std::max_element(bs.begin(), bs.end());
    if (block_i >= m || block_j >= m)
        throw std::out_of_range("cpsfs: block index outside the array");
    return bs[static_cast<size_t>(block_i) * m + block_j];
}

}  // namespace cpsfs
