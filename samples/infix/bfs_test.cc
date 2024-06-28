#include "bfs.h"

#include <iostream>

namespace
{
  using R = bfs::Result<bfs::CatString>;

  std::pair<R, R> list_of_up_to_acc(std::function<R()> fn, int count)
  {
    if (count == 0)
    {
      return {R{}, R{std::string("")}};
    }
    else
    {
      auto [smaller, current] = list_of_up_to_acc(fn, count - 1);
      return {
        smaller.concat(current),
        fn().flat_map<bfs::CatString>([current](auto suffix) {
          return current.map<bfs::CatString>(
            [suffix](auto elem) { return elem.concat(suffix); });
        }),
      };
    }
  }

  R list_of_up_to(std::function<R()> fn, int count)
  {
    auto [smaller, current] = list_of_up_to_acc(fn, count);
    return smaller.concat(current);
  }
}

// FIXME: this isn't really a test, just a quick check things make sense
int main()
{
  R combinations = list_of_up_to(
    []() {
      return R(std::string("^")).concat([]() { return R(std::string("!")); });
    },
    3);

  for (auto elem : combinations)
  {
    std::cout << elem.str() << std::endl;
  }
}
