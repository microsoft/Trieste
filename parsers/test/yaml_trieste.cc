#include <trieste/driver.h>
#include <trieste/yaml.h>

int main(int argc, char** argv)
{
  trieste::Driver(trieste::yaml::reader()).run(argc, argv);
}
