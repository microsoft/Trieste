#include "infix.h"

#include <trieste/driver.h>

int main(int argc, char** argv)
{
  using namespace trieste;
  Reader read_and_calculate = infix::reader() >>= infix::calculate();
  return Driver(read_and_calculate).run(argc, argv);
}
