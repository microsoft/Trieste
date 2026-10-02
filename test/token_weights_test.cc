#include <iostream>
#include <trieste/trieste.h>

using namespace trieste;

namespace
{
  inline const auto WeightA = TokenDef("test-weight-a");
  inline const auto WeightB = TokenDef("test-weight-b");

  bool check_wf_token_weights()
  {
    auto ok = true;
    auto gloc = [](Rand&, Node) { return Location(); };
    std::vector<Token> choices = {WeightA, WeightB};

    // Zero-weight tokens should never be selected when a positive candidate
    // exists in the probabilistic branches.
    {
      auto g = wf::Gen(
                 wf::TokenTerminalDistance{{WeightA, 1}, {WeightB, 1}},
                 gloc,
                 123,
                 5,
                 {},
                 false)
                 .token_weights(wf::TokenWeights{{WeightA, 0}, {WeightB, 5}});

      for (size_t i = 0; i < 64; i++)
      {
        if (g.choose(choices, 0, Top) != WeightB)
        {
          std::cout << "wf_token_weights: selected zero-weight token at depth 0"
                    << std::endl;
          ok = false;
          break;
        }
      }
    }

    // All-zero weights are invalid at a choice point.
    {
      auto g = wf::Gen(
                 wf::TokenTerminalDistance{{WeightA, 1}, {WeightB, 1}},
                 gloc,
                 321,
                 5,
                 {},
                 false)
                 .token_weights(wf::TokenWeights{{WeightA, 0}, {WeightB, 0}});

      bool threw = false;
      try
      {
        (void)g.choose(choices, 0, Top);
      }
      catch (const std::runtime_error&)
      {
        threw = true;
      }

      if (!threw)
      {
        std::cout << "wf_token_weights: expected throw for all-zero weights"
                  << std::endl;
        ok = false;
      }
    }

    // A singleton choice with zero weight should also fail.
    {
      auto g =
        wf::Gen(
          wf::TokenTerminalDistance{{WeightA, 1}}, gloc, 111, 5, {}, false)
          .token_weights(wf::TokenWeights{{WeightA, 0}});

      bool threw = false;
      try
      {
        (void)g.choose(std::vector<Token>{WeightA}, 0, Top);
      }
      catch (const std::runtime_error&)
      {
        threw = true;
      }

      if (!threw)
      {
        std::cout
          << "wf_token_weights: expected throw for zero-weight singleton"
          << std::endl;
        ok = false;
      }
    }

    // Ceiling fallback remains distance-driven, ignoring token weights.
    {
      auto g = wf::Gen(
                 wf::TokenTerminalDistance{{WeightA, 0}, {WeightB, 10}},
                 gloc,
                 222,
                 1,
                 {},
                 false)
                 .token_weights(wf::TokenWeights{{WeightA, 0}, {WeightB, 100}});

      if (g.choose(choices, 2, Top) != WeightA)
      {
        std::cout << "wf_token_weights: ceiling fallback ignored distance"
                  << std::endl;
        ok = false;
      }
    }

    return ok;
  }
}

int main()
{
  if (!check_wf_token_weights())
    return 1;

  std::cout << "All WF token weight tests passed" << std::endl;
  return 0;
}
