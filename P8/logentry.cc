#include <array>
#include <functional>
#include <map>
#include <sstream>
#include <type_traits>

#include "logentry.hh"

uint32_t crc32(const void *_buf, size_t len, uint32_t crc) {
  static const std::array<uint32_t, 256> table = []() {
    static constexpr uint32_t poly = 0x04C11DB7;
    std::array<uint32_t, 256> res;
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t crc = i << 24;
      for (int j = 0; j < 8; ++j)
        crc = crc << 1 ^ (crc & 0x8000'0000 ? poly : 0);
      res[i] = crc;
    }
    return res;
  }();

  const uint8_t *p = static_cast<const uint8_t *>(_buf);
  while (len > 0) {
    --len;
    uint8_t temp = *p++ ^ crc >> 24;
    crc = table[temp] ^ crc << 8;
  }
  return crc;
}

std::string hexdump(const void *_p, size_t len) {
  static constexpr char hexdigits[] = "0123456789abcdef";
  const uint8_t *p = static_cast<const uint8_t *>(_p), *e = p + len;
  std::string res;
  res.reserve(2 * len);
  for (; p != e; ++p) {
    res += hexdigits[*p >> 4 & 0xf];
    res += hexdigits[*p & 0xf];
  }
  return res;
}

namespace {

inline void mustread(Reader &r, void *buf, size_t n) {
  if (!r.tryread(buf, n))
    throw log_corrupt("premature EOF");
}

template <typename I, typename T = void>
using integral_t = std::enable_if_t<std::is_integral_v<I>, T>;

struct WithBytes {
  std::function<void(uint8_t *, size_t)> f_;
  WithBytes(decltype(f_) f) : f_(std::move(f)) {}

  template <typename I> integral_t<I> operator()(const char *, I &i) const {
    f_(reinterpret_cast<uint8_t *>(&i), sizeof(i));
  }

  template <typename I>
  integral_t<I> operator()(const char *, std::vector<I> &v) const {
    if (v.size() > 0xff)
      throw std::length_error("Maximum LogEntry vector size exceeded");
    uint8_t len = v.size();
    f_(reinterpret_cast<uint8_t *>(&len), sizeof(len));
    v.resize(len);
    f_(reinterpret_cast<uint8_t *>(v.data()), v.size());
  }
};

template <typename E> size_t entry_nbytes(const E &e) {
  size_t res = 0;
  const_cast<E &>(e).archive(
      WithBytes([&res](uint8_t *p, size_t n) { res += n; }));
  return res;
}

template <typename E> void entry_read(Reader &r, E &e) {
  e.archive(WithBytes([&r](uint8_t *p, size_t n) { mustread(r, p, n); }));
}

template <typename E> void entry_write(Writer &w, const E &e) {
  const_cast<E &>(e).archive(
      WithBytes([&w](uint8_t *p, size_t n) { w.write(p, n); }));
}

inline void value_show(std::ostream &os, uint32_t u) { os << u; }

inline void value_show(std::ostream &os, const std::vector<std::uint8_t> &v) {
  os << hexdump(v.data(), v.size());
}

template <typename I>
inline integral_t<I> value_show(std::ostream &os, const std::vector<I> &v) {
  for (I i : v)
    os << " " << i;
}

template <typename E> void entry_show(std::ostream &os, const E &e) {
  os << "  " << e.type() << "\n";
  const_cast<E &>(e).archive([&os](const char *field, const auto &val) {
    os << "    " << field << ": ";
    value_show(os, val);
    os << "\n";
  });
}

} // anonymous namespace

void LogEntry::save(Writer &w) const {
  struct CrcWriter : Writer {
    Writer &next_;
    uint32_t crc_;
    CrcWriter(Writer &next, uint32_t crc = LOG_CRC_SEED)
        : next_(next), crc_(crc) {}
    void write(const void *buf, std::size_t len) override {
      next_.write(buf, len);
      crc_ = crc32(buf, len, crc_);
    }
  };

  CrcWriter cw(w);
  Header h{sequence_, uint8_t(entry_.index())};
  entry_write(cw, h);
  visit([&cw](const auto &t) { entry_write(cw, t); });
  Footer f{cw.crc_, sequence_};
  entry_write(w, f);
}

namespace detail {

template <typename V, size_t I> void indexed_emplace(V &v) {
  v.template emplace<I>();
}

template <typename V, size_t... Is>
constexpr std::array<void (*)(V &), sizeof...(Is)>
make_variant_initializers(std::index_sequence<Is...>) {
  return {indexed_emplace<V, Is>...};
}

} // namespace detail

