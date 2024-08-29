#include <cmath>               // for fabs
#include <string.h>
#include <vector>
#include <stdio.h>
#include "utils.h"
#include <sys/time.h>

unsigned long timestamp(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) return 0;
    return (unsigned long)((unsigned long)tv.tv_sec * 1000 + (unsigned long)tv.tv_usec/1000);
}

void Chrono::print_elapsed_time(const char* prefix){
        unsigned long time = timestamp() - m_time;
		printf("%s [%i ms]\n", prefix, time);
	}

double mean(const double data[], int len) {
    double sum = 0.0, mean = 0.0;

    int i;
    for(i=0; i<len; ++i) {
        sum += data[i];
    }

    mean = sum/len;
    return mean;
}

double stddev(const double data[], int len) {
    double the_mean = mean(data, len);
    double standardDeviation = 0.0;

    int i;
    for(i=0; i<len; ++i) {
        standardDeviation += powf(data[i] - the_mean, 2.f);
    }

    return sqrt(standardDeviation/len);
}

void smoothed_z_score(const double y[], double signals[], const int count, const int lag, const float threshold, const float influence)
{
    memset(signals, 0, sizeof(double) * count);
    double filteredY[count];
    memcpy(filteredY, y, sizeof(double) * count);
    double *avgFilter = (double*)alloca(count*sizeof(double));
    double *stdFilter = (double*)alloca(count*sizeof(double));

    avgFilter[lag - 1] = mean(y, lag);
    stdFilter[lag - 1] = stddev(y, lag);

    for (int i = lag; i < count; i++) {
        if (fabs(y[i] - avgFilter[i-1]) > threshold * stdFilter[i-1]) {
            if (y[i] > avgFilter[i-1]) {
                signals[i] = 1;
            } else {
                signals[i] = -1;
            }
            filteredY[i] = influence * y[i] + (1 - influence) * filteredY[i-1];
        } else {
            signals[i] = -0;
        }
        avgFilter[i] = mean(filteredY + i-lag, lag);
        stdFilter[i] = stddev(filteredY + i-lag, lag);
    }
}

static const double TINY_FLOAT = 1.0e-300;

using double_vect = std::vector<double>;
using int_vect = std::vector<int>;

class double_mat : public std::vector<double_vect> {
private:
    //! disable the default constructor
    explicit double_mat() {};
    //! disable assignment operator until it is implemented.
    double_mat &operator =(const double_mat &) { return *this; };
public:
    //! constructor with sizes
    double_mat(const size_t rows, const size_t cols, const float def=0.0);
    //! copy constructor for matrix
    double_mat(const double_mat &m);
    //! copy constructor for vector
    double_mat(const double_vect &v);

    //! use default destructor
    // ~float_mat() {};

    //! get size
    size_t nr_rows(void) const { return size(); };
    //! get size
    size_t nr_cols(void) const { return front().size(); };
};



// constructor with sizes
double_mat::double_mat(const size_t rows,const size_t cols,const float defval)
        : std::vector<double_vect>(rows) {
    int i;
    for (i = 0; i < rows; ++i) {
        (*this)[i].resize(cols, defval);
    }
    if ((rows < 1) || (cols < 1)) {
        char buffer[1024];

    }
}

// copy constructor for matrix
double_mat::double_mat(const double_mat &m) : std::vector<double_vect>(m.size()) {

    double_mat::iterator inew = begin();
    double_mat::const_iterator iold = m.begin();
    for (/* empty */; iold < m.end(); ++inew, ++iold) {
        const size_t oldsz = iold->size();
        inew->resize(oldsz);
        const double_vect oldvec(*iold);
        *inew = oldvec;
    }
}

// copy constructor for vector
double_mat::double_mat(const double_vect &v)
        : std::vector<double_vect>(1) {

    const size_t oldsz = v.size();
    front().resize(oldsz);
    front() = v;
}

//////////////////////
// Helper functions //
//////////////////////

//! permute() orders the rows of A to match the integers in the index array.
void permute(double_mat &A, int_vect &idx)
{
    int_vect i(idx.size());
    int j,k;

    for (j = 0; j < A.nr_rows(); ++j) {
        i[j] = j;
    }

    // loop over permuted indices
    for (j = 0; j < A.nr_rows(); ++j) {
        if (i[j] != idx[j]) {

            // search only the remaining indices
            for (k = j+1; k < A.nr_rows(); ++k) {
                if (i[k] ==idx[j]) {
                    std::swap(A[j],A[k]); // swap the rows and
                    i[k] = i[j];     // the elements of
                    i[j] = idx[j];   // the ordered index.
                    break; // next j
                }
            }
        }
    }
}

/*! \brief Implicit partial pivoting.
 *
 * The function looks for pivot element only in rows below the current
 * element, A[idx[row]][column], then swaps that row with the current one in
 * the index map. The algorithm is for implicit pivoting (i.e., the pivot is
 * chosen as if the max coefficient in each row is set to 1) based on the
 * scaling information in the vector scale. The map of swapped indices is
 * recorded in swp. The return value is +1 or -1 depending on whether the
 * number of row swaps was even or odd respectively. */
