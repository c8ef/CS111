#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string_view>

#include <openssl/sha.h>

// Equivalent to memset(data, 0, size), but through use of volatile
// and prevention of inlining, less likely to get optimized away.
// Used to erase sensitive data from memory when it is no longer
// needed, so as to avoid, e.g., leaving copies of cryptographic keys
// around in memory.
void secure_erase(volatile std::uint8_t *data, std::size_t size);

// Exception thrown when OpenSSL's libcrypto returns an error
class crypto_error : public std::runtime_error {
  std::shared_ptr<std::string> msg_;

public:
  crypto_error(std::shared_ptr<std::string> msg)
      : runtime_error(*msg), msg_(std::move(msg)) {}
  crypto_error(std::string msg)
      : crypto_error(
            std::make_shared<std::string>(std::string(std::move(msg)))) {}
};

// A 32-byte encryption key suitable for use with AES128 in XEX mode,
// which requires two 16-byte subkeys.  This is just a
// std::array<uint8_t, 32> except that you can initialize it from a
// string and it will hash the string to ensure all 32 bytes depend
// equally on all input bytes even if your string is not 32 bytes
// long.  Also, the destructor attempts to wipe the key from memory.
struct Key : public std::array<std::uint8_t, 32> {
  using std::array<std::uint8_t, 32>::array;
  Key() {}
  Key(std::string_view s) {
    static_assert(array().size() == SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const std::uint8_t *>(s.data()), s.size(), data());
  }
  Key &operator=(std::string_view s) {
    SHA256(reinterpret_cast<const std::uint8_t *>(s.data()), s.size(), data());
    return *this;
  }
  ~Key() { secure_erase(data(), size()); }
};

// Encrypt pages that are a multiple of the blocksize using
// Xor-Encrypt-Xor (XEX) mode with the AES128 block cipher.  The
// 16-byte block "in" at "offset" is encrypted to result "out" by
// computing:
//
//   X := Encrypt(K2, offset/blocksize)
//   out := Encrypt(K1, in XOR X) XOR X
//
// Here K1 is the first 16 bytes of the key, K2 is the second 16 bytes
// of the key, and offset/blocksize is encoded as a blocksize-byte
// integer in big-endian order.
//
// This scheme ensures that repeated plaintext blocks do not result in
// repeated ciphertext blocks, which would leak information about the
// contents of a file.
struct PageCrypter {
  // Size of the block in the underlying AES blockcipher.
  static constexpr std::size_t blocksize = 16;
  Key key_;

  PageCrypter() {}
  explicit PageCrypter(std::string_view sv) : key_(sv) {}

  // Encrypt and decrypt a page.  Both offset and len must be a
  // multiple of blocksize.  Note that offset is used only to tweak
  // the encryption; the data encrypted is always between src and
  // src+len and the result is stored from dst to dst+len.
  void encrypt(std::uint8_t *dst, const std::uint8_t *src, std::size_t len,
               std::size_t offset);
  void decrypt(std::uint8_t *dst, const std::uint8_t *src, std::size_t len,
               std::size_t offset);

private:
  std::unique_ptr<std::uint8_t[]> tweaks(std::size_t offset, std::size_t len);
};
