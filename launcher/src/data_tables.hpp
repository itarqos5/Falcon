#pragma once

// Where the launcher downloads Falcon's data tables from, and the SHA-256 the zip must have. Written by tools/make_data_tables.ps1 when a release is made (it zips the tables, takes their
// hash and puts it here); the launcher refuses a zip that does not match.
namespace launcher {

inline constexpr const char* kDataTablesUrl = "https://github.com/itarqos5/Falcon/releases/download/v1.2.0/falcon-data-26.3.zip";
inline constexpr const char* kDataTablesSha256 = "e32757140d4974aa622a0505c5db377d2244685cc202f3fe1de5cad5e9e91309";
inline constexpr unsigned long long kDataTablesSize = 2438730;

}  // namespace launcher
