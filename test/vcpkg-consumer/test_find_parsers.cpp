/**
 * vcpkg integration test for the trieste[parsers] feature.
 * Verifies that trieste::json and trieste::yaml targets are usable.
 */
#include <trieste/json.h>
#include <trieste/yaml.h>

#include <cstdio>

int main()
{
  auto jr = trieste::json::reader();
  auto yr = trieste::yaml::reader();
  std::printf("JSON and YAML parsers available\n");
  return 0;
}
