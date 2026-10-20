#include "driver/driver.hpp"

#include <iostream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
  // Write LF, not CRLF: dumps and PTX must be byte-identical on every OS.
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  return wf::driver::main(argc, argv, std::cout, std::cerr);
}
