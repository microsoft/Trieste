// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <iostream>
#include <trieste/trieste.h>

using namespace trieste;

namespace
{
  inline const auto Root = TokenDef("rewrite_test.Root");
  inline const auto A = TokenDef("rewrite_test.A");
  inline const auto B = TokenDef("rewrite_test.B");
  inline const auto C = TokenDef("rewrite_test.C");
  inline const auto Matched = TokenDef("rewrite_test.Matched");

  bool test_not_sequence()
  {
    PassDef pass{
      "not-sequence",
      wf::empty,
      dir::once,
      {(!T(A) * T(B)) >> [](Match&) -> Node { return Matched; }}};

    Node root = Root << C << B;
    const auto [result, iterations, changes] = pass.run(root);

    if (
      (iterations != 1) || (changes != 1) || (result->size() != 1) ||
      (result->front()->type() != Matched))
    {
      std::cerr << "A sequence beginning with !Pattern was not rewritten:"
                << std::endl
                << result << std::endl;
      return false;
    }

    return true;
  }
}

int main()
{
  return test_not_sequence() ? 0 : 1;
}