template <typename... Ts>
inline void set_variant_index(std::variant<Ts...> &v, const size_t i) {
  static constexpr auto initializers =
      detail::make_variant_initializers<std::variant<Ts...>>(
          std::make_index_sequence<sizeof...(Ts)>{});
  if (i >= initializers.size())
    throw log_corrupt("invalid variant index");
  initializers[i](v);
}

bool LogEntry::load(Reader &r) {
  struct CrcReader : Reader {
    Reader &next_;
    uint32_t crc_;
    CrcReader(Reader &next, uint32_t crc = LOG_CRC_SEED)
        : next_(next), crc_(crc) {}
    bool tryread(void *buf, std::size_t len) override {
      if (next_.tryread(buf, len)) {
        crc_ = crc32(buf, len, crc_);
        return true;
      }
      return false;
    }
  };

  CrcReader cr(r);
  Header h;
  entry_read(cr, h);
  set_variant_index(entry_, h.type);
  sequence_ = h.sequence;
  visit([&cr](auto &t) { entry_read(cr, t); });
  Footer f;
  entry_read(r, f);
  if (h.sequence != f.sequence)
    throw log_corrupt("sequence number mismatch");
  if (f.checksum != cr.crc_)
    throw log_corrupt("bad checksum");
  return true;
}

std::string LogEntry::show(const filsys *sb) const {
  std::ostringstream res;
  res << "* LSN " << sequence_ << "\n";
  visit([&res](const auto &e) { entry_show(res, e); });
  if (sb)
    if (const LogPatch *ep = get<LogPatch>())
      res << "  " << what_patch(*sb, *ep) << "\n";
  return res.str();
}

size_t LogEntry::nbytes() const {
  return sizeof(Header) + visit([](auto &e) { return entry_nbytes(e); }) +
         sizeof(Footer);
}

std::string what_data_patch(const LogPatch &e) {
  std::ostringstream res;
  if (e.bytes.size() == sizeof(direntv6)) {
    direntv6 de;
    memcpy(&de, e.bytes.data(), sizeof(de));
    res << "dirent (" << de.d_inumber << ", \"" << de.name() << "\")";
  } else if (e.bytes.size() == sizeof(uint16_t)) {
    uint16_t blockno;
    memcpy(&blockno, e.bytes.data(), sizeof(blockno));
    res << "block pointer " << blockno;
  } else if (e.offset_in_block == 0 &&
             e.bytes.size() == sizeof(inode::i_addr) + 1) {
    // The + 1 in the size is a hack in Inode::make_large so we
    // can differentiate this case from a directory entry above.
    res << "block pointers";
    std::array<uint16_t, IADDR_SIZE> ptrs;
    memcpy(ptrs.data(), e.bytes.data(), sizeof(uint16_t) * ptrs.size());
    for (uint16_t bn : ptrs)
      res << " " << bn;
  } else
    res << "unknown data patch";
  return res.str();
}

std::string what_inode_patch(const LogPatch &e) {
#define FIELD(field)                                                           \
  { offsetof(inode, field), #field }
  static const std::map<int, const char *> ifields{
      FIELD(i_mode),  FIELD(i_nlink), FIELD(i_uid),   FIELD(i_size0),
      FIELD(i_size1), FIELD(i_addr),  FIELD(i_atime), FIELD(i_mtime),
  };
#undef FIELD
  std::ostringstream res;
  uint16_t inum = 1 + (e.blockno - INODE_START_SECTOR) * INODES_PER_BLOCK +
                  e.offset_in_block / sizeof(inode);
  res << "inode #" << inum << " (";
  if (e.bytes.size() >= sizeof(inode))
    res << "whole inode";
  else {
    size_t s = e.offset_in_block % sizeof(inode);
    if (e.bytes.size() == 2 && !(s & 1) && s >= offsetof(inode, i_addr) &&
        s < offsetof(inode, i_addr[IADDR_SIZE])) {
      uint16_t blockno;
      memcpy(&blockno, e.bytes.data(), sizeof(blockno));
      res << "i_addr[" << ((s - offsetof(inode, i_addr)) / 2)
          << "] = block pointer " << blockno;
    } else {
      bool need_comma = false;
      for (auto i = ifields.lower_bound(s),
                end = ifields.lower_bound(s + e.bytes.size());
           i != end; ++i) {
        if (need_comma)
          res << ", ";
        else
          need_comma = true;
        res << i->second;
      }
    }
  }
  res << ")";
  return res.str();
}

std::string what_patch(const filsys &sb, const LogPatch &e) {
  if (e.blockno >= sb.datastart())
    return what_data_patch(e);
  else if (e.blockno >= INODE_START_SECTOR)
    return what_inode_patch(e);
  else
    return "superblock/bootblock patch?";
}
