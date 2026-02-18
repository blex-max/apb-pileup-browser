#include "extb.hpp"
#include <cstdlib>
#include <iostream>
#include <stdlib.h>

#include "app.hpp"
#include "hts/boundary-types.hpp"


int main(int, char **) {

    auto& ctx = app::init ();

    // --- DEMO --- //
    // n.b. probaby good to keep a demo mode in the final product!

    // set up main display for showing query sequences
    // auto& main_display = ctx.ui.display;
    // extb::clear(main_display);
    // auto ref_line = extb::Box::make_rel_row (main_display, 0);
    // auto ref_sep = extb::Box::make_rel_row (main_display, 1);
    // extb::set_cell (ref_sep, 0x2500, TB_DIM);
    // auto query_box = extb::Box::make_rel_box
    //     (main_display, 0, main_display.ilast, 2, main_display.ilast);

    // log_err (
    //     "main display box: x {}-{}, y {}-{}; xlast {}, ylast {}",
    //     main_display.gj1,
    //     main_display.gj2,
    //     main_display.gi1,
    //     main_display.gi2,
    //     main_display.ilast,
    //     main_display.ilast
    // );
    // log_err (
    //     "query display box: x {}-{}, y {}-{}",
    //     query_box.gj1,
    //     query_box.gj2,
    //     query_box.gi1,
    //     query_box.gi2
    // );

    // const auto dd = make_test_display_data(10, 20);

    // const auto &queries = std::get<Queries> (dd);
    // for (size_t i = 0; i < queries.size(); ++i) {
    //     const auto q = queries[i];
    //     write_string (
    //         query_box,
    //         {static_cast<int> (q.start), static_cast<int> (i)},
    //         queries[i].q
    //     );
    // }
    // int row_sel = 0;
    // add_attr(ctx.ui.display, {0, row_sel}, TB_REVERSE);

    // tb_present();

    try {
        app::loop(ctx);
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        tb_shutdown();
        return EXIT_FAILURE;
    }

    tb_shutdown();
    return EXIT_SUCCESS;
}
