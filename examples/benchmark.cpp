#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <tuple>
#include <numeric>
#include <random>
#include <boost/program_options.hpp>
#define VEXCL_USE_CUSPARSE
#include <vexcl/devlist.hpp>
#include <vexcl/vector.hpp>
#include <vexcl/reductor.hpp>
#include <vexcl/random.hpp>
#include <vexcl/tagged_terminal.hpp>
#include <vexcl/element_index.hpp>
#include <vexcl/spmat.hpp>
#include <vexcl/stencil.hpp>

#ifdef _MSC_VER
#  pragma warning(disable : 4267)
#endif

//---------------------------------------------------------------------------
struct Options {
    bool bm_saxpy;
    bool bm_vector;
    bool bm_reductor;
    bool bm_stencil;
    bool bm_spmv;
    bool bm_rng;
    bool bm_cpu;

    Options() :
        bm_saxpy(true),
        bm_vector(true),
        bm_reductor(true),
        bm_stencil(true),
        bm_spmv(true),
        bm_rng(true),
        bm_cpu(true)
    {}
} options;

//---------------------------------------------------------------------------
template <typename real>
std::vector<real> random_vector(size_t n) {
    std::default_random_engine rng( std::rand() );
    std::uniform_real_distribution<real> rnd(0.0, 1.0);

    std::vector<real> x(n);
    std::generate(x.begin(), x.end(), [&]() { return rnd(rng); });

    return x;
}

//---------------------------------------------------------------------------
template <typename real>
std::pair<double,double> benchmark_saxpy(
        const vex::Context &ctx, vex::profiler<> &prof
        )
{
    const size_t N = 1024 * 1024;
    const size_t M = 1024;
    double time_elapsed;

    std::vector<real> A(N, 0);
    std::vector<real> B = random_vector<real>(N);
    std::vector<real> alphavec = random_vector<real>(1);
    real alpha = alphavec[0];

    vex::vector<real> a(ctx, A);
    vex::vector<real> b(ctx, B);

    auto ta = vex::tag<1>(a);

    ta = alpha * ta + b;
    ta = 0;

    prof.tic_cpu("OpenCL");
    for(size_t i = 0; i < M; i++)
        ta = alpha * ta + b;
    ctx.finish();
    time_elapsed = prof.toc("OpenCL");

    double gflops = (2.0 * N * M) / time_elapsed / 1e9;
    double bwidth = (3.0 * N * M * sizeof(real)) / time_elapsed / 1e9;

    std::cout
        << "Vector SAXPY (" << vex::type_name<real>() << ")\n"
        << "  OpenCL"
        << "\n    GFLOPS:    " << gflops
        << "\n    Bandwidth: " << bwidth
        << std::endl;

    if (options.bm_cpu) {
        prof.tic_cpu("C++");
        for(size_t i = 0; i < M; i++)
            for(size_t j = 0; j < N; j++)
                A[j] = alpha * A[j] + B[j];
        time_elapsed = prof.toc("C++");

        {
            double gflops = (2.0 * N * M) / time_elapsed / 1e9;
            double bwidth = (3.0 * N * M * sizeof(real)) / time_elapsed / 1e9;

            std::cout
                << "  C++"
                << "\n    GFLOPS:    " << gflops
                << "\n    Bandwidth: " << bwidth
                << std::endl;
        }

        vex::copy(A, b);
        vex::Reductor<real, vex::SUM> sum(ctx);

        a -= b;
        std::cout << "  res = " << sum(a * a)
                  << std::endl << std::endl;
    }

    return std::make_pair(gflops, bwidth);
}


