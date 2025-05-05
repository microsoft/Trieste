#include "infix.h"

#include <trieste/driver.h>

int main(int argc, char** argv)
{
  infix::Config config;
  config.enable_tuples = true;
  config.sanity();
  return trieste::Driver(infix::reader(config)).run(argc, argv);
}
