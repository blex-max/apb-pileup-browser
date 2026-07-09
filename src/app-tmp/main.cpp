#include <cstdlib>
#include <stdlib.h>

#include <argparse/argparse.hpp>

#include <plog/Log.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Formatters/TxtFormatter.h>

#include "app.hpp"


int main(int argc, char** argv) {
    plog::init<plog::TxtFormatter> (plog::debug, plog::streamStdErr);

    try {
        app::init (app::init_cli (argc, argv));
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    try {
        app::loop();
    } catch (const std::exception &e) {
        PLOGF << e.what();
        app::shutdown();
        return EXIT_FAILURE;
    }

    app::shutdown();
    return EXIT_SUCCESS;
}
