#pragma once

#include <string>

#ifndef FIX_NAME
#define FIX_NAME "ExampleFix"
#endif

#ifndef VERSION_MAJOR
#define VERSION_MAJOR 0
#endif
#ifndef VERSION_MINOR
#define VERSION_MINOR 1
#endif
#ifndef VERSION_PATCH
#define VERSION_PATCH 0
#endif

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)
#define VERSION_STRING STRINGIFY(VERSION_MAJOR) "." STRINGIFY(VERSION_MINOR) "." STRINGIFY(VERSION_PATCH)

inline const std::string sFixVersion = VERSION_STRING;
inline const std::string sFixName = FIX_NAME;

#define COMPANY_NAME      "AnyAspectRatio"
#define PRODUCT_NAME      FIX_NAME
#define PRODUCT_VERSION   VERSION_STRING
#define FILE_VERSION      VERSION_STRING
#define LEGAL_COPYRIGHT   "Copyright (c) 2026 AnyAspectRatio contributors."
#define LEGAL_TRADEMARKS  ""
#define COMMENTS          ""
#define FILE_DESCRIPTION_ASI     FIX_NAME " ASI Plugin"
#define INTERNAL_NAME_ASI        FIX_NAME ".asi"
#define ORIGINAL_FILENAME_ASI    FIX_NAME ".asi"