//---------------------------------------------------------------------------
template <typename real>
std::pair<double,double> benchmark_vector(
        const vex::Context &ctx, vex::profiler<> &prof
        )
{
    const size_t N = 1024 * 1024;
    const size_t M = 1024;
    double time_elapsed;

    std::vector<real> A(N, 0);
    std::vector<real> B = random_vector<real>(N);
    std::vector<real> C = random_vector<real>(N);
    std::vector<real> D = random_vector<real>(N);

    vex::vector<real> a(ctx, A);
    vex::vector<real> b(ctx, B);
    vex::vector<real> c(ctx, C);
    vex::vector<real> d(ctx, D);

    a += b + c * d;
    a = 0;

    prof.tic_cpu("OpenCL");
    for(size_t i = 0; i < M; i++)
        a += b + c * d;
    ctx.finish();
    time_elapsed = prof.toc("OpenCL");

    double gflops = (3.0 * N * M) / time_elapsed / 1e9;
    double bwidth = (5.0 * N * M * sizeof(real)) / time_elapsed / 1e9;

    std::cout
        << "Vector arithmetic (" << vex::type_name<real>() << ")\n"
        << "  OpenCL"
        << "\n    GFLOPS:    " << gflops
        << "\n    Bandwidth: " << bwidth
        << std::endl;

    if (options.bm_cpu) {
        prof.tic_cpu("C++");
        for(size_t i = 0; i < M; i++)
            for(size_t j = 0; j < N; j++)
                A[j] += B[j] + C[j] * D[j];
        time_elapsed = prof.toc("C++");
        {
            double gflops = (3.0 * N * M) / time_elapsed / 1e9;
            double bwidth = (5.0 * N * M * sizeof(real)) / time_elapsed / 1e9;

            std::cout
                << "  C++"
                << "\n    GFLOPS:    " << gflops
                << "\n    Bandwidth: " << bwidth
                << std::endl;
        }

        vex::copy(A, b);
        vex::Reductor<real, vex::SUM> sum(ctx);

        a -= b;
        std::cout << "  res = " << sum(a * a)
                  << std::endl << std::endl;
    }

    return std::make_pair(gflops, bwidth);
}

//---------------------------------------------------------------------------
template <typename real>
std::pair<double, double> benchmark_reductor(
        const vex::Context &ctx, vex::profiler<> &prof
        )
{
    const size_t N = 16 * 1024 * 1024;
    const size_t M = 1024 / 16;
    double time_elapsed;

    std::vector<real> A = random_vector<real>(N);
    std::vector<real> B = random_vector<real>(N);

    vex::vector<real> a(ctx, A);
    vex::vector<real> b(ctx, B);

    vex::Reductor<real, vex::SUM> sum(ctx);

    real sum_cl = sum(a * b);
    sum_cl = 0;

    prof.tic_cpu("OpenCL");
    for(size_t i = 0; i < M; i++)
        sum_cl += sum(a * b);
    ctx.finish();
    time_elapsed = prof.toc("OpenCL");

    double gflops = 2.0 * N * M / time_elapsed / 1e9;
    double bwidth = 2.0 * N * M * sizeof(real) / time_elapsed / 1e9;

    std::cout
        << "Reduction (" << vex::type_name<real>() << ")\n"
        << "  OpenCL"
        << "\n    GFLOPS:    " << gflops
        << "\n    Bandwidth: " << bwidth
        << std::endl;

    if (options.bm_cpu) {
        real sum_cpp = 0;
        prof.tic_cpu("C++");
        for(size_t i = 0; i < M; i++)
            sum_cpp += std::inner_product(A.begin(), A.end(), B.begin(), static_cast<real>(0));
        time_elapsed = prof.toc("C++");

        {
            double gflops = 2.0 * N * M / time_elapsed / 1e9;
            double bwidth = 2.0 * N * M * sizeof(real) / time_elapsed / 1e9;

            std::cout
                << "  C++"
                << "\n    GFLOPS:    " << gflops
                << "\n    Bandwidth: " << bwidth
                << std::endl;
        }

        std::cout << "  res = " << fabs( (sum_cl - sum_cpp) / sum_cpp )
                  << std::endl << std::endl;
    }

    return std::make_pair(gflops, bwidth);
}

