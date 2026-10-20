#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--version") {
      std::cout << "warpforge " << WARPFORGE_VERSION << "\n";
      return 0;
    }
  }
  std::cerr << "usage: warpforge <input.wf> [options]\n";
  return 2;
}
