#include <trieste/driver.h>
#include <trieste/json.h>

int main(int argc, char** argv)
{
  trieste::Driver(trieste::json::reader()).run(argc, argv);
}