//---------------------------------------------------------------------------
template <typename real>
std::pair<double, double> benchmark_stencil(
        const vex::Context &ctx, vex::profiler<> &prof
        )
{
    const long N = 1024 * 1024;
    const long M = 1024;
    double time_elapsed;

    std::vector<real> A = random_vector<real>(N);
    std::vector<real> B(N);

    std::vector<real> S(21, static_cast<real>(1) / 21);
    long center = S.size() / 2;
    vex::stencil<real> s(ctx, S, center);

    vex::vector<real> a(ctx, A);
    vex::vector<real> b(ctx, N);

    b = a * s;

    prof.tic_cpu("OpenCL");
    for(long i = 0; i < M; i++)
        b = a * s;
    ctx.finish();
    time_elapsed = prof.toc("OpenCL");

    double gflops = 2.0 * S.size() * N * M / time_elapsed / 1e9;
    double bwidth = 2.0 * S.size() * N * M * sizeof(real) / time_elapsed / 1e9;

    std::cout
        << "Stencil convolution (" << vex::type_name<real>() << ")\n"
        << "  OpenCL"
        << "\n    GFLOPS:    " << gflops
        << "\n    Bandwidth: " << bwidth
        << std::endl;

    if (options.bm_cpu) {
        prof.tic_cpu("C++");
        for(long j = 0; j < M; j++) {
            for(long i = 0; i < N; i++) {
                real sum = 0;
                for(long k = 0; k < (long)S.size(); k++)
                    sum += S[k] * A[std::min<long>(N-1, std::max<long>(0, i + k - center))];
                B[i] = sum;
            }
        }
        time_elapsed = prof.toc("C++");

        {
            double gflops = 2.0 * S.size() * N * M / time_elapsed / 1e9;
            double bwidth = 2.0 * S.size() * N * M * sizeof(real) / time_elapsed / 1e9;

            std::cout
                << "  C++"
                << "\n    GFLOPS:    " << gflops
                << "\n    Bandwidth: " << bwidth
                << std::endl;
        }

        vex::Reductor<real, vex::MAX> max(ctx);
        copy(B, a);

        std::cout << "  res = " << max(fabs(a - b))
                  << std::endl << std::endl;
    }

    return std::make_pair(gflops, bwidth);
}

