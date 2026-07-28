#include "subcommands.hpp"

#include <fmt/format.h>
#include <plog/Log.h>

#include "app/tui.hpp"
#include "backend/PileupDB.hpp"
#include "backend/hts_types.hpp"
#include "demo/demo.hpp"

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
  auto stateRet = init (db);
  if (!stateRet) {
    shutdown();  // would be nice if shutdown was run on state going out of scope...
    return std::unexpected{stateRet.error()};
  }
  AppState state = std::move (*stateRet);

  auto loopRet = loop (state);
  if (!loopRet) {
    shutdown();
    return std::unexpected (loopRet.error());
  }

  shutdown();

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
  auto stateRet = init (db);
  if (!stateRet) {
    shutdown();  // would be nice if shutdown was run on state going out of scope...
    return std::unexpected{stateRet.error()};
  }
  AppState state = std::move (*stateRet);

  auto loopRet = loop (state);
  if (!loopRet) {
    shutdown();
    return std::unexpected (loopRet.error());
  }

  shutdown();

  return {};
}

VoidOrErr run_mode (const AlnModeArgs& args)
{
  PLOGD << "Opening alignment file";
  auto alnRet = load_aln (args.alnPath.c_str());
  if (!alnRet) {
    return std::unexpected (alnRet.error());
  }
  auto aln = std::move (*alnRet);

  PLOGD << "Parsing locus string";
  PileupPosition pos{};
  hts_pos_t _pend = 1;  // required by htslib, not used here
  if (hts_parse_region (
          args.locus.c_str(), &pos.tid, &pos.pos, &_pend,
          reinterpret_cast<hts_name2id_f> (sam_hdr_name2tid),
          aln.o_hdr, HTS_PARSE_ONE_COORD
      ) == NULL) {
    std::string errMsg{"Could not parse locus string "};
    errMsg += args.locus;
    errMsg += "; ";
    if (pos.tid < 0) {
      errMsg += "invalid contig";
    }
    else {
      errMsg += "malformed";
    }
    return std::unexpected (make_htslib_err (-1, errMsg));

    ;
  }

  std::optional<FastaFile> ff;
  if (args.refPath) {
    PLOGD << "Opening reference fasta file";
    auto ffRet = load_fasta ((*(args.refPath)).c_str());
    if (!ffRet) {
      return std::unexpected (ffRet.error());
    }
    ff.emplace (std::move (*ffRet));
  }

  PLOGD << "Creating database";
  PileupDB db;
  auto initRet = init_db (db);
  if (!initRet) {
    return std::unexpected (initRet.error());
  }

  PLOGD << "Inserting metadata";
  auto imRet = insert_metadata (db, aln);
  if (!imRet) {
    return std::unexpected (imRet.error());
  }

  PLOGD << "Inserting pileup";
  auto irRet = insert_pileup (db, aln, pos, ff);
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
  auto stateRet = init (db);
  if (!stateRet) {
    shutdown();  // would be nice if shutdown was run on state going out of scope...
    return std::unexpected{stateRet.error()};
  }
  AppState state = std::move (*stateRet);

  auto loopRet = loop (state);
  if (!loopRet) {
    shutdown();
    return std::unexpected (loopRet.error());
  }

  shutdown();

  return {};
}
