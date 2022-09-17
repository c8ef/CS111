#include <cstdio>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mcryptfile.hh"

static const char *data = "00000111112222233333444445555566666777778888899999";

// This file contains test cases for both of the projects in this
// directory (memory-mapped encrypted files, and page replacement).

//! Overwrites all of the bytes of a given page with distinct
//! identifying information including a label (which shouldn't be more
//! than 10-20 chars.) and the index of this page within the file
//! (page_index). The page will also contain an internal checksum.
void fill_page(char *page, const char *label, int page_index) {
  size_t msg_length =
      snprintf(page, page_size, "%s, page %d", label, page_index);

  // Fill in the rest of the page with character data.
  const char *src = data;
  for (size_t i = msg_length + 1; i < page_size; i++) {
    page[i] = *src;
    src++;
    if (*src == '\0') {
      src = data;
    }
  }

  // Compute a checksum such that all of the 32-bit words of the page
  // add up to 0, and place it in the last word of the page.
  int32_t *words = reinterpret_cast<int32_t *>(page);
  size_t num_words = page_size / sizeof(int32_t);
  int32_t sum = 0;
  for (size_t i = 0; i < (num_words - 1); i++) {
    sum += words[i];
  }
  words[num_words - 1] = -sum;
}

//! Given a page of memory that was filled by fill_page, this function
//! returns a printable string describing the page, including the label
//! and page_index passed to fill_page, plus the page's checksum, which
//! should be 0.
std::string page_signature(char *page) {
  // Compute the checksum of the page.
  int32_t *words = reinterpret_cast<int32_t *>(page);
  size_t num_words = page_size / sizeof(int32_t);
  int32_t sum = 0;
  for (size_t i = 0; i < num_words; i++) {
    sum += words[i];
  }

  // Make sure the page contains an initial null-terminated string
  // that isn't very long.
  int length;
  for (length = 0; length < 100; length++) {
    if (page[length] == '\0') {
      break;
    }
  }

  char buffer[200];
  snprintf(buffer, sizeof(buffer), "%.*s, checksum %d", length, page, sum);
  return std::string(buffer);
}

//! Create an encrypted file with a given number of pages. The (cleartext)
//! contents of each page are determined by fill_page.
void write_file(const char *name, int num_pages, const char *key) {
  CryptFile f(Key(key), name);
  char page[page_size];

  for (int i = 0; i < num_pages; i++) {
    fill_page(page, name, i);
    f.aligned_pwrite(page, page_size, i * page_size);
  }
}

//! Read a file whose pages were filled by fill_page and return all
//! of the page signatures (as determined by page_signature).
std::string read_file(const char *name, const char *key) {
  CryptFile f(Key(key), name);
  char page[page_size];
  std::string result;

  if ((f.file_size() % page_size) != 0) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer),
             "File %s has size %lu: not an "
             "even multiple of page size (%lu)",
             name, f.file_size(), page_size);
  }
  int num_pages = f.file_size() / page_size;
  if (num_pages == 0) {
    return "file empty";
  }
  for (int i = 0; i < num_pages; i++) {
    f.aligned_pread(page, page_size, i * page_size);
    if (!result.empty()) {
      result += "\n";
    }
    result += page_signature(page);
  }
  return result;
}

void read_test() {
  printf("Creating file with 3 pages\n");
  write_file("__test__", 3, "12345");
  MCryptFile f(Key("12345"), "__test__");
  char *p = f.map();
  printf("Mapped file; region has %lu bytes\n", f.map_size());
  printf("Reading memory-mapped pages\n");
  printf("Page 1 signature: %s\n", page_signature(p + page_size).c_str());
  printf("Page 0 signature: %s\n", page_signature(p).c_str());
  printf("Page 2 signature: %s\n", page_signature(p + 2 * page_size).c_str());
  printf("Paging I/O: %lu pages read, %lu pages written\n",
         f.pread_bytes / page_size, f.pwrite_bytes / page_size);
}