//---------------------------------------------------------------------------
template <typename real>
std::pair<double,double> benchmark_spmv(
        const vex::Context &ctx, vex::profiler<> &prof
        )
{
    // Construct matrix for 3D Poisson problem in cubic domain.
    const size_t n = 128;
    const size_t N = n * n * n;
    const size_t M = 1024;

    double time_elapsed;

    const real h2i = (n - 1) * (n - 1);

    std::vector<size_t> row;
    std::vector<uint>   col;
    std::vector<real>   val;
    std::vector<real>   X(n * n * n, static_cast<real>(1e-2));
    std::vector<real>   Y(n * n * n, 0);

    row.reserve(n * n * n + 1);
    col.reserve(6 * (n - 2) * (n - 2) * (n - 2) + n * n * n);
    val.reserve(6 * (n - 2) * (n - 2) * (n - 2) + n * n * n);

    row.push_back(0);
    for(size_t k = 0, idx = 0; k < n; k++) {
        for(size_t j = 0; j < n; j++) {
            for(size_t i = 0; i < n; i++, idx++) {
                if (
                        i == 0 || i == (n - 1) ||
                        j == 0 || j == (n - 1) ||
                        k == 0 || k == (n - 1)
                   )
                {
                    col.push_back(idx);
                    val.push_back(1);
                    row.push_back(row.back() + 1);
                } else {
                    col.push_back(idx - n * n);
                    val.push_back(-h2i);

                    col.push_back(idx - n);
                    val.push_back(-h2i);

                    col.push_back(idx - 1);
                    val.push_back(-h2i);

                    col.push_back(idx);
                    val.push_back(6 * h2i);

                    col.push_back(idx + 1);
                    val.push_back(-h2i);

                    col.push_back(idx + n);
                    val.push_back(-h2i);

                    col.push_back(idx + n * n);
                    val.push_back(-h2i);

                    row.push_back(row.back() + 7);
                }
            }
        }
    }

    size_t nnz = row.back();

    // Transfer data to compute devices.
    vex::SpMat<real,uint> A(ctx, n * n * n, n * n * n, row.data(), col.data(), val.data());

    vex::vector<real> x(ctx, X);
    vex::vector<real> y(ctx, Y);

    // Get timings.
    y += A * x;
    y = 0;

    prof.tic_cpu("OpenCL");
    for(size_t i = 0; i < M; i++)
        y += A * x;
    ctx.finish();
    time_elapsed = prof.toc("OpenCL");

    double gflops = (2.0 * nnz + N) * M / time_elapsed / 1e9;
    double bwidth = M * (nnz * (2 * sizeof(real) + sizeof(size_t)) + 4 * N * sizeof(real)) / time_elapsed / 1e9;

    std::cout
        << "SpMV (" << vex::type_name<real>() << ")\n"
        << "  OpenCL"
        << "\n    GFLOPS:    " << gflops
        << "\n    Bandwidth: " << bwidth
        << std::endl;

    if (options.bm_cpu) {
        prof.tic_cpu("C++");
        for(size_t k = 0; k < M; k++)
            for(size_t i = 0; i < N; i++) {
                real s = 0;
                for(size_t j = row[i]; j < row[i + 1]; j++)
                    s += val[j] * X[col[j]];
                Y[i] += s;
            }
        time_elapsed = prof.toc("C++");

        {
            double gflops = (2.0 * nnz + N) * M / time_elapsed / 1e9;
            double bwidth = M * (nnz * (2 * sizeof(real) + sizeof(size_t)) + 4 * N * sizeof(real)) / time_elapsed / 1e9;

            std::cout
                << "  C++"
                << "\n    GFLOPS:    " << gflops
                << "\n    Bandwidth: " << bwidth
                << std::endl;
        }

        copy(Y, x);

        y -= x;

        vex::Reductor<real, vex::SUM> sum(ctx);

        std::cout << "  res = " << sum(y * y) << std::endl << std::endl;
    }

    return std::make_pair(gflops, bwidth);
}

