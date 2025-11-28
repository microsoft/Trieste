#include "infix.h"

#include <trieste/driver.h>

int main(int argc, char** argv)
{
  using namespace trieste;
  return Driver(infix::reader() >>= infix::calculate()).run(argc, argv);
}
