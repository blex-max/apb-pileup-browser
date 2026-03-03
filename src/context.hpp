#include "hts/boundary-types.hpp"
#include "singleton.hpp"

struct PileupContext : singleton::Singleton {
    struct {
        int row_sel = 0;
        PileupDisplayBundle pd;
    } state;
    struct {
    } ui;
};
