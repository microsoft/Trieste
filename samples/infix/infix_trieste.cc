#include "infix.h"

#include <trieste/driver.h>

int main(int argc, char** argv)
{
  return trieste::Driver(infix::reader()).run(argc, argv);
}
