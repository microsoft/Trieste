/**
 * Minimal vcpkg integration test for the trieste header-only target.
 * Verifies that find_package(trieste) + trieste::trieste works and
 * that the version header is accessible.
 */
#include <trieste/trieste.h>
#include <trieste/version.h>

#include <cstdio>

int main()
{
  std::printf("Trieste %s\n", TRIESTE_VERSION);

  // Smoke-test: create a trivial AST node to verify linkage.
  auto node = trieste::NodeDef::create(trieste::Top);
  if (node == nullptr)
    return 1;

  return 0;
}