static int partial_pivot(double_mat &A, const size_t row, const size_t col,
                         double_vect &scale, int_vect &idx, float tol)
{
    if (tol <= 0.0)
        tol = TINY_FLOAT;

    int swapNum = 1;

    // default pivot is the current position, [row,col]
    int pivot = row;
    double piv_elem = fabs(A[idx[row]][col]) * scale[idx[row]];

    // loop over possible pivots below current
    int j;
    for (j = row + 1; j < A.nr_rows(); ++j) {

        const double tmp = fabs(A[idx[j]][col]) * scale[idx[j]];

        // if this elem is larger, then it becomes the pivot
        if (tmp > piv_elem) {
            pivot = j;
            piv_elem = tmp;
        }
    }
#if 0
    if(piv_elem < tol) {
      //sgs_error("partial_pivot(): Zero pivot encountered.\n")
#endif

    if(pivot > row) {           // bring the pivot to the diagonal
        j = idx[row];           // reorder swap array
        idx[row] = idx[pivot];
        idx[pivot] = j;
        swapNum = -swapNum;     // keeping track of odd or even swap
    }
    return swapNum;
}

/*! \brief Perform backward substitution.
 *
 * Solves the system of equations A*b=a, ASSUMING that A is upper
 * triangular. If diag==1, then the diagonal elements are additionally
 * assumed to be 1.  Note that the lower triangular elements are never
 * checked, so this function is valid to use after a LU-decomposition in
 * place.  A is not modified, and the solution, b, is returned in a. */
static void lu_backsubst(double_mat &A, double_mat &a, bool diag=false)
{
    int r,c,k;

    for (r = (A.nr_rows() - 1); r >= 0; --r) {
        for (c = (A.nr_cols() - 1); c > r; --c) {
            for (k = 0; k < A.nr_cols(); ++k) {
                a[r][k] -= A[r][c] * a[c][k];
            }
        }
        if(!diag) {
            for (k = 0; k < A.nr_cols(); ++k) {
                a[r][k] /= A[r][r];
            }
        }
    }
}

/*! \brief Perform forward substitution.
 *
 * Solves the system of equations A*b=a, ASSUMING that A is lower
 * triangular. If diag==1, then the diagonal elements are additionally
 * assumed to be 1.  Note that the upper triangular elements are never
 * checked, so this function is valid to use after a LU-decomposition in
 * place.  A is not modified, and the solution, b, is returned in a. */
static void lu_forwsubst(double_mat &A, double_mat &a, bool diag=true)
{
    int r,k,c;
    for (r = 0;r < A.nr_rows(); ++r) {
        for(c = 0; c < r; ++c) {
            for (k = 0; k < A.nr_cols(); ++k) {
                a[r][k] -= A[r][c] * a[c][k];
            }
        }
        if(!diag) {
            for (k = 0; k < A.nr_cols(); ++k) {
                a[r][k] /= A[r][r];
            }
        }
    }
}

/*! \brief Performs LU factorization in place.
 *
 * This is Crout's algorithm (cf., Num. Rec. in C, Section 2.3).  The map of
 * swapped indeces is recorded in idx. The return value is +1 or -1
 * depending on whether the number of row swaps was even or odd
 * respectively.  idx must be preinitialized to a valid set of indices
 * (e.g., {1,2, ... ,A.nr_rows()}). */
static int lu_factorize(double_mat &A, int_vect &idx, float tol=TINY_FLOAT)
{
    if ( tol <= 0.0)
        tol = TINY_FLOAT;

    if ((A.nr_rows() == 0) || (A.nr_rows() != A.nr_cols())) {
      //sgs_error("lu_factorize(): cannot handle empty "
      //           "or nonsquare matrices.\n");

        return 0;
    }

    double_vect scale(A.nr_rows());  // implicit pivot scaling
    int i,j;
    for (i = 0; i < A.nr_rows(); ++i) {
        float maxval = 0.0;
        for (j = 0; j < A.nr_cols(); ++j) {
            if (fabs(A[i][j]) > maxval)
                maxval = fabs(A[i][j]);
        }
        if (maxval == 0.0) {
	  //sgs_error("lu_factorize(): zero pivot found.\n");
            return 0;
        }
        scale[i] = 1.0 / maxval;
    }

    int swapNum = 1;
    int c,r;
    for (c = 0; c < A.nr_cols() ; ++c) {            // loop over columns
        swapNum *= partial_pivot(A, c, c, scale, idx, tol); // bring pivot to diagonal
        for(r = 0; r < A.nr_rows(); ++r) {      //  loop over rows
            int lim = (r < c) ? r : c;
            for (j = 0; j < lim; ++j) {
                A[idx[r]][c] -= A[idx[r]][j] * A[idx[j]][c];
            }
            if (r > c)
                A[idx[r]][c] /= A[idx[c]][c];
        }
    }
    permute(A,idx);
    return swapNum;
}

/*! \brief Solve a system of linear equations.
 * Solves the inhomogeneous matrix problem with lu-decomposition. Note that
 * inversion may be accomplished by setting a to the identity_matrix. */
static double_mat lin_solve(const double_mat &A, const double_mat &a,
                           float tol=TINY_FLOAT)
{
    double_mat B(A);
    double_mat b(a);
    int_vect idx(B.nr_rows());
    int j;

    for (j = 0; j < B.nr_rows(); ++j) {
        idx[j] = j;  // init row swap label array
    }
    lu_factorize(B,idx,tol); // get the lu-decomp.
    permute(b,idx);          // sort the inhomogeneity to match the lu-decomp
    lu_forwsubst(B,b);       // solve the forward problem
    lu_backsubst(B,b);       // solve the backward problem
    return b;
}

///////////////////////
// related functions //
///////////////////////

//! Returns the inverse of a matrix using LU-decomposition.
static double_mat invert(const double_mat &A)
{
    const int n = A.size();
    double_mat E(n, n, 0.0);
    double_mat B(A);
    int i;

    for (i = 0; i < n; ++i) {
        E[i][i] = 1.0;
    }

    return lin_solve(B, E);
}

//! returns the transposed matrix.
static double_mat transpose(const double_mat &a)
{
    double_mat res(a.nr_cols(), a.nr_rows());
    int i,j;

    for (i = 0; i < a.nr_rows(); ++i) {
        for (j = 0; j < a.nr_cols(); ++j) {
            res[j][i] = a[i][j];
        }
    }
    return res;
}

//! matrix multiplication.
double_mat operator *(const double_mat &a, const double_mat &b)
{
    double_mat res(a.nr_rows(), b.nr_cols());
    if (a.nr_cols() != b.nr_rows()) {
      //sgs_error("incompatible matrices in multiplication\n");
        return res;
    }

    int i,j,k;

    for (i = 0; i < a.nr_rows(); ++i) {
        for (j = 0; j < b.nr_cols(); ++j) {
            double sum(0.0);
            for (k = 0; k < a.nr_cols(); ++k) {
                sum += a[i][k] * b[k][j];
            }
            res[i][j] = sum;
        }
    }
    return res;
}


//! calculate savitzky golay coefficients.
static double_vect sg_coeff(const double_vect &b, const size_t deg)
{
    const size_t rows(b.size());
    const size_t cols(deg + 1);
    double_mat A(rows, cols);
    double_vect res(rows);

    // generate input matrix for least squares fit
    int i,j;
    for (i = 0; i < rows; ++i) {
        for (j = 0; j < cols; ++j) {
            A[i][j] = pow(double(i), double(j));
        }
    }

    double_mat c(invert(transpose(A) * A) * (transpose(A) * transpose(b)));

    for (i = 0; i < b.size(); ++i) {
        res[i] = c[0][0];
        for (j = 1; j <= deg; ++j) {
            res[i] += c[j][0] * pow(double(i), double(j));
        }
    }
    return res;
}

/*! \brief savitzky golay smoothing.
 *
 * This method means fitting a polynome of degree 'deg' to a sliding window
 * of width 2w+1 throughout the data.  The needed coefficients are
 * generated dynamically by doing a least squares fit on a "symmetric" unit
 * vector of size 2w+1, e.g. for w=2 b=(0,0,1,0,0). evaluating the polynome
 * yields the sg-coefficients.  at the border non symmectric vectors b are
 * used. */
bool sg_smooth(const double *v, double *res, const int size, const int width, const int deg)
{
    memset(res, 0, sizeof(double)*size);
    if ((width < 1) || (deg < 0) || (size < (2 * width + 2))) {
      //sgs_error("sgsmooth: parameter error.\n");
        return false;
    }

    const int window = 2 * width + 1;
    const int endidx = size - 1;

    // do a regular sliding window average
    int i,j;
    if (deg == 0) {
        // handle border cases first because we need different coefficients
        for (i = 0; i < width; ++i) {
	    const double scale = 1.0/double(i+1);
            const double_vect c1(width, scale);
            for (j = 0; j <= i; ++j) {
                res[i]          += c1[j] * v[j];
                res[endidx - i] += c1[j] * v[endidx - j];
            }
        }

        // now loop over rest of data. reusing the "symmetric" coefficients.
	const double scale = 1.0/double(window);
        const  double_vect c2(window, scale);
        for (i = 0; i <= (size - window); ++i) {
            for (j = 0; j < window; ++j) {
                res[i + width] += c2[j] * v[i + j];
            }
        }
        return true;
    }

    // handle border cases first because we need different coefficients
    for (i = 0; i < width; ++i) {
        double_vect b1(window, 0.0);
        b1[i] = 1.0;

        const double_vect c1(sg_coeff(b1, deg));
        for (j = 0; j < window; ++j) {
            res[i]          += c1[j] * v[j];
            res[endidx - i] += c1[j] * v[endidx - j];
        }
    }

    // now loop over rest of data. reusing the "symmetric" coefficients.
    double_vect b2(window, 0.0);
    b2[width] = 1.0;
    const double_vect c2(sg_coeff(b2, deg));

    for (i = 0; i <= (size - window); ++i) {
        for (j = 0; j < window; ++j) {
            res[i + width] += c2[j] * v[i + j];
        }
    }
    return true;
}
