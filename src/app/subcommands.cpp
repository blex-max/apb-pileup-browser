#include "subcommands.hpp"

#include <plog/Log.h>

#include <format>

#include "app/tui.hpp"
#include "backend/PileupDB.hpp"
#include "backend/demo.hpp"
#include "backend/hts_types.hpp"

VoidOrErr run_mode (const DemoModeArgs& args)
{
  PLOGD << "Creating database";
  PileupDB db;
  auto initRet = init_db (db);
  if (!initRet) {
    return std::unexpected (initRet.error());
  }

  PLOGD << "Inserting demo data into pileup db";
  auto demoRet = insert_demo_data (db, 300, 100);
  if (!demoRet) {
    return std::unexpected (demoRet.error());
  }

  if (args.dumpPath) {
    PLOGD << "Dumping db";
    auto dumpRet = dump_to_disk (db, *args.dumpPath);
    if (!dumpRet) {
      return std::unexpected (dumpRet.error());
    }
    return {};
  }

  // load frontend

  return {};
}

VoidOrErr run_mode (const DbModeArgs& args)
{
  PLOGD << "Creating database";
  PileupDB db;
  auto initRet = init_db (db);
  if (!initRet) {
    return std::unexpected (initRet.error());
  }

  PLOGD << "Loading db from disk";
  auto loadRet = load_from_disk (db, args.dbPath);
  if (!loadRet) {
    return std::unexpected (loadRet.error());
  }

  // load frontend
  auto stateRet = init();
  if (!stateRet) {
    return std::unexpected{initRet.error()};
  }
  AppState state = *stateRet;

  loop (state);
  shutdown (state);

  return {};
}

VoidOrErr run_mode (const AlnModeArgs& args)
{
  PLOGD << "Opening alignment file";
  AlnFile aln;
  aln.o_fh = hts_open (args.alnPath.c_str(), "r");
  if (!aln.o_fh) {
    return std::unexpected (make_htslib_err (
        -1,
        std::format (
            "Could not open alignment file at {}", args.alnPath
        )
    ));
  }
  aln.o_hdr = sam_hdr_read (aln.o_fh);
  if (!aln.o_hdr) {
    return std::unexpected (make_htslib_err (
        -1,
        "Could not read header "
        "from alignment file"
    ));
  }
  aln.o_idx = sam_index_load (aln.o_fh, args.alnPath.c_str());
  if (!aln.o_idx) {
    return std::unexpected (make_htslib_err (
        -1,
        std::format ("Could not load index for {}", args.alnPath)
    ));
  }

  PLOGD << "Parsing locus string";
  PileupPosition pos{};
  hts_pos_t _pend = 1; // required by htslib
  if (hts_parse_region (
          args.locus.c_str(), &pos.tid, &pos.pos, &_pend,
          reinterpret_cast<hts_name2id_f> (sam_hdr_name2tid),
          aln.o_hdr, HTS_PARSE_ONE_COORD
      ) == NULL) {
    return std::unexpected (make_htslib_err (
        -1, std::format (
                "Could not parse locus string {}", args.locus
            )
    ));
  }

  PLOGD << "Creating database";
  PileupDB db;
  auto initRet = init_db (db);
  if (!initRet) {
    return std::unexpected (initRet.error());
  }

  PLOGD << "Inserting alignment pileup into "
           "pileup db";
  auto imRet = insert_metadata (db, aln);
  if (!imRet) {
    return std::unexpected (imRet.error());
  }

  auto irRet = insert_pileup (db, aln, pos);
  if (!irRet) {
    return std::unexpected (irRet.error());
  }

  if (args.dumpPath) {
    PLOGD << "Dumping db";
    auto dumpRet = dump_to_disk (db, *args.dumpPath);
    if (!dumpRet) {
      return std::unexpected (dumpRet.error());
    }
    return {};
  }

  // load frontend

  return {};
}
