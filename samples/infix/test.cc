#include "trieste/token.h"

#include <sstream>
#include <trieste/trieste.h>

using namespace trieste;

const auto Lhs = TokenDef("lhs");
const auto Rhs = TokenDef("rhs");

const auto Foo = TokenDef("foo", flag::print);
const auto Bar = TokenDef("bar", flag::print);
const auto Ping = TokenDef("ping", flag::print);

const auto Nom = TokenDef("nom", flag::print);

const auto AnyTok = Foo | Bar | Ping | Nom;

// an over-permissive WF so we can focus on the example
// clang-format off
const auto wf =
  (Top <<= AnyTok)
  | (Foo <<= (AnyTok)++)
  | (Bar <<= (AnyTok)++)
  | (Ping <<= (AnyTok)++)
  ;
// clang-format on

PassDef pass(dir::flag flags)
{
  return {
    "pass",
    ::wf,
    flags,
    {
      In(Ping) * (T(Bar)[Lhs] * (T(Foo)[Foo] << End) * T(Bar)[Rhs]) >>
        [](Match& _) { return _(Foo) << _(Lhs) << _(Rhs); },

      (T(Foo) << End) >> [](Match& _) { return Nom ^ "nom-foo"; },
    }};
}

Rewriter rewriter(dir::flag flags)
{
  return {
    "rewriter",
    {
      pass(flags),
    },
    ::wf,
  };
}

int main()
{
  auto input = Top << (Ping << (Bar ^ "1") << (Foo ^ "2") << (Bar ^ "3"));

  std::cout << "Input:" << std::endl << input << std::endl;

  for (auto flags :
       {dir::bottomup,
        dir::topdown,
        dir::bottomup | dir::once,
        dir::topdown | dir::once})
  {
    auto flags_to_str = [](dir::flag flag) {
      std::ostringstream out;
      if (flag & dir::bottomup)
      {
        out << "bottomup";
      }
      if (flag & dir::topdown)
      {
        out << "topdown";
      }
      if (flag & dir::once)
      {
        out << " | once";
      }
      return out.str();
    };

    std::cout << "flags: " << flags_to_str(flags) << std::endl;
    auto output = input >> rewriter(flags);
    std::cout << output.ast << std::endl;
  }
  return 0;
}
