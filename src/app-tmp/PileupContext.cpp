#include "PileupContext.hpp"

#include <htslib/sam.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>

#include "hts/accessors.hpp"
#include "hts/types.hpp"
#include "util.hpp"

PileupBundle::PileupBundle(PileupBundle&& o) noexcept
    : pos{o.pos},
      span{o.span},
      ref_seq{std::move(o.ref_seq)},
      storage{o.storage},
      data{std::move(o.data)}
{
  o.storage = nullptr;
}

PileupBundle& PileupBundle::operator=(PileupBundle&& o) noexcept
{
  if (this != &o) {
    if (storage) {
      bam_plp_destroy(storage);
    }
    pos = o.pos;
    span = o.span;
    ref_seq = std::move(o.ref_seq);
    storage = o.storage;
    o.storage = nullptr;
    data = std::move(o.data);
  }
  return *this;
}

static std::unordered_map<std::string_view, StringifyFn>
    BAM_RENDER_CALLBACKS{
        {"qual",
         [](const bam_pileup1_t* p1) -> std::string {
           return std::to_string(htsacc::base_qual(p1));
         }},
        {"flag", [](const bam_pileup1_t* p1) -> std::string {
           return std::to_string(htsacc::flag(p1));
         }}
    };

StringifyFn get_pileup_text_callback(std::string_view name)
{
  auto it = BAM_RENDER_CALLBACKS.find(name);
  return it != BAM_RENDER_CALLBACKS.end() ? it->second : nullptr;
}

extern "C" {
struct PileupCapture {
  htsFile* fh;
  hts_itr_t* it;
};
int pileup_func(void* data, bam1_t* b)
{
  PileupCapture* d = (PileupCapture*)(data);
  return sam_itr_next(d->fh, d->it, b);
}
}

void load_pileup(
    PileupBundle& b, const htsacc::AlnFile& fh,
    const PileupPosition& pos
)
{
  auto aln_iter =
      sam_itr_queryi(fh.idx, pos.tid, pos.pos, pos.pos + 1);
  if (aln_iter == nullptr) {
    throw format_runtime_error(
        "could not create iterator for pileup at "
        "{}:{}",
        sam_hdr_tid2name(
            fh.hdr,
            pos.tid
        ),  // could fail
        pos.pos
    );
  }
  PileupCapture pfc{fh.f, aln_iter};
  auto piter = bam_plp_init(pileup_func, &pfc);

  PileupColumn data;
  int64_t plp_pos = -1;
  int plp_tid = -1;
  int n_plp = -1;
  const bam_pileup1_t* plarr;
  while ((plarr = bam_plp64_auto(
              piter, &plp_tid, &plp_pos, &n_plp
          )) != 0) {
    if (n_plp < 0 || plp_tid < 0 || plp_pos < 0) {
      throw std::runtime_error("pileup failed");
    }

    if (plp_pos < pos.pos) {
      continue;  // doesn't cover variant
    }
    else if (plp_pos == pos.pos) {
      const auto nread = static_cast<size_t>(n_plp);
      data.reserve(nread);
      for (size_t i = 0; i < nread; ++i) {
        data.push_back(plarr + i);
      }
    }
    break;
  }

  // sort ascending query start
  std::sort(
      begin(data), end(data), [](const auto a, const auto b) {
        return a->b->core.pos < b->b->core.pos;
      }
  );

  b.data.clear();
  if (b.storage) {
    bam_plp_destroy(b.storage);
    b.storage = nullptr;
  }
  b.pos = pos;
  b.storage = piter;
  b.data = std::move(data);
  if (data.empty()) {
    b.span = {
        0, 0
    };  // unknown implications if this is later used to fetch reference
  }
  else {
    b.span = {
        data.front()->b->core.pos, data.back()->b->core.pos
    };
  }

  hts_itr_destroy(aln_iter);
}
