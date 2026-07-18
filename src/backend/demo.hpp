#pragma once

#include "backend/PileupDB.hpp"

VoidOrErr insert_demo_data (
    PileupDB& db, size_t region_width, size_t n_query
);
