#pragma once

#include "backend/PileupDB.hpp"

VoidOrErr insert_demo_data (
    PileupDB& db, size_t regWidth, size_t nQuery
);
