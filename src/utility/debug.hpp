#pragma once

#ifdef STDERR_DEBUG
#include <iostream>
#define CERR_DEBUG(msg) std::cerr << msg << std::endl;
#else
#define CERR_DEBUG(msg)
#endif
