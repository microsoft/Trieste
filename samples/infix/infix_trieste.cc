#include <trieste/driver.h>
#include "infix.h"

int main(int argc, char** argv)
{
  return trieste::Driver(infix::reader()).run(argc, argv);
}
