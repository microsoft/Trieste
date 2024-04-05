#include <trieste/driver.h>
#include <trieste/yaml.h>

int main(int argc, char** argv)
{
  trieste::Driver driver(
    "yaml", nullptr, trieste::yaml::parser(), trieste::yaml::passes());
  return driver.run(argc, argv);
}
