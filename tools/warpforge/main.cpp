#include "driver/driver.hpp"

#include <iostream>

int main(int argc, char** argv) { return wf::driver::main(argc, argv, std::cout, std::cerr); }
