#include "bfs.h"

#include <iostream>

using namespace std::string_view_literals;

namespace
{
  using R = bfs::Result<bfs::CatString>;

  std::pair<R, R> list_of_up_to_acc(std::function<R()> fn, int count)
  {
    if (count == 0)
    {
      return {R{}, R{""sv}};
    }
    else
    {
      bfs::Result<bfs::CatString> smaller, current;
      std::tie(smaller, current) = list_of_up_to_acc(fn, count - 1);
      return {
        smaller.or_(current),
        fn().flat_map<bfs::CatString>([=](auto suffix) {
          return current.map<bfs::CatString>(
            [=](auto elem) { return elem.concat(suffix); });
        }),
      };
    }
  }

  R list_of_up_to(std::function<R()> fn, int count)
  {
    auto [smaller, current] = list_of_up_to_acc(fn, count);
    return smaller.or_(current);
  }
}

// this isn't really a test, just a quick check things make sense
int main()
{
  R combinations = list_of_up_to([]() { return R("^"sv).or_(R("!"sv)); }, 3);

  for (auto elem : combinations)
  {
    std::cout << elem.str() << std::endl;
  }
}
