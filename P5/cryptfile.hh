#pragma once

#include "crypto.hh"
#include "vm.hh"

// An interface to an encrypted file that transparently decrypts data
// on read and encrypts it on write.
class CryptFile {
public:
  // Block size of the underlying AES encryption algorithm (16 bytes)
  static constexpr std::size_t blocksize = PageCrypter::blocksize;

  // Opens file path using encryption key key.  Throws a
  // std::system_error if the file cannot be opened.
  CryptFile(Key key, std::string path);
  virtual ~CryptFile();

  // Return current size of underlying file
  std::size_t file_size();

  // Read and decrypt data from the file at position offset.  Both
  // len and offset must be multiples of blocksize.
  int aligned_pread(void *dst, std::size_t len, std::size_t offset);

  // Encrypt and write data to the file at position offset.  Both len
  // and offset must be multiples of blocksize.
  int aligned_pwrite(const void *src, std::size_t len, std::size_t offset);

  // I/O statistics (for tests).
  int pread_bytes;
  int pwrite_bytes;

protected:
  unique_fd fd_;      // fd for file containing ciphertext
  PageCrypter crypt_; // Encryption/decryption state
};
