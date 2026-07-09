#include "cli.hpp"

#include "argparse/argparse.hpp"

#include "plog/Log.h"
#include "util.hpp"
#include <expected>

ArgsOrErr init_cli (int argc, char** argv)
{
  argparse::ArgumentParser cli ("apb", "0.0.0");
  StartupArgs args;

  cli.add_argument ("--demo")
    .help ("run with synthetic data, no alignment file required")
    .flag()
    .store_into(args.demo);
  cli.add_argument ("--dump")
    .help ("convert pileup to sqlite3 database, dump to disk, and exit.")
    .nargs(1)
    .metavar("PATH")
    .store_into(args.dumpPath);
  // TODO: redo below args
  cli.add_argument ("SAM")
    .help ("path to alignment file in s/b/cram format")
    .nargs (argparse::nargs_pattern::optional);
  cli.add_argument ("region")
    .help ("genomic region in the form tid:position")
    .nargs (argparse::nargs_pattern::optional);

  try {
    cli.parse_args(argc, argv);
  }
  catch (const std::exception& ex) {
    std::ostringstream oss;
    oss << ex.what() << "\n" << cli;
    return std::unexpected{make_cli_err(oss.str())};
  }

  if (args.demo) {
    return args;  // no further info needed
  }

  const std::string alnFp = cli.present<std::string>("SAM").value_or("");
  if (alnFp.empty()) {
    std::ostringstream oss;
    oss << "SAM argument required when not using --demo\n" << cli;
    return std::unexpected(make_cli_err(oss.str()));
  }

  // TODO: better just to store paths at this stage, and evaluate the total
  // set of args before we start opening files.
  PLOGD << "Opening alignment file";
  {
    AlnFile aln;
    aln.o_fh = hts_open(alnFp.c_str(), "r");
    if (!aln.o_fh)
      return std::unexpected(make_htslib_err(-1, std::format("Could not open alignment file at {}", alnFp)));
    aln.o_hdr = sam_hdr_read(aln.o_fh);
    if (!aln.o_hdr)
      return std::unexpected(make_htslib_err(-1, "Could not read header from alignment file"));
    aln.o_idx = sam_index_load(aln.o_fh, alnFp.c_str());
    if (!aln.o_idx)
      return std::unexpected(make_htslib_err(-1, std::format("Could not load index for {}", alnFp)));
    args.aln = std::move(aln);
  }

  PLOGD << "Parsing region string";
  PileupPosition userRegion{};
  const auto regionStr = cli.present<std::string>("region").value_or("");
  if (regionStr.empty()) {
    std::ostringstream oss;
    oss << "region argument required when not using --demo\n" << cli;
    return std::unexpected(make_cli_err(oss.str()));
  }
  {
    hts_pos_t _pend=1; // required by htslib
    if (hts_parse_region(regionStr.c_str(), &userRegion.tid, &userRegion.pos,
                         &_pend, reinterpret_cast<hts_name2id_f>(sam_hdr_name2tid),
                         args.aln->o_hdr, HTS_PARSE_ONE_COORD) == NULL) {
      return std::unexpected(make_htslib_err(-1, std::format("Could not parse region string {}", regionStr)));
    }
  }
  args.start = userRegion;

  return args;
}
