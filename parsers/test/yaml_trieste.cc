#include <trieste/driver.h>
#include <trieste/yaml.h>

int main(int argc, char** argv)
{
  trieste::Driver driver(trieste::yaml::reader(), nullptr);
  return driver.run(argc, argv);
}
