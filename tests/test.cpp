// NOTE:
// The point of these tests is to catch bugs which will not
// be trivially caught by the defensive programming (APB_ASSERT/UNREACHABLE etc.)
// during test runs. So rare cases only rather than general behaviour verification.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
