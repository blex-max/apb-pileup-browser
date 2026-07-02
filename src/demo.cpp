#include "demo.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <htslib/sam.h>
#include <random>

namespace demo {

static std::string random_base_seq (size_t len) {
    static const char bases[] = "ATCG";
    std::string out;
    for (size_t i = 0; i < len; ++i)
        out += bases[rand() % (sizeof(bases) - 1)];
    return out;
}

extern "C" {
struct DemoCapture { bam1_t* arr; size_t n; size_t i; };
static int demo_read_fn (void* data, bam1_t* b) {
    auto* d = static_cast<DemoCapture*>(data);
    if (d->i >= d->n) return -1;
    return bam_copy1(b, d->arr + d->i++) ? 0 : -1;
}
}

PileupBundle make_demo_pileup (size_t region_width, size_t n_query) {
    std::mt19937 rng;

    const hts_pos_t pileup_pos = static_cast<hts_pos_t>((region_width / 2) - 1);
    const auto qlen = static_cast<size_t>(pileup_pos);
    const auto ref_seq = random_base_seq(region_width);

    std::uniform_int_distribution<size_t> gstart_gen(0, qlen);
    std::vector<hts_pos_t> v_gstart(n_query);
    std::ranges::generate(v_gstart, [&]{ return static_cast<hts_pos_t>(gstart_gen(rng)); });
    std::sort(begin(v_gstart), end(v_gstart));

    auto barr = static_cast<bam1_t*>(calloc(n_query, sizeof(bam1_t)));

    for (size_t i = 0; i < n_query; ++i) {
        const hts_pos_t q_gstart = v_gstart[i];
        const auto read_seq = ref_seq.substr(static_cast<size_t>(q_gstart), qlen);
        const std::string qual(qlen, 'F');
        const uint32_t cigar_op =
            (static_cast<uint32_t>(qlen) << BAM_CIGAR_SHIFT) | BAM_CMATCH;
        bam_set1(
            barr + i,
            0, nullptr,
            0,          // flags: mapped
            0,          // tid
            q_gstart,
            30,         // mapq
            1, &cigar_op,
            -1, 0, 0,
            static_cast<size_t>(qlen),
            read_seq.c_str(), qual.c_str(),
            0
        );
    }

    DemoCapture cap{barr, n_query, 0};
    auto piter = bam_plp_init(demo_read_fn, &cap);

    PileupColumn col;
    int64_t plp_pos = -1;
    int plp_tid = -1;
    int n_plp   = -1;
    const bam_pileup1_t* plarr;
    while ((plarr = bam_plp64_auto(piter, &plp_tid, &plp_pos, &n_plp)) != nullptr) {
        if (n_plp < 0 || plp_tid < 0 || plp_pos < 0)
            throw std::runtime_error("demo pileup failed");
        if (plp_pos < pileup_pos) continue;
        if (plp_pos == pileup_pos) {
            const auto nread = static_cast<size_t>(n_plp);
            col.reserve(nread);
            for (size_t i = 0; i < nread; ++i)
                col.push_back(plarr + i);
        }
        break;
    }

    std::sort(begin(col), end(col),
        [](const auto* a, const auto* b){ return a->b->core.pos < b->b->core.pos; });

    // htslib has copied the bam records internally; free temp storage
    for (size_t i = 0; i < n_query; ++i) free(barr[i].data);
    free(barr);

    PileupBundle b;
    b.pos     = {0, pileup_pos};
    b.span    = col.empty()
                    ? PileupSpan{0, 0}
                    : PileupSpan{col.front()->b->core.pos, col.back()->b->core.pos};
    b.ref_seq = ref_seq;
    b.storage = piter;  // NOTE: DemoCapture is out of scope — do not advance storage
    b.data    = std::move(col);
    return b;
}

}  // namespace demo
