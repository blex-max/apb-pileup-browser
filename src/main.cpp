#include <cstdlib>
#include <locale.h>
#include <stdlib.h>

#define TB_IMPL
#include "app.hpp"
#include "hts/boundary-types.hpp"

int main(int, char **) {

    auto& ctx = app::init ();

    // --- DEMO DATA --- //
    // n.b. probaby good to keep a demo mode in the final product!

    const auto dd = make_test_display_data(10, 20);

    // --- END --- //

    const auto &queries = std::get<Queries> (dd);
    for (size_t i = 0; i < queries.size(); ++i) {
        const auto q = queries[i];
        write_string (
            ctx.ui.display,
            {static_cast<int> (q.start), static_cast<int> (i)},
            queries[i].q
        );
    }
    int row_sel = 0;
    add_attr(ctx.ui.display, {0, row_sel}, TB_REVERSE);

    tb_present();

    app::loop(ctx);

    tb_shutdown();
    return EXIT_SUCCESS;
}
