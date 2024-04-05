#include <trieste/driver.h>
#include <trieste/json.h>

int main(int argc, char** argv)
{
  trieste::Driver driver(trieste::json::reader(), nullptr);
  return driver.run(argc, argv);
}
