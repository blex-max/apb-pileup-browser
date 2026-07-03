#include "hts/types.hpp"


namespace htsacc {

AlnFile::~AlnFile() noexcept {
    if (idx) hts_idx_destroy(idx);
    if (hdr) sam_hdr_destroy(hdr);
    if (f)   hts_close(f);
}

AlnFile::AlnFile(AlnFile&& o) noexcept
    : f{o.f}, hdr{o.hdr}, idx{o.idx}
{
    o.f = nullptr; o.hdr = nullptr; o.idx = nullptr;
}

AlnFile& AlnFile::operator=(AlnFile&& o) noexcept {
    if (this != &o) {
        if (idx) hts_idx_destroy(idx);
        if (hdr) sam_hdr_destroy(hdr);
        if (f)   hts_close(f);
        f = o.f; hdr = o.hdr; idx = o.idx;
        o.f = nullptr; o.hdr = nullptr; o.idx = nullptr;
    }
    return *this;
}

}
