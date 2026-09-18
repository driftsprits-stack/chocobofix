#include "service/crypto.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

#if defined(__APPLE__)
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#define TA_PBKDF2_COMMONCRYPTO 1
#elif defined(TA_HAVE_OPENSSL)
#include <openssl/evp.h>
#define TA_PBKDF2_OPENSSL 1
#else
#error "No vetted PBKDF2 implementation available. Build with OpenSSL (-DTA_HAVE_OPENSSL) or on Apple."
#endif

namespace ta {
namespace {

constexpr size_t kSaltBytes = 16;
constexpr size_t kKeyBytes = 32;

std::string ToHex(const unsigned char* p, size_t n) {
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) { out.push_back(kHex[p[i] >> 4]); out.push_back(kHex[p[i] & 0xF]); }
  return out;
}

bool FromHex(const std::string& s, std::vector<unsigned char>* out) {
  if (s.size() % 2) return false;
  out->clear();
  for (size_t i = 0; i < s.size(); i += 2) {
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    const int hi = nib(s[i]), lo = nib(s[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out->push_back(static_cast<unsigned char>((hi << 4) | lo));
  }
  return true;
}

bool Pbkdf2(const std::string& password, const unsigned char* salt, size_t salt_len,
            int iterations, unsigned char* out, size_t out_len) {
#if TA_PBKDF2_COMMONCRYPTO
  return CCKeyDerivationPBKDF(kCCPBKDF2, password.data(), password.size(), salt, salt_len,
                              kCCPRFHmacAlgSHA256, static_cast<unsigned>(iterations),
                              out, out_len) == kCCSuccess;
#else
  return PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt,
                           static_cast<int>(salt_len), iterations, EVP_sha256(),
                           static_cast<int>(out_len), out) == 1;
#endif
}

}  // namespace

std::vector<unsigned char> RandomBytes(size_t n) {
  std::vector<unsigned char> buf(n);
  int fd = ::open("/dev/urandom", O_RDONLY);
  if (fd < 0) throw std::runtime_error("cannot open /dev/urandom");
  size_t got = 0;
  while (got < n) {
    const ssize_t r = ::read(fd, buf.data() + got, n - got);
    if (r <= 0) { ::close(fd); throw std::runtime_error("short read from /dev/urandom"); }
    got += static_cast<size_t>(r);
  }
  ::close(fd);
  return buf;
}

std::string RandomToken(size_t bytes) {
  const auto b = RandomBytes(bytes);
  return ToHex(b.data(), b.size());
}

// Stored form: pbkdf2_sha256$<iterations>$<salt_hex>$<key_hex>
std::string HashPassword(const std::string& password, int iterations) {
  const auto salt = RandomBytes(kSaltBytes);
  unsigned char key[kKeyBytes];
  if (!Pbkdf2(password, salt.data(), salt.size(), iterations, key, sizeof key))
    throw std::runtime_error("PBKDF2 failed");
  return "pbkdf2_sha256$" + std::to_string(iterations) + "$" + ToHex(salt.data(), salt.size()) +
         "$" + ToHex(key, sizeof key);
}

bool VerifyPassword(const std::string& password, const std::string& stored) {
  std::vector<std::string> parts;
  std::string cur;
  for (char c : stored) { if (c == '$') { parts.push_back(cur); cur.clear(); } else cur.push_back(c); }
  parts.push_back(cur);
  if (parts.size() != 4 || parts[0] != "pbkdf2_sha256") return false;
  int iterations = 0;
  try { iterations = std::stoi(parts[1]); } catch (...) { return false; }
  if (iterations < 1000 || iterations > 10000000) return false;
  std::vector<unsigned char> salt;
  if (!FromHex(parts[2], &salt) || salt.empty()) return false;
  unsigned char key[kKeyBytes];
  if (!Pbkdf2(password, salt.data(), salt.size(), iterations, key, sizeof key)) return false;
  return ConstantTimeEquals(ToHex(key, sizeof key), parts[3]);
}

bool ConstantTimeEquals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned diff = 0;
  for (size_t i = 0; i < a.size(); ++i)
    diff |= static_cast<unsigned>(static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
  return diff == 0;
}

}  // namespace ta
