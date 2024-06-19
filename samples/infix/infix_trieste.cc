#include "infix.h"

#include <trieste/driver.h>

int main(int argc, char** argv)
{
  // TODO: config
  infix::Config config;
  config.enable_tuples = true;
  config.use_parser_tuples = false;
  config.tuples_require_parens = true;
  config.sanity();
  return trieste::Driver(infix::reader(config)).run(argc, argv);
}
