// wf-gen: write seeded random WarpForge programs to disk for the GPU fuzz.
//
//   wf-gen <first-seed> <count> <out-dir>   ->  <out-dir>/fuzz_<seed>.wf

#include "support/program_generator.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: wf-gen <first-seed> <count> <out-dir>\n";
    return 2;
  }
  uint32_t first = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
  uint32_t count = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
  std::filesystem::path dir = argv[3];
  std::filesystem::create_directories(dir);
  for (uint32_t seed = first; seed < first + count; ++seed) {
    std::ofstream out(dir / ("fuzz_" + std::to_string(seed) + ".wf"), std::ios::binary);
    out << "// seed " << seed << "\n" << wf::test::ProgramGenerator(seed).generate();
  }
  return 0;
}
