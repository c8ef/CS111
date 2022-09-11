#include <cctype>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// A redirect specifies that file descriptor fd should be connected to
// file path, which should be opened with flags.  For an input redirect
// (standard input, file descriptor 0), flags will be O_RDONLY.  For
// an output redirect (standard output, file descriptor 1), flags
// should be O_WRONLY|O_CREAT|O_TRUNC (which means open the file for
// writing, create it if the file doesn't exist, and truncate the file
// if it already exists).  Note that output files should be created
// with mode 0666 (which the kernel will adjust with the process's
// umask).
//
// Note that a command may have multiple redirects for the same file
// descriptor.  In that case each subsequent redirect will overwrite
// the previous one.  E.g., "echo hello > file1 > file2" will create
// an empty file1 and a file2 with contents "hello".  When in doubt,
// you can test redirect behavior in bash, as this shell should be
// similar.
struct redirect {
  const int fd;
  const std::string path;
  const int flags;
  redirect(int f, const std::string p, mode_t fl) : fd(f), path(p), flags(fl) {}
};

// A single command to be executed.
struct cmd {
  std::vector<std::string> args;
  std::vector<redirect> redirs;
};

// A pipeline is series of commands.  Unless overriden by I/O redirections,
// standard output of command [n] should be connected to standard input of
// command [n+1] using a pipe.  Standard input of the first command and
// standard output of the last command should remain unchanged, unless
// they are redirected.
//
// As with bash, file redirections should take precedence over pipes.
// For example "echo test > out | cat" should produce no output but
// write "test" to file out.
using pipeline = std::vector<cmd>;

// This method is invoked after a command line has been parsed; pl describes
// the subprocesses that must be created, along with any I/O redirections.
// This method invokes the subprocesses and waits for them to complete
// before it returns.
void run_pipeline(pipeline pl) {
  // You have to implement this function
  std::cerr << "run_pipeline function hasn't been implemented" << std::endl;
}

inline bool isspecial(char c) {
  return ((c == '>') || (c == '<') || (c == '|'));
}

// Parses one line of input and returns a vector describing each command
// in the pipeline. The newline should be removed from the input line;
// an empty result indicates a parsing error.
pipeline parse(char *line) {
  pipeline result(1);

  // A non-zero value (either '<' or '>') indicates that the previous
  // token was that redirection character, and the next token better be
  // a file name.
  char redirect = 0;

  // Each iteration through the following loop processes one token from
  // the line (either a special character such as '>' or a word of
  // non-special characters).
  char *p = line;
  while (*p) {
    while (isspace(*p))
      p++;
    if (!*p)
      break;
    if (isspecial(*p)) {
      if (redirect) {
        std::cerr << "missing file name for " << redirect << " redirection"
                  << std::endl;
        return {};
      }
      if (*p == '|') {
        if (result.back().args.empty()) {
          std::cerr << "missing command for pipeline" << std::endl;
          return {};
        }
        result.emplace_back();
      } else
        redirect = *p;
      p++;
      continue;
    }

    // At this point we know we're processing a word or file name.
    char *end = p + 1;
    while (*end && !isspecial(*end) && !isspace(*end))
      end++;
    std::string word(p, end - p);
    if (redirect) {
      if (redirect == '<')
        result.back().redirs.emplace_back(0, std::move(word), O_RDONLY);
      else
        result.back().redirs.emplace_back(1, std::move(word),
                                          O_CREAT | O_WRONLY | O_TRUNC);
      redirect = 0;
    } else
      result.back().args.push_back(std::move(word));
    p = end;
  }

  if (redirect) {
    std::cerr << "missing file name for " << redirect << " redirection"
              << std::endl;
    return {};
  }
  if (result.back().args.empty()) {
    if (result.size() > 1)
      std::cerr << "missing final command for pipeline" << std::endl;
  }
  return result;
}

int main() {
  const std::string prompt = "sh111$ ";
  std::string line;
  while (true) {
    if (isatty(0))
      std::cout << prompt;
    if (!std::getline(std::cin, line))
      exit(0);
    pipeline pl = parse(line.data());
    if (!pl.empty())
      run_pipeline(std::move(pl));
  }
}
