#include "demo.hpp"
#include "core/PileupDB.hpp"

#include <cassert>
#include <cstdlib>
#include <random>

#include <htslib/sam.h>

static std::string random_base_seq (size_t len) {
    static const char bases[] = "ATCG";
    std::string out;
    for (size_t i = 0; i < len; ++i)
        out += bases[rand() % (sizeof(bases) - 1)];
    return out;
}

PileupDB make_demo_pileup (size_t regWidth, size_t nQuery) {
    std::mt19937 rng;

    const hts_pos_t pileupPos = static_cast<hts_pos_t>((regWidth / 2) - 1);
    const auto qLen = static_cast<size_t>(pileupPos);
    const auto refSeq = random_base_seq(regWidth);

    std::uniform_int_distribution<size_t> gstartGen(0, qLen);

    // TODO: make db
    PileupDB db;
    auto r = init(db);
    if (!r) {
        // TODO: handle err
    }

    for (size_t i = 0; i < nQuery; ++i) {
        const auto qGstart = gstartGen(rng);
        const auto readSeq = refSeq.substr(static_cast<size_t>(qGstart), qLen);
        const std::string qual(qLen, 'F');
        const uint32_t cigOp =
            (static_cast<uint32_t>(qLen) << BAM_CIGAR_SHIFT) | BAM_CMATCH;

        // TODO: fill db
    }

}

