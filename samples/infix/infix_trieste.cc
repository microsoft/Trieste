#include "infix.h"

#include <trieste/driver.h>

int main(int argc, char** argv)
{
  // TODO: config
  infix::Config config{
    .use_parser_tuples = false,
    .enable_tuples = true,
  };
  return trieste::Driver(infix::reader(config)).run(argc, argv);
}