void write_test() {
  {
    printf("Mapping new file\n");
    MCryptFile f(Key("12345"), "__test__");
    char *p = f.map(3 * page_size);
    printf("Writing 3 memory-mapped pages\n");
    fill_page(p + 2 * page_size, "write_test", 2);
    fill_page(p, "write_test", 0);
    fill_page(p + page_size, "write_test", 1);
    printf("Destroying MCryptFile\n");
  }
  printf("Reading page signatures from file:\n%s\n",
         read_file("__test__", "12345").c_str());
}

void update_test() {
  printf("Creating file with 2 pages\n");
  write_file("__test__", 2, "99999");
  MCryptFile f(Key("99999"), "__test__");
  char *p = f.map();
  printf("Mapped file; region has %lu bytes\n", f.map_size());
  printf("Updating page 1\n");
  memmove(p + page_size + 2, "1234", 4);
  printf("Page signatures in file before flush:\n%s\n",
         read_file("__test__", "99999").c_str());
  f.flush();
  printf("Page signatures in file after flush:\n%s\n",
         read_file("__test__", "99999").c_str());
  printf("Paging I/O: %lu pages read, %lu pages written\n",
         f.pread_bytes / page_size, f.pwrite_bytes / page_size);
}

void extend_test() {
  std::size_t mapped_size = page_size * 100;
  printf("Creating file with 2 pages\n");
  write_file("__test__", 2, "99999");
  MCryptFile f(Key("99999"), "__test__");
  printf("Mapping with region size %lu\n", mapped_size);
  char *p = f.map(mapped_size);
  printf("Writing pages 2 and 3\n");
  fill_page(p + 2 * page_size, "new_info", 2);
  fill_page(p + 3 * page_size, "new_info", 3);
  printf("Syncing\n");
  f.flush();
  printf("Page signatures in file after flush:\n%s\n",
         read_file("__test__", "99999").c_str());
  printf("Paging I/O: %lu pages read, %lu pages written\n",
         f.pread_bytes / page_size, f.pwrite_bytes / page_size);
}

void multiple_writes_test() {
  printf("Creating file with 2 pages\n");
  write_file("__test__", 2, "99999");
  MCryptFile f(Key("99999"), "__test__");
  char *p = f.map();
  printf("Updating page 0\n");
  memmove(p + 2, "1111", 4);
  printf("Syncing\n");
  f.flush();
  printf("Updating page 0 again\n");
  memmove(p + 2, "2222", 4);
  printf("Syncing again\n");
  f.flush();
  printf("Page signatures in file after flush:\n%s\n",
         read_file("__test__", "99999").c_str());
  printf("Paging I/O: %lu pages read, %lu pages written\n",
         f.pread_bytes / page_size, f.pwrite_bytes / page_size);
}

void remap_test() {
  printf("Creating file with 2 pages\n");
  write_file("__test__", 2, "99999");
  MCryptFile f(Key("99999"), "__test__");
  printf("Mapping with default region size\n");
  char *p = f.map();
  printf("Updating page 0\n");
  memmove(p + 2, "1111", 4);
  std::size_t new_size = page_size * 3;
  printf("Unmapping (without flush), then remapping with region size %lu\n",
         new_size);
  f.unmap();
  p = f.map(new_size);
  printf("Writing (new) page 2\n");
  fill_page(p + 2 * page_size, "new_info", 2);
  printf("Syncing\n");
  f.flush();
  printf("Page signatures in file after flush:\n%s\n",
         read_file("__test__", "99999").c_str());
  printf("Paging I/O: %lu pages read, %lu pages written\n",
         f.pread_bytes / page_size, f.pwrite_bytes / page_size);
}

