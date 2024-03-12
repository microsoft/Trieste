#include <trieste/driver.h>
#include <trieste/json.h>

int main(int argc, char** argv)
{
  trieste::Driver driver(
    "json", nullptr, trieste::json::parser(), trieste::json::passes());
  return driver.run(argc, argv);
}
