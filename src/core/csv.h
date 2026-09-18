// Minimal strict CSV reader/writer.
//
// Reading is deliberately unforgiving: every failure carries file, 1-based row
// and column name so the UI can point a planner at the exact cell. Nothing is
// coerced silently.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/model.h"

namespace ta {

// One actionable input problem. `row` is 1-based over the physical file
// including the header, matching what a spreadsheet shows.
struct InputError {
  std::string file;
  int row = 0;
  std::string field;
  std::string message;

  std::string Format() const;
};

class CsvTable {
 public:
  // Throws nothing; failures are appended to `errors`. Returns false if the
  // file could not be read or the header is unusable.
  static bool Load(const std::string& path, const std::vector<std::string>& required_columns,
                   CsvTable* out, std::vector<InputError>* errors);

  int RowCount() const { return static_cast<int>(rows_.size()); }
  const std::string& File() const { return file_; }

  // Accessors. Each records an InputError and returns a fallback if the cell is
  // missing or malformed, so a bad file yields a complete error list in one pass
  // rather than stopping at the first problem.
  std::string Str(int row, const std::string& col, std::vector<InputError>* errors) const;
  std::string OptStr(int row, const std::string& col) const;
  int Int(int row, const std::string& col, std::vector<InputError>* errors,
          int min_value, int max_value) const;
  Date DateOf(int row, const std::string& col, std::vector<InputError>* errors) const;

  void AddError(int row, const std::string& col, std::string message,
                std::vector<InputError>* errors) const;

 private:
  std::string file_;
  std::unordered_map<std::string, int> col_index_;
  std::vector<std::vector<std::string>> rows_;
};

// Writes RFC4180-ish CSV. Competition outputs contain only bare identifiers and
// integers, so `escape_for_spreadsheet` is false there: prefixing a cell would
// corrupt the required schema. Human-facing exports pass true, which neutralises
// leading =, +, -, @ so a spreadsheet cannot execute a crafted identifier.
class CsvWriter {
 public:
  CsvWriter(std::string path, std::vector<std::string> header,
            bool escape_for_spreadsheet = false);
  void Row(const std::vector<std::string>& cells);
  bool Commit(std::string* error);   // atomic: write temp, fsync, rename

 private:
  std::string path_;
  bool escape_;
  std::string buf_;
};

std::string Sha256Hex(const std::string& bytes);
bool ReadFile(const std::string& path, std::string* out);

}  // namespace ta
