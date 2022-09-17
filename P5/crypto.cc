#include <memory>
#include <string>
#include <type_traits>

#include <openssl/err.h>
#include <openssl/evp.h>

#include "crypto.hh"

using std::size_t;
using std::uint8_t;

static_assert(std::is_nothrow_copy_constructible<crypto_error>::value);

[[gnu::noinline]] void secure_erase(volatile uint8_t *data, size_t size) {
  std::fill(data, data + size, 0);
}

[[noreturn]] void crypto_raise(std::string msg = "") {
  unsigned long code = ERR_get_error();
  if (code) {
    if (!msg.empty())
      msg += ": ";
    char buf[120];
    ERR_error_string_n(code, buf, sizeof(buf));
    msg += buf;
  }
  if (msg.empty())
    msg = "crypto_error";
  throw crypto_error(msg);
}

namespace {
// Wrap C types that have destructors in C++ types that automatically
// invoke the destructor.  ctype<Destructor> is analogous to a
// unique_ptr<T> where the type T is inferred from the argument type
// of Destructor(T*).
template <auto Destructor> struct ctype;
template <typename T, typename R, R (*Destructor)(T *)>
struct ctype<Destructor> {
  T *val_;
  ctype() : val_(nullptr) {}
  explicit ctype(T *val) : val_(val) {}
  ctype(ctype &&other) : val_(other.val_) { other.val_ = nullptr; }
  ~ctype() {
    if (val_)
      Destructor(val_);
  }
  operator T *() { return val_; }
};

using CipherCtx = ctype<EVP_CIPHER_CTX_free>;

void xorbuf(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
            size_t len) {
  using u = uint64_t;
  static_assert(PageCrypter::blocksize % sizeof(u) == 0);
  for (size_t i = 0; i < len; i += sizeof(u))
    reinterpret_cast<u &>(dst[i]) = reinterpret_cast<const u &>(src1[i]) ^
                                    reinterpret_cast<const u &>(src2[i]);
}
} // namespace

void PageCrypter::encrypt(uint8_t *dst, const uint8_t *src, size_t len,
                          size_t offset) {
  std::unique_ptr<uint8_t[]> buf = tweaks(offset, len);
  xorbuf(dst, src, buf.get(), len);

  CipherCtx ctx{EVP_CIPHER_CTX_new()};
  if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key_.data(),
                         nullptr) != 1)
    crypto_raise("EVP_EncryptInit_ex(aes_128_ecb)");
  int outl;
  if (EVP_EncryptUpdate(ctx, dst, &outl, dst, len) != 1)
    crypto_raise("EVP_EncryptUpdate(aes_128_ecb)");

  xorbuf(dst, dst, buf.get(), len);
}

void PageCrypter::decrypt(uint8_t *dst, const uint8_t *src, size_t len,
                          size_t offset) {
  std::unique_ptr<uint8_t[]> buf = tweaks(offset, len);
  xorbuf(dst, src, buf.get(), len);

  CipherCtx ctx{EVP_CIPHER_CTX_new()};
  if (EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key_.data(),
                         nullptr) != 1)
    crypto_raise("EVP_DecryptInit_ex(aes_128_ecb)");
  int outl;
  if (EVP_DecryptUpdate(ctx, dst, &outl, dst, len) != 1)
    crypto_raise("EVP_DecryptUpdate(aes_128_ecb)");

  xorbuf(dst, dst, buf.get(), len);
}

std::unique_ptr<uint8_t[]> PageCrypter::tweaks(size_t offset, size_t len) {
  if (offset % blocksize || len % blocksize)
    throw std::domain_error(
        "PageCrypter must operate at multiples of cipher block_size");
  std::unique_ptr<uint8_t[]> res(new uint8_t[len]);
  for (size_t i = 0; i < len; i += blocksize) {
    size_t blockno = (offset + i) / blocksize;
    for (size_t j = blocksize; j-- > 0; blockno >>= 8)
      res[i + j] = blockno & 0xff;
  }

  CipherCtx ctx{EVP_CIPHER_CTX_new()};
  if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, &key_[16], nullptr) !=
      1)
    crypto_raise("EVP_EncryptInit_ex(aes_128_ecb)");
  int outl;
  if (EVP_EncryptUpdate(ctx, res.get(), &outl, res.get(), len) != 1)
    crypto_raise("EVP_EncryptUpdate(aes_128_ecb)");

  return res;
}
