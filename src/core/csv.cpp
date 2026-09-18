#include "core/csv.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>

namespace ta {
namespace {

// Splits one CSV record, honouring double-quoted fields with "" escapes.
std::vector<std::string> SplitRecord(std::string_view line) {
  std::vector<std::string> out;
  std::string cur;
  bool in_quotes = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (in_quotes) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') { cur.push_back('"'); ++i; }
        else in_quotes = false;
      } else {
        cur.push_back(c);
      }
    } else if (c == '"') {
      in_quotes = true;
    } else if (c == ',') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

std::string Trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

}  // namespace

std::string InputError::Format() const {
  std::ostringstream os;
  os << file;
  if (row > 0) os << ":row " << row;
  if (!field.empty()) os << ":" << field;
  os << ": " << message;
  return os.str();
}

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream os;
  os << in.rdbuf();
  *out = os.str();
  return true;
}

// Self-contained SHA-256 (FIPS 180-4). Used only to bind a plan to the exact
// input bytes it came from, so a dependency on a TLS library is not warranted.
// Correctness is pinned by known-answer tests in tests/test_core.cpp.
namespace {

struct Sha256Ctx {
  std::uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  std::uint64_t len = 0;
  unsigned char buf[64]{};
  size_t buf_len = 0;
};

constexpr std::uint32_t kK[64] = {
  0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
  0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
  0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
  0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
  0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
  0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
  0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
  0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};

inline std::uint32_t Ror(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void Sha256Block(Sha256Ctx& c, const unsigned char* p) {
  std::uint32_t w[64];
  for (int i = 0; i < 16; ++i)
    w[i] = (std::uint32_t(p[i*4]) << 24) | (std::uint32_t(p[i*4+1]) << 16) |
           (std::uint32_t(p[i*4+2]) << 8) | std::uint32_t(p[i*4+3]);
  for (int i = 16; i < 64; ++i) {
    const std::uint32_t s0 = Ror(w[i-15],7) ^ Ror(w[i-15],18) ^ (w[i-15] >> 3);
    const std::uint32_t s1 = Ror(w[i-2],17) ^ Ror(w[i-2],19) ^ (w[i-2] >> 10);
    w[i] = w[i-16] + s0 + w[i-7] + s1;
  }
  std::uint32_t a=c.h[0],b=c.h[1],cc=c.h[2],d=c.h[3],e=c.h[4],f=c.h[5],g=c.h[6],hh=c.h[7];
  for (int i = 0; i < 64; ++i) {
    const std::uint32_t S1 = Ror(e,6) ^ Ror(e,11) ^ Ror(e,25);
    const std::uint32_t ch = (e & f) ^ (~e & g);
    const std::uint32_t t1 = hh + S1 + ch + kK[i] + w[i];
    const std::uint32_t S0 = Ror(a,2) ^ Ror(a,13) ^ Ror(a,22);
    const std::uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
    const std::uint32_t t2 = S0 + mj;
    hh=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
  }
  c.h[0]+=a; c.h[1]+=b; c.h[2]+=cc; c.h[3]+=d; c.h[4]+=e; c.h[5]+=f; c.h[6]+=g; c.h[7]+=hh;
}

void Sha256Update(Sha256Ctx& c, const unsigned char* p, size_t n) {
  c.len += n;
  while (n > 0) {
    const size_t take = std::min(n, 64 - c.buf_len);
    std::memcpy(c.buf + c.buf_len, p, take);
    c.buf_len += take; p += take; n -= take;
    if (c.buf_len == 64) { Sha256Block(c, c.buf); c.buf_len = 0; }
  }
}

}  // namespace

std::string Sha256Hex(const std::string& bytes) {
  Sha256Ctx c;
  Sha256Update(c, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
  const std::uint64_t bitlen = c.len * 8;
  unsigned char pad = 0x80;
  Sha256Update(c, &pad, 1);
  unsigned char zero = 0;
  while (c.buf_len != 56) Sha256Update(c, &zero, 1);
  unsigned char lenbe[8];
  for (int i = 0; i < 8; ++i) lenbe[i] = static_cast<unsigned char>(bitlen >> (56 - 8*i));
  c.len -= 8;  // length bytes are not part of the message
  Sha256Update(c, lenbe, 8);

  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (int i = 0; i < 8; ++i)
    for (int b = 3; b >= 0; --b) {
      const unsigned char v = static_cast<unsigned char>(c.h[i] >> (8*b));
      out.push_back(kHex[v >> 4]); out.push_back(kHex[v & 0xF]);
    }
  return out;
}

void CsvTable::AddError(int row, const std::string& col, std::string message,
                        std::vector<InputError>* errors) const {
  if (errors) errors->push_back(InputError{file_, row, col, std::move(message)});
}

bool CsvTable::Load(const std::string& path, const std::vector<std::string>& required_columns,
                    CsvTable* out, std::vector<InputError>* errors) {
  std::string raw;
  if (!ReadFile(path, &raw)) {
    if (errors) errors->push_back(InputError{path, 0, "", "file could not be read"});
    return false;
  }
  // Reject absurd inputs before parsing, so a hostile upload cannot exhaust memory.
  static constexpr size_t kMaxBytes = 32u * 1024 * 1024;
  if (raw.size() > kMaxBytes) {
    if (errors) errors->push_back(InputError{path, 0, "", "file exceeds 32 MiB limit"});
    return false;
  }
  out->file_ = path;
  if (raw.rfind("\xEF\xBB\xBF", 0) == 0) raw.erase(0, 3);  // strip UTF-8 BOM

  std::vector<std::string> lines;
  { std::istringstream is(raw); std::string l;
    while (std::getline(is, l)) { if (!l.empty() && l.back() == '\r') l.pop_back();
                                  lines.push_back(std::move(l)); } }
  if (lines.empty()) {
    if (errors) errors->push_back(InputError{path, 0, "", "file is empty"});
    return false;
  }
  const auto header = SplitRecord(lines[0]);
  for (size_t i = 0; i < header.size(); ++i) out->col_index_[Trim(header[i])] = static_cast<int>(i);

  bool ok = true;
  for (const auto& c : required_columns) {
    if (!out->col_index_.count(c)) {
      if (errors) errors->push_back(InputError{path, 1, c, "required column is missing from the header"});
      ok = false;
    }
  }
  if (!ok) return false;

  static constexpr size_t kMaxRows = 500000;
  for (size_t i = 1; i < lines.size(); ++i) {
    if (Trim(lines[i]).empty()) continue;   // tolerate trailing blank lines only
    if (out->rows_.size() >= kMaxRows) {
      if (errors) errors->push_back(InputError{path, static_cast<int>(i + 1), "", "file exceeds 500000 data rows"});
      return false;
    }
    auto cells = SplitRecord(lines[i]);
    cells.resize(header.size());
    for (auto& c : cells) c = Trim(std::move(c));
    out->rows_.push_back(std::move(cells));
  }
  return true;
}

std::string CsvTable::Str(int row, const std::string& col, std::vector<InputError>* errors) const {
  auto it = col_index_.find(col);
  if (it == col_index_.end()) { AddError(row + 2, col, "column not present", errors); return {}; }
  const auto& v = rows_[row][it->second];
  if (v.empty()) AddError(row + 2, col, "value is required but empty", errors);
  return v;
}

std::string CsvTable::OptStr(int row, const std::string& col) const {
  auto it = col_index_.find(col);
  if (it == col_index_.end()) return {};
  return rows_[row][it->second];
}

int CsvTable::Int(int row, const std::string& col, std::vector<InputError>* errors,
                  int min_value, int max_value) const {
  const std::string v = Str(row, col, errors);
  if (v.empty()) return min_value;
  int parsed = 0;
  const char* b = v.data();
  const char* e = v.data() + v.size();
  auto [ptr, ec] = std::from_chars(b, e, parsed);
  if (ec != std::errc() || ptr != e) {
    AddError(row + 2, col, "expected an integer, found \"" + v + "\"", errors);
    return min_value;
  }
  if (parsed < min_value || parsed > max_value) {
    AddError(row + 2, col, "value " + v + " is outside the permitted range " +
             std::to_string(min_value) + ".." + std::to_string(max_value), errors);
    return std::clamp(parsed, min_value, max_value);
  }
  return parsed;
}

Date CsvTable::DateOf(int row, const std::string& col, std::vector<InputError>* errors) const {
  const std::string v = Str(row, col, errors);
  if (v.empty()) return Date{};
  auto d = Date::Parse(v);
  if (!d) {
    AddError(row + 2, col, "expected a date as YYYY-MM-DD, found \"" + v + "\"", errors);
    return Date{};
  }
  return *d;
}

// --- writer ----------------------------------------------------------------

CsvWriter::CsvWriter(std::string path, std::vector<std::string> header, bool escape)
    : path_(std::move(path)), escape_(escape) {
  Row(header);
}

void CsvWriter::Row(const std::vector<std::string>& cells) {
  for (size_t i = 0; i < cells.size(); ++i) {
    if (i) buf_.push_back(',');
    std::string v = cells[i];
    // Spreadsheet formula neutralisation, only where the schema permits it.
    if (escape_ && !v.empty() && (v[0] == '=' || v[0] == '+' || v[0] == '-' || v[0] == '@')) {
      v.insert(v.begin(), '\'');
    }
    const bool needs_quote = v.find_first_of(",\"\n\r") != std::string::npos;
    if (needs_quote) {
      buf_.push_back('"');
      for (char c : v) { if (c == '"') buf_.push_back('"'); buf_.push_back(c); }
      buf_.push_back('"');
    } else {
      buf_ += v;
    }
  }
  buf_.push_back('\n');
}

// Durability: content lands in a sibling temp file which is fsynced, then
// renamed over the target. A crash leaves either the old file or the new one,
// never a half-written schedule. Success is reported only after the rename.
bool CsvWriter::Commit(std::string* error) {
  const std::string tmp = path_ + ".tmp";
  int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) { *error = "cannot open " + tmp + " for writing"; return false; }
  size_t off = 0;
  while (off < buf_.size()) {
    ssize_t n = ::write(fd, buf_.data() + off, buf_.size() - off);
    if (n <= 0) { ::close(fd); ::unlink(tmp.c_str()); *error = "write failed for " + tmp; return false; }
    off += static_cast<size_t>(n);
  }
  if (::fsync(fd) != 0) { ::close(fd); ::unlink(tmp.c_str()); *error = "fsync failed for " + tmp; return false; }
  ::close(fd);
  if (::rename(tmp.c_str(), path_.c_str()) != 0) {
    ::unlink(tmp.c_str()); *error = "rename failed for " + path_; return false;
  }
  return true;
}

}  // namespace ta