//---------------------------------------------------------------------------
template <typename real>
std::pair<double,double> benchmark_spmv_ccsr(
        const vex::Context &ctx, vex::profiler<> &prof
        )
{
    // Construct matrix for 3D Poisson problem in cubic domain.
    const uint n = 128;
    const uint N = n * n * n;
    const uint M = 1024;

    double time_elapsed;

    const real h2i = (n - 1) * (n - 1);

    std::vector<size_t> idx;
    std::vector<size_t> row(3);
    std::vector<int>    col(8);
    std::vector<real>   val(8);

    std::vector<real>   X(n * n * n, static_cast<real>(1e-2));
    std::vector<real>   Y(n * n * n, 0);

    idx.reserve(n * n * n);

    row[0] = 0;
    row[1] = 1;
    row[2] = 8;

    col[0] = 0;
    val[0] = 1;

    col[1] = -static_cast<int>(n * n);
    col[2] = -static_cast<int>(n);
    col[3] =    -1;
    col[4] =     0;
    col[5] =     1;
    col[6] =     n;
    col[7] =  (n * n);

    val[1] = -h2i;
    val[2] = -h2i;
    val[3] = -h2i;
    val[4] =  h2i * 6;
    val[5] = -h2i;
    val[6] = -h2i;
    val[7] = -h2i;

    for(size_t k = 0; k < n; k++) {
        for(size_t j = 0; j < n; j++) {
            for(size_t i = 0; i < n; i++) {
                if (
                        i == 0 || i == (n - 1) ||
                        j == 0 || j == (n - 1) ||
                        k == 0 || k == (n - 1)
                   )
                {
                    idx.push_back(0);
                } else {
                    idx.push_back(1);
                }
            }
        }
    }

    size_t nnz = 6 * (n - 2) * (n - 2) * (n - 2) + n * n * n;

    // Transfer data to compute devices.
    vex::SpMatCCSR<real,int> A(ctx.queue(0), n * n * n, 2,
            idx.data(), row.data(), col.data(), val.data());

    std::vector<vex::command_queue> q1(1, ctx.queue(0));
    vex::vector<real> x(q1, X);
    vex::vector<real> y(q1, Y);

    // Get timings.
    y += A * x;
    y = 0;

    prof.tic_cpu("OpenCL");
    for(size_t i = 0; i < M; i++)
        y += A * x;
    ctx.finish();
    time_elapsed = prof.toc("OpenCL");

    double gflops = (2.0 * nnz + N) * M / time_elapsed / 1e9;
    double bwidth = M * (nnz * (2 * sizeof(real) + sizeof(int)) + 4 * N * sizeof(real)) / time_elapsed / 1e9;

    std::cout
        << "SpMV (CCSR) (" << vex::type_name<real>() << ")\n"
        << "  OpenCL"
        << "\n    GFLOPS:    " << gflops
        << "\n    Bandwidth: " << bwidth
        << std::endl;

    if (options.bm_cpu) {
        prof.tic_cpu("C++");
        for(size_t k = 0; k < M; k++)
            for(size_t i = 0; i < N; i++) {
                real s = 0;
                for(size_t j = row[idx[i]]; j < row[idx[i] + 1]; j++)
                    s += val[j] * X[i + col[j]];
                Y[i] += s;
            }
        time_elapsed = prof.toc("C++");

        {
            double gflops = (2.0 * nnz + N) * M / time_elapsed / 1e9;
            double bwidth = M * (nnz * (2 * sizeof(real) + sizeof(int)) + 4 * N * sizeof(real)) / time_elapsed / 1e9;

            std::cout
                << "  C++"
                << "\n    GFLOPS:    " << gflops
                << "\n    Bandwidth: " << bwidth
                << std::endl;
        }

        copy(Y, x);

        y -= x;

        vex::Reductor<real, vex::SUM> sum(q1);

        std::cout << "  res = " << sum(y * y) << std::endl << std::endl;
    }

    return std::make_pair(gflops, bwidth);
}

//---------------------------------------------------------------------------
template <typename real, class GF>
double rng_throughput(const vex::Context &ctx, size_t N, size_t M) {

    vex::Random<real, GF> rnd;
    vex::Reductor<real, vex::MAX> max(ctx);

    real s = max( rnd( vex::element_index(0, N), std::rand() ) );

    vex::stopwatch<> w;

    for(size_t i = 0; i < M; i++)
        s = std::max(s, max( rnd( vex::element_index(0, N), std::rand() ) ));
    ctx.finish();

    return N * M / w.toc();
}

//---------------------------------------------------------------------------
template <typename real>
void benchmark_rng(
        const vex::Context &ctx, vex::profiler<> &prof
        )
{
    const size_t N = 16 * 1024 * 1024;
    const size_t M = 1024;

    prof.tic_cpu("OpenCL (threefry)");
    double rps = rng_throughput<real, vex::random::threefry>(ctx, N, M);
    prof.toc("OpenCL (threefry)");

    std::cout
        << "Random numbers per second (" << vex::type_name<real>() << ")\n"
        << "    OpenCL (threefry): " << rps << std::endl;

    prof.tic_cpu("OpenCL (philox)");
    rps = rng_throughput<real, vex::random::philox>(ctx, N, M);
    prof.toc("OpenCL (philox)");

    std::cout
        << "    OpenCL (philox):   " << rps << std::endl;

    if (options.bm_cpu) {
        std::mt19937 rng( std::rand() );
        std::uniform_real_distribution<real> rnd(0.0, 1.0);

        prof.tic_cpu("C++ (mt19937)");
        real s = 0;
        for(size_t j = 0; j < N; j++)
            s = std::max(s, rnd(rng));
        double time_elapsed = prof.toc("C++ (mt19937)");

        std::cout
            << "    C++    (mt19937):  " << N / time_elapsed << std::endl;
    }
}

