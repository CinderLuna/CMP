#include "cmp/ResistorBands.h"
#include <QCoreApplication>
#include <QTextStream>
#include <limits>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool pass = true;
    auto check = [&](double value, const QStringList &expected)
    {
        auto bands = cmp::resistorBands(value);
        const bool valid = bands.exact && bands.names == expected && bands.colors.size() == expected.size();
        pass &= valid;
        QTextStream(stdout) << (valid ? "PASS " : "FAIL ") << value << ": " << bands.names.join('/') << '\n';
    };
    check(1000, {"brown", "black", "red", "gold"});
    check(10000, {"brown", "black", "orange", "gold"});
    check(68000, {"blue", "grey", "orange", "gold"});
    check(470, {"yellow", "violet", "brown", "gold"});
    check(4.7, {"yellow", "violet", "gold", "gold"});
    check(.47, {"yellow", "violet", "silver", "gold"});
    check(.1, {"brown", "black", "silver", "gold"});
    check(4990, {"yellow", "white", "white", "brown", "gold"});
    check(1.01, {"brown", "black", "brown", "silver", "gold"});
    check(999e9, {"white", "white", "white", "white", "gold"});
    for (double value : {0., -10., 12345., .001, 1e12, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()})
    {
        const auto bands = cmp::resistorBands(value);
        pass &= !bands.exact && bands.names.isEmpty() && bands.colors.isEmpty();
    }
    QTextStream(stdout) << "exact_band_mapping_and_no_silent_rounding=" << pass << '\n';
    return pass ? 0 : 1;
}
