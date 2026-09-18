// Password hashing and token handling.
//
// No cryptography is implemented here. Key derivation is delegated to the
// platform's vetted PBKDF2-HMAC-SHA256: CommonCrypto on Apple, OpenSSL
// elsewhere. Randomness comes from the OS CSPRNG. If neither backend is
// available the build fails rather than falling back to something weaker.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ta {

// Bytes from the operating system CSPRNG. Throws std::runtime_error if the
// source cannot be read - a service that cannot generate unguessable tokens
// must not start, not carry on with predictable ones.
std::vector<unsigned char> RandomBytes(size_t n);
std::string RandomToken(size_t bytes = 32);   // URL-safe hex

// PBKDF2-HMAC-SHA256. `iterations` is stored with the hash so it can be raised
// later without invalidating existing accounts.
std::string HashPassword(const std::string& password, int iterations = 210000);

// Returns true if `password` matches `stored`. Constant-time in the digest
// comparison. A malformed record returns false rather than throwing.
bool VerifyPassword(const std::string& password, const std::string& stored);

// Constant-time comparison for tokens and digests.
bool ConstantTimeEquals(const std::string& a, const std::string& b);

// Content hashing (ta::Sha256Hex) lives in core/csv.h. It is used to bind a plan
// to its bytes and an approval to a validation result - never for passwords.

}  // namespace ta