//---------------------------------------------------------------------------
template <typename real>
void run_tests(const vex::Context &ctx, vex::profiler<> &prof)
{
    std::cout
        << "----------------------------------------------------------\n"
        << "Profiling \"" << vex::type_name<real>() << "\" performance\n"
        << "----------------------------------------------------------\n"
        << ctx << std::endl;

    std::ostringstream fname;
    fname << "profile_" << vex::type_name<real>() << ".dat";
    std::ofstream log(fname.str().c_str(), std::ios::app);

    log << ctx.size() << " ";

    double gflops, bwidth;

    prof.tic_cpu( vex::type_name<real>() );

    if (options.bm_saxpy) {
        prof.tic_cpu("Vector SAXPY");
        std::tie(gflops, bwidth) = benchmark_saxpy<real>(ctx, prof);
        prof.toc("Vector SAXPY");

        log << gflops << " " << bwidth << " ";
    }

    if (options.bm_vector) {
        prof.tic_cpu("Vector arithmetic");
        std::tie(gflops, bwidth) = benchmark_vector<real>(ctx, prof);
        prof.toc("Vector arithmetic");

        log << gflops << " " << bwidth << " ";
    }

    if (options.bm_reductor) {
        prof.tic_cpu("Reduction");
        std::tie(gflops, bwidth) = benchmark_reductor<real>(ctx, prof);
        prof.toc("Reduction");

        log << gflops << " " << bwidth << " ";
    }

    if (options.bm_stencil) {
        prof.tic_cpu("Stencil");
        std::tie(gflops, bwidth) = benchmark_stencil<real>(ctx, prof);
        prof.toc("Stencil");

        log << gflops << " " << bwidth << " ";
    }

    if (options.bm_spmv) {
        prof.tic_cpu("SpMV");
        std::tie(gflops, bwidth) = benchmark_spmv<real>(ctx, prof);
        prof.toc("SpMV");

        log << gflops << " " << bwidth << std::endl;

        prof.tic_cpu("SpMV (CCSR)");
        std::tie(gflops, bwidth) = benchmark_spmv_ccsr<real>(ctx, prof);
        prof.toc("SpMV (CCSR)");
    }

    if (options.bm_rng) {
        prof.tic_cpu("Random number generation");
        benchmark_rng<real>(ctx, prof);
        prof.toc("Random number generation");
    }

    prof.toc( vex::type_name<real>() );

    std::cout << std::endl << std::endl;
}

//---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    namespace po = boost::program_options;
    po::options_description desc("Options");

    desc.add_options()
        ("help,h", "show help")
        ("bm_vec",
            po::value<bool>(&options.bm_vector)->default_value(true),
            "benchmark vector arithmetics (on/off)"
            )
        ("bm_rdc",
            po::value<bool>(&options.bm_reductor)->default_value(true),
            "benchmark reduction (on/off)"
            )
        ("bm_stn",
            po::value<bool>(&options.bm_stencil)->default_value(true),
            "benchmark stencil convolution (on/off)"
            )
        ("bm_spm",
            po::value<bool>(&options.bm_spmv)->default_value(true),
            "benchmark sparse matrix - vector product (on/off)"
            )
        ("bm_rng",
            po::value<bool>(&options.bm_rng)->default_value(true),
            "benchmark random number generation (on/off)"
            )
        ("bm_cpu",
            po::value<bool>(&options.bm_cpu)->default_value(true),
            "benchmark host CPU performance (on/off)"
            )
        ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }

    try {
        vex::profiler<> prof;

        {
            vex::Context ctx(vex::Filter::Env && vex::Filter::DoublePrecision);
            if (ctx) run_tests<double>(ctx, prof);
        }

        {
            vex::Context ctx(vex::Filter::Env);
            if (ctx) run_tests<float>(ctx, prof);
        }

        std::cout << prof << std::endl;
    } catch (const vex::error &e) {
        std::cerr << e << std::endl;
        return 1;
    }
}

// vim: et
