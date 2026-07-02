#pragma once

#include <htslib/hts.h>
#include <htslib/sam.h>

namespace htsacc {

struct AlnFile {
    htsFile*   f   = nullptr;
    sam_hdr_t* hdr = nullptr;
    hts_idx_t* idx = nullptr;

    ~AlnFile() noexcept;
    AlnFile() = default;
    AlnFile(const AlnFile&)            = delete;
    AlnFile& operator=(const AlnFile&) = delete;
    AlnFile(AlnFile&&) noexcept;
    AlnFile& operator=(AlnFile&&) noexcept;
};

}
