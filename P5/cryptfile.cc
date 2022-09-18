#include <algorithm>
#include <cstdio>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cryptfile.hh"

using std::size_t;
using std::uint8_t;

using Buffer = std::unique_ptr<uint8_t[]>;

CryptFile::CryptFile(Key key, std::string path)
    : pread_bytes(0), pwrite_bytes(0),
      fd_(open(path.c_str(), O_RDWR | O_CREAT, 0666)) {
  crypt_.key_ = std::move(key);
  if (fd_ == -1)
    threrror(path.c_str());
}

CryptFile::~CryptFile() {}

std::size_t CryptFile::file_size() {
  struct stat sb;
  if (fstat(fd_, &sb) == -1)
    threrror("fstat");
  // On 32-bit systems, off_t could be larger than size_t
  assert(sb.st_size >= 0);
  if ((typename std::make_unsigned<decltype(sb.st_size)>::type)sb.st_size >
      std::numeric_limits<std::size_t>::max())
    throw std::overflow_error("file size too large to fit in size_t");
  return sb.st_size;
}

int CryptFile::aligned_pread(void *dst, size_t len, size_t offset) {
  Buffer buf(new uint8_t[len]);
  int n = ::pread(fd_, buf.get(), len, offset);
  if (n <= 0)
    return n;
  n -= n % blocksize;
  // I have no idea what is happening here
  // without '+ 16', the last 16 bytes will not be decrypted
  crypt_.decrypt(static_cast<uint8_t *>(dst), buf.get(), n + 16, offset);
  pread_bytes += n;
  return n;
}

int CryptFile::aligned_pwrite(const void *src, size_t len, size_t offset) {
  Buffer buf(new uint8_t[len]);
  crypt_.encrypt(buf.get(), static_cast<const uint8_t *>(src), len, offset);
  pwrite_bytes += len;
  return ::pwrite(fd_, buf.get(), len, offset);
}
