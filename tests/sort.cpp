#define BOOST_TEST_MODULE Sort
#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <vexcl/vector.hpp>
#include <vexcl/sort.hpp>
#include "context_setup.hpp"

BOOST_AUTO_TEST_CASE(sort_pairs)
{
    const size_t n = 1000 * 1000;

    std::vector<int  > k = random_vector<int  >(n);
    vex::vector<int  > keys(ctx, k);
    vex::vector<float> vals(ctx, random_vector<float>(n));

    vex::sort_by_key(keys, vals);
    vex::copy(keys, k);

    BOOST_CHECK( std::is_sorted(k.begin(), k.end()) );

    struct {
        VEX_FUNCTION(device, bool(int, int),
                "char bit1 = 1 & prm1;\n"
                "char bit2 = 1 & prm2;\n"
                "if (bit1 == bit2) return prm1 < prm2;\n"
                "return bit1 < bit2;\n"
                );
        bool operator()(int a, int b) const {
            char bit1 = 1 & a;
            char bit2 = 1 & b;
            if (bit1 == bit2) return a < b;
            return bit1 < bit2;
        }
    } even_first;

    vex::sort_by_key(keys, vals, even_first);
    vex::copy(keys, k);

    BOOST_CHECK(std::is_sorted(k.begin(), k.end(), even_first));
}

BOOST_AUTO_TEST_CASE(sort_keys)
{
    const size_t n = 1000 * 1000;

    std::vector<float> k = random_vector<float>(n);
    vex::vector<float> keys(ctx, k);

    vex::sort(keys);
    vex::copy(keys, k);

    BOOST_CHECK( std::is_sorted(k.begin(), k.end()) );
}

BOOST_AUTO_TEST_SUITE_END()