void three_files_test() {
  printf("Creating 3 files\n");
  write_file("__test__", 10, "11111");
  write_file("__test2__", 5, "22222");
  write_file("__test3__", 20, "33333");
  MCryptFile f(Key("11111"), "__test__");
  MCryptFile f2(Key("22222"), "__test2__");
  MCryptFile f3(Key("33333"), "__test3__");
  printf("Mapping with default region size\n");
  char *p = f.map();
  char *p2 = f2.map();
  char *p3 = f3.map();
  printf("Reading memory-mapped pages\n");
  printf("File 1 page 0 signature: %s\n", page_signature(p).c_str());
  printf("File 2 page 0 signature: %s\n", page_signature(p2).c_str());
  printf("File 3 page 10 signature: %s\n",
         page_signature(p3 + 10 * page_size).c_str());
  printf("Writing pages 0 and 4 in file 2\n");
  fill_page(p2, "new_page_0", 0);
  fill_page(p2 + 4 * page_size, "new_page_4", 4);
  printf("Syncing\n");
  f2.flush();
  printf("Reading page signatures from file:\n%s\n",
         read_file("__test2__", "22222").c_str());
}

void big_file_test() {
  printf("Setting memory size to 5 pages\n");
  MCryptFile::set_memory_size(5);
  printf("Creating file with 15 pages\n");
  write_file("__test__", 15, "12345");
  MCryptFile f(Key("12345"), "__test__");
  printf("Mapping with default region size\n");
  char *p = f.map();
  printf("Reading all page signatures from memory, in order\n");
  for (int i = 0; i < 15; i++) {
    printf("Page %d signature: %s\n", i,
           page_signature(p + i * page_size).c_str());
  }
  printf("Paging I/O: %lu pages read, %lu pages written\n",
         f.pread_bytes / page_size, f.pwrite_bytes / page_size);
}

void two_files_test() {
  printf("Setting memory size to 5 pages\n");
  MCryptFile::set_memory_size(5);
  printf("Creating 2 files with 10 pages each\n");
  write_file("__test__", 10, "11111");
  write_file("__test2__", 10, "22222");
  MCryptFile f(Key("11111"), "__test__");
  MCryptFile f2(Key("22222"), "__test2__");
  printf("Interleaved mapped accesses (single page in file 1, all pages in "
         "file 2)\n");
  volatile char *p = f.map();
  volatile char *p2 = f2.map();
  int sum1 = 0;
  int sum2 = 0;
  for (int i = 0; i < 30; i++) {
    char c1 = p[page_size + 15];
    char c2 = p2[(i % 10) * page_size + 16];
    sum1 += c1 - '0';
    sum2 += c2 - '0';
  }
  printf("Sum1 %d, sum2 %d\n", sum1, sum2);
  printf("Paging I/O for file 1: %lu pages read, %lu pages written\n",
         f.pread_bytes / page_size, f.pwrite_bytes / page_size);
  printf("Paging I/O for file 2: %lu pages read, %lu pages written\n",
         f2.pread_bytes / page_size, f2.pwrite_bytes / page_size);
}

void writeback_test() {
  printf("Setting memory size to 5 pages\n");
  MCryptFile::set_memory_size(5);
  printf("Creating 3 files with 10 pages each\n");
  write_file("__test__", 10, "11111");
  write_file("__test2__", 10, "22222");
  write_file("__test3__", 10, "33333");
  MCryptFile f(Key("11111"), "__test__");
  MCryptFile f2(Key("22222"), "__test2__");
  MCryptFile f3(Key("33333"), "__test3__");
  printf("Modifying page 1 of file 1\n");
  char *p = f.map();
  memmove(p + page_size + 2, "4444", 4);
  printf("Modifying page 2 of file 2\n");
  char *p2 = f2.map();
  memmove(p2 + 2 * page_size + 2, "5555", 4);
  printf("Modifying page 3 of file 3\n");
  char *p3 = f3.map();
  memmove(p3 + 3 * page_size + 2, "6666", 4);
  printf("Reading file 1 to flush all dirty pages\n");
  int sum = 0;
  for (int i = 1; i < 10; i++) {
    char c = p[i * page_size + 15];
    sum += c - '0';
  }
  printf("Sum of digits read: %d\n", sum);
  printf("Page signatures in file 2:\n%s\n",
         read_file("__test2__", "22222").c_str());
  printf("Rereading modified data\n");
  printf("File 1: %.4s, file 2: %.4s, file 3: %.4s\n", p + page_size + 2,
         p2 + 2 * page_size + 2, p3 + 3 * page_size + 2);
  printf("Paging I/O for file 1: %lu pages read, %lu pages written\n",
         f.pread_bytes / page_size, f.pwrite_bytes / page_size);
  printf("Paging I/O for file 2: %lu pages read, %lu pages written\n",
         f2.pread_bytes / page_size, f2.pwrite_bytes / page_size);
  printf("Paging I/O for file 3: %lu pages read, %lu pages written\n",
         f3.pread_bytes / page_size, f3.pwrite_bytes / page_size);
}

