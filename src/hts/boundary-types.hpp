#pragma once

#include <cstdint>
#include <htslib/sam.h>
#include <memory>
#include <string>


struct PileupSpan {
  uint64_t gstart;
  uint64_t gend;
  uint64_t pos;
};

// to display sequence
struct QueryRep {
  uint64_t id, qstart;
  std::string s;
  uint8_t qual=0;  // for demo
  uint8_t flag=0;
};

struct RefRep {
  std::string s;
};

struct PileupData {
  PileupSpan ps;
  struct {
    std::string s;
  } ref;
  struct {
    std::unique_ptr<bam_pileup1_t[]> begin;  // dereference to get array start
                                             // TODO must write custom free for this block of memory
    size_t idx;  // track position
    size_t n;
    // TODO cache arr indexed as bam_pileup1_t[] arr (it - *begin) to cached query data
    // does bam_pileup_cd have relevance to caching?
  } query;
};

// class PileupRecord {
  
// }

// class PileupRecords {
// public:
//     PileupRecords(const bam_pileup1_t* arr, std::size_t n)
//         : data_(arr), size_(n) {}

//     auto view() const {
//         return std::ranges::subrange(data_, data_ + size_)
//              | std::views::transform([](const bam_pileup1_t& r) {
//                    return RecordView{r};
//                });
//     }

// private:
//     const bam_pileup1_t* data_;
//     std::size_t size_;
// };

