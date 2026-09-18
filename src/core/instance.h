#pragma once

#include <string>
#include <vector>

#include "core/csv.h"
#include "core/model.h"

namespace ta {

// Loads the eight instance CSVs from `dir`. Returns false if any hard input
// problem was found; `errors` then lists every problem discovered (the loader
// does not stop at the first), each with file, row and field.
//
// The eight canonical filenames are fixed by the brief. `dir` may contain other
// files; they are ignored.
bool LoadInstance(const std::string& dir, Instance* out, std::vector<InputError>* errors);

// Exposed for tests: builds chain indices, expands every activity's span into
// occupied locations, derives closure zones, and computes exclusive pairs.
// Assumes the raw tables are already loaded and cross-referenced.
bool BuildTopology(Instance* inst, std::vector<InputError>* errors);

extern const char* const kInstanceFiles[8];

}  // namespace ta