void random_test() {
  static const int VIRTUAL_PAGES = 20;
  static const int PHYSICAL_PAGES = 10;
  // The most recent value written in each page, used to check
  // results at the end.
  int last_written[20];

  srand(getpid() ^ time(NULL));
  printf("Setting memory size to %d pages\n", PHYSICAL_PAGES);
  MCryptFile::set_memory_size(PHYSICAL_PAGES);
  printf("Creating file with %d pages\n", VIRTUAL_PAGES);
  write_file("__test__", VIRTUAL_PAGES, "11111");
  MCryptFile f(Key("11111"), "__test__");
  printf("Accessing random pages, sometimes writing\n");
  volatile char *p = f.map();
  for (int page = 0; page < VIRTUAL_PAGES; page++) {
    last_written[page] = -1;
  }
  for (int i = 0; i < 10000; i++) {
    int page = rand() % VIRTUAL_PAGES;
    if (rand() & 1) {
      reinterpret_cast<volatile int *>(p + page * page_size)[2] = i;
      last_written[page] = i;
    } else {
      int actual = reinterpret_cast<volatile int *>(p + page * page_size)[2];
      if ((last_written[page] >= 0) && (last_written[page] != actual)) {
        printf("Error: expected value %d in page %i, but read %d\n", page,
               last_written[page], actual);
      }
    }
  }
  printf("Checking final values in pages\n");
  for (int page = 0; page < VIRTUAL_PAGES; page++) {
    int actual = reinterpret_cast<volatile int *>(p + page * page_size)[2];
    if ((last_written[page] >= 0) && (last_written[page] != actual)) {
      printf("Error: expected value %d in page %i, but read %d\n", page,
             last_written[page], actual);
    }
  }
  //    printf("Paging I/O : %lu pages read, %lu pages written\n",
  //            f.pread_bytes/page_size, f.pwrite_bytes/page_size);
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    unlink("__test__");
    unlink("__test2__");
    unlink("__test3__");

    // Tests for Project 5 (memory-mapped encrypted file)
    if (strcmp(argv[i], "read") == 0) {
      read_test();
    } else if (strcmp(argv[i], "write") == 0) {
      write_test();
    } else if (strcmp(argv[i], "update") == 0) {
      update_test();
    } else if (strcmp(argv[i], "extend") == 0) {
      extend_test();
    } else if (strcmp(argv[i], "multiple_writes") == 0) {
      multiple_writes_test();
    } else if (strcmp(argv[i], "remap") == 0) {
      remap_test();
    } else if (strcmp(argv[i], "three_files") == 0) {
      three_files_test();

      // Tests for Project 6 (page replacement)
    } else if (strcmp(argv[i], "big_file") == 0) {
      big_file_test();
    } else if (strcmp(argv[i], "two_files") == 0) {
      two_files_test();
    } else if (strcmp(argv[i], "writeback") == 0) {
      writeback_test();
    } else if (strcmp(argv[i], "random") == 0) {
      random_test();
    } else {
      printf("No test named '%s'; choices are:\n  read\n  write\n  "
             "update\n  extend\n  multiple_writes\n  remap\n "
             "three_files\n big_file\n two_files\n  random\n",
             argv[i]);
    }
    unlink("__test__");
    unlink("__test2__");
    unlink("__test3__");
  }
}
