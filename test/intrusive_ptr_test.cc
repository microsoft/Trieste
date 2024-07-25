#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <trieste/intrusive_ptr.h>

struct Dummy
: public trieste::
    intrusive_refcounted<Dummy, trieste::intrusive_ptr_threading::async>
{
  size_t tag;

  Dummy(size_t tag_) : tag{tag_} {}
};

using ptr_t = trieste::intrusive_ptr<Dummy>;
using ActionFn = ptr_t(ptr_t);

std::vector<ActionFn*> actions{
  [](ptr_t ptr) -> ptr_t {
    if (ptr == nullptr)
    {
      std::cout << "Should only be setting to nullptr once per thread!"
                << std::endl;
      std::abort();
    }
    return nullptr; // dec_ref on this ptr
  },
  [](ptr_t ptr) {
    auto tmp = std::move(ptr);
    return tmp;
  },
  [](ptr_t ptr) {
    auto tmp = ptr;
    return ptr;
  },
  [](ptr_t ptr) {
    auto& alias = ptr;
    alias = ptr;
    return ptr;
  },
};

struct Behavior
{
  size_t action_idx;
  size_t ptr_idx;

  bool operator<(const Behavior& other) const
  {
    return std::pair{action_idx, ptr_idx} <
      std::pair{other.action_idx, other.ptr_idx};
  }
};

struct Test
{
  size_t ptr_count;
  std::vector<std::vector<Behavior>> thread_behaviors;

  void run() const
  {
    // Each thread gets its own copy of an array of N pointers, where every
    // thread shares refcounts with every other thread.
    std::vector<std::vector<ptr_t>> ptrs_per_thread;
    ptrs_per_thread.emplace_back();
    for (size_t i = 0; i < ptr_count; ++i)
    {
      ptrs_per_thread.front().push_back(ptr_t{new Dummy{i}});
    }
    while (ptrs_per_thread.size() < thread_behaviors.size())
    {
      ptrs_per_thread.push_back(ptrs_per_thread.back());
    }

    std::vector<std::thread> threads;
    for (size_t i = 0; i < thread_behaviors.size(); ++i)
    {
      threads.emplace_back([&, i]() {
        for (auto& behavior : thread_behaviors.at(i))
        {
          auto& ptr = ptrs_per_thread.at(i).at(behavior.ptr_idx);
          ptr = actions[behavior.action_idx](ptr);
        }
      });
    }

    for (auto& thread : threads)
    {
      thread.join();
    }

    // Sanity check: every thread should be setting their ptr to nullptr at some
    // point
    for (const auto& ptrs : ptrs_per_thread)
    {
      for (const auto& ptr : ptrs)
      {
        if (ptr != nullptr)
        {
          std::cout << "non-null ptr!" << std::endl;
          std::abort();
        }
      }
    }
  }
};

std::vector<Test>
build_tests(size_t ptr_count, size_t thread_count, size_t permutations)
{
  std::vector<Behavior> all_behaviors;
  for (size_t action_idx = 0; action_idx < actions.size(); ++action_idx)
  {
    for (size_t ptr_idx = 0; ptr_idx < ptr_count; ++ptr_idx)
    {
      all_behaviors.push_back({
        action_idx,
        ptr_idx,
      });
    }
  }

  std::vector<Test> tests = {{ptr_count, {}}};
  for (size_t i = 0; i < thread_count; ++i)
  {
    std::vector<Test> next_tests;
    for (const auto& test : tests)
    {
      // Allow adding some extra permutations if you think you're stuck at the
      // first few.
      for (size_t permutation_idx = 0; permutation_idx < permutations;
           ++permutation_idx)
      {
        auto mod_test = test;
        mod_test.thread_behaviors.push_back(all_behaviors);
        next_tests.push_back(mod_test);

        // Unconditionally permute the behaviors. We're not looking for total
        // coverage, just variety.
        std::next_permutation(all_behaviors.begin(), all_behaviors.end());
      }
    }
    tests = next_tests;
  }
  return tests;
}

// The intention of this test is to do a lot of work to refcounts, while under
// some kind of thread sanitizer. Changing the tag on Dummy from async to sync
// should make Clang's thread sanitizer unhappy, for instance, whereas if the
// tag is async then everything _should_ be fine.
int main()
{
  // Be very careful when increasing these numbers... they can quickly eat up
  // your memory and time.
  auto tests = build_tests(3, 6, 4);
  std::cout << "Found " << tests.size() << " permutations." << std::endl;

  for (auto test : tests)
  {
    test.run();
  }

  std::cout << "Ran " << tests.size() << " permutations." << std::endl;
  return 0;
}
