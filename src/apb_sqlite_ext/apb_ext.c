// clang-format off
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
// clang-format on

#define S3_TRUE 1
#define S3_FALSE 0

/* Insert your extension code here */
// TODO: sqlite3 scalar (operating on a single row) function
// for apb_feature_at

// returns 1 if true, else 0
void apb_contains_motif (
    sqlite3_context* ctx, int argc, sqlite3_value** argv
)
{
  // (void*)ctx
  /*
     expected arguments are record cigar (blob), record seq (text), cigar motif (text), seq motif (text).
     At least one pair of cigar + cigar motif OR seq + seq motif must both not be NULL.
     NULL expresses optionality for other combinations.
  */
  // TODO: first, basic exact matching
  assert (argc == 4);
  const uint32_t* br_cig = sqlite3_value_blob (argv[0]);
  // FIXME: it is unnecessary to store nCig in the schema,
  // because we can just do this:
  unsigned long lCig =
      (unsigned long)sqlite3_value_bytes (argv[0]) / sizeof (uint32_t);

  const unsigned char* br_seq = sqlite3_value_text (argv[1]);
  unsigned long lSeq = sqlite3_value_bytes (argv[1]);

  const unsigned char* br_cigMotif = sqlite3_value_text (argv[2]);
  unsigned long lCigMotif = sqlite3_value_bytes (argv[2]);

  const unsigned char* br_seqMotif = sqlite3_value_text (argv[3]);
  unsigned long lSeqMotif = sqlite3_value_bytes (argv[3]);

  if (br_seq == NULL && br_cig == NULL) {
    sqlite3_result_null (ctx);
  }

  if (br_seqMotif != NULL && br_cigMotif != NULL) {
    // then we're doing an alignment event match
  }
  else if (br_seqMotif != NULL) {
    // then we're doing a string search on seq,
    // and can fall back to sqlite3's instr
    if (br_seq == NULL) {
      sqlite3_result_null (ctx);
    }
    if (lSeqMotif == 0) {
      // 0-len search motif always matches
      sqlite3_result_int (ctx, S3_TRUE);
    }
    // else match on seq
  }
  else if (br_cigMotif != NULL) {
    // then we're doing a string search on cigar,
    // and can fall back to sqlite3's instr
    if (br_cig == NULL) {
      sqlite3_result_null (ctx);
    }
    if (lSeqMotif == 0) {
      // 0-len search motif always matches
      sqlite3_result_int (ctx, S3_TRUE);
    }
    // else match on cigar
    // TODO: do I want the cigar string to search, or do I want to convert the motif text?
    // I think the latter!
  }
}


#ifdef _WIN32
__declspec (dllexport)
#endif
/* NOTE: entrypoint name must be sqlite3_<libname>_init where <libname>
** corresponds to the shared library .so name as follows:  Copy every
** ASCII alphabetic character from the filename after the last "/" through
** the next following ".", converting each character to lowercase, and
** discarding the first three characters if they are "lib".
*/
int sqlite3_apbext_init (
    sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi
)
{
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2 (pApi);
  /* Insert here calls to
  **     sqlite3_create_function_v2(),
  ** to register the new features that your extension adds.
  */
  return rc;
}
