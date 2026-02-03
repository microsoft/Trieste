// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cassert>
#include <iostream>
#include <trieste/nodeworker.h>

using namespace trieste;

// Test tokens for our test nodes
inline const auto TestNode = TokenDef("nodeworker_test.TestNode");
inline const auto TestLeaf = TokenDef("nodeworker_test.TestLeaf");

// ============================================================================
// Unified Work class for all tests
// ============================================================================

enum class BlockMode
{
  Single, // Use block_on for single dependency
  All,    // Use block_on_all for multiple dependencies
  Any,    // Use block_on_any for multiple dependencies
};

struct TestWork
{
  struct State : NodeWorkerState
  {
    int seed_count{0};
    int process_count{0};
    int resolve_order{-1};
    std::string custom_data;
  };

  // Configuration: maps nodes to their dependencies
  NodeMap<std::vector<Node>> dependencies;

  // How to block when there are multiple dependencies
  BlockMode block_mode{BlockMode::Single};

  // Whether to automatically depend on children
  bool process_children{false};

  // Counter for tracking resolution order
  int order_counter{0};

  void seed(const Node&, State& s)
  {
    s.seed_count++;
  }

  bool process(const Node& n, NodeWorker<TestWork>& worker)
  {
    auto& s = worker.state(n);
    s.process_count++;

    // Get dependencies for this node
    std::vector<Node> deps;
    auto it = dependencies.find(n);
    if (it != dependencies.end())
    {
      deps = it->second;
    }
    else if (process_children && !n->empty())
    {
      deps.assign(n->begin(), n->end());
    }

    // If no dependencies, resolve immediately
    if (deps.empty())
    {
      s.resolve_order = order_counter++;
      return true;
    }

    // Check if dependencies are resolved based on block mode
    bool can_resolve = false;
    switch (block_mode)
    {
      case BlockMode::Single:
      case BlockMode::All:
      {
        // Need all dependencies resolved
        can_resolve = true;
        for (const auto& dep : deps)
        {
          if (!worker.is_resolved(dep))
          {
            can_resolve = false;
            break;
          }
        }
        break;
      }
      case BlockMode::Any:
      {
        // Need any dependency resolved
        can_resolve = false;
        for (const auto& dep : deps)
        {
          if (worker.is_resolved(dep))
          {
            can_resolve = true;
            break;
          }
        }
        break;
      }
    }

    if (!can_resolve)
    {
      // Block on dependencies
      switch (block_mode)
      {
        case BlockMode::Single:
          worker.block_on(n, deps[0]);
          break;
        case BlockMode::All:
          worker.block_on_all(n, deps);
          break;
        case BlockMode::Any:
          worker.block_on_any(n, deps);
          break;
      }
      return false;
    }

    s.resolve_order = order_counter++;
    return true;
  }
};

// ============================================================================
// Test 1: Simple single-node resolution
// ============================================================================

bool test_single_node()
{
  std::cout << "Test: single node resolution... ";

  Node n = NodeDef::create(TestNode);
  NodeWorker<TestWork> worker{TestWork{}};

  worker.add(n);
  worker.run();

  if (!worker.is_resolved(n))
  {
    std::cout << "FAILED: node not resolved" << std::endl;
    return false;
  }

  if (worker.state(n).kind != WorkerStatus::Resolved)
  {
    std::cout << "FAILED: state kind not Resolved" << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Test 2: Multiple independent nodes
// ============================================================================

bool test_multiple_independent()
{
  std::cout << "Test: multiple independent nodes... ";

  Node n1 = NodeDef::create(TestNode);
  Node n2 = NodeDef::create(TestNode);
  Node n3 = NodeDef::create(TestNode);

  NodeWorker<TestWork> worker{TestWork{}};

  worker.add(n1);
  worker.add(n2);
  worker.add(n3);
  worker.run();

  if (!worker.is_resolved(n1) || !worker.is_resolved(n2) ||
      !worker.is_resolved(n3))
  {
    std::cout << "FAILED: not all nodes resolved" << std::endl;
    return false;
  }

  if (worker.states().size() != 3)
  {
    std::cout << "FAILED: expected 3 states, got " << worker.states().size()
              << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Test 3: Simple blocking (block_on)
// ============================================================================

bool test_simple_blocking()
{
  std::cout << "Test: simple blocking (block_on)... ";

  Node dependency = NodeDef::create(TestLeaf);
  Node dependent = NodeDef::create(TestNode);

  TestWork work;
  work.dependencies[dependent] = {dependency};

  NodeWorker<TestWork> worker{work};

  // Add only the dependent - dependency should be added via block_on
  worker.add(dependent);
  worker.run();

  if (!worker.is_resolved(dependency))
  {
    std::cout << "FAILED: dependency not resolved" << std::endl;
    return false;
  }

  if (!worker.is_resolved(dependent))
  {
    std::cout << "FAILED: dependent not resolved" << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Test 4: Block on already resolved node (should not block)
// ============================================================================

bool test_block_on_resolved()
{
  std::cout << "Test: block_on already resolved node... ";

  Node dependency = NodeDef::create(TestLeaf);
  Node dependent = NodeDef::create(TestNode);

  TestWork work;
  work.dependencies[dependent] = {dependency};

  NodeWorker<TestWork> worker{work};

  // Add dependency first, run to resolve it
  worker.add(dependency);
  worker.run();

  if (!worker.is_resolved(dependency))
  {
    std::cout << "FAILED: dependency not resolved after first run" << std::endl;
    return false;
  }

  // Now add dependent - it should not block
  worker.add(dependent);
  worker.run();

  if (!worker.is_resolved(dependent))
  {
    std::cout << "FAILED: dependent not resolved (should not have blocked)"
              << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Test 5: block_on_all - wait for multiple dependencies
// ============================================================================

bool test_block_on_all()
{
  std::cout << "Test: block_on_all... ";

  Node dep1 = NodeDef::create(TestLeaf);
  Node dep2 = NodeDef::create(TestLeaf);
  Node dep3 = NodeDef::create(TestLeaf);
  Node dependent = NodeDef::create(TestNode);

  TestWork work;
  work.block_mode = BlockMode::All;
  work.dependencies[dependent] = {dep1, dep2, dep3};

  NodeWorker<TestWork> worker{work};

  worker.add(dependent);
  worker.run();

  // All should be resolved
  if (!worker.is_resolved(dep1) || !worker.is_resolved(dep2) ||
      !worker.is_resolved(dep3))
  {
    std::cout << "FAILED: not all dependencies resolved" << std::endl;
    return false;
  }

  if (!worker.is_resolved(dependent))
  {
    std::cout << "FAILED: dependent not resolved" << std::endl;
    return false;
  }

  // Dependent should resolve last
  if (worker.state(dependent).resolve_order != 3)
  {
    std::cout << "FAILED: dependent did not resolve last (order="
              << worker.state(dependent).resolve_order << ")" << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Test 6: block_on_any - wait for any dependency
// ============================================================================

bool test_block_on_any()
{
  std::cout << "Test: block_on_any... ";

  Node dep1 = NodeDef::create(TestLeaf);
  Node dep2 = NodeDef::create(TestLeaf);
  Node dependent = NodeDef::create(TestNode);

  TestWork work;
  work.block_mode = BlockMode::Any;
  work.dependencies[dependent] = {dep1, dep2};

  NodeWorker<TestWork> worker{work};

  worker.add(dependent);
  worker.run();

  // Dependent should be resolved
  if (!worker.is_resolved(dependent))
  {
    std::cout << "FAILED: dependent not resolved" << std::endl;
    return false;
  }

  // Dependent should have been processed twice (once blocking, once resolving)
  if (worker.state(dependent).process_count != 2)
  {
    std::cout << "FAILED: expected 2 process calls, got "
              << worker.state(dependent).process_count << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Test 7: Chain of dependencies (A -> B -> C)
// ============================================================================

bool test_chain()
{
  std::cout << "Test: chain of dependencies (A -> B -> C)... ";

  Node a = NodeDef::create(TestNode);
  Node b = NodeDef::create(TestNode);
  Node c = NodeDef::create(TestLeaf);

  TestWork work;
  work.dependencies[a] = {b}; // A depends on B
  work.dependencies[b] = {c}; // B depends on C
  // C has no dependencies

  NodeWorker<TestWork> worker{work};

  // Only add A - B and C should be added via block_on
  worker.add(a);
  worker.run();

  if (!worker.is_resolved(a) || !worker.is_resolved(b) || !worker.is_resolved(c))
  {
    std::cout << "FAILED: not all nodes resolved" << std::endl;
    return false;
  }

  // Resolution order should be C, B, A
  if (worker.state(c).resolve_order != 0)
  {
    std::cout << "FAILED: C should resolve first" << std::endl;
    return false;
  }
  if (worker.state(b).resolve_order != 1)
  {
    std::cout << "FAILED: B should resolve second" << std::endl;
    return false;
  }
  if (worker.state(a).resolve_order != 2)
  {
    std::cout << "FAILED: A should resolve last" << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Test 8: Multiple dependents on same origin
// ============================================================================

bool test_multiple_dependents()
{
  std::cout << "Test: multiple dependents on same origin... ";

  Node origin = NodeDef::create(TestLeaf);
  Node d1 = NodeDef::create(TestNode);
  Node d2 = NodeDef::create(TestNode);
  Node d3 = NodeDef::create(TestNode);

  TestWork work;
  work.dependencies[d1] = {origin};
  work.dependencies[d2] = {origin};
  work.dependencies[d3] = {origin};

  NodeWorker<TestWork> worker{work};

  worker.add(d1);
  worker.add(d2);
  worker.add(d3);
  worker.run();

  if (!worker.is_resolved(origin))
  {
    std::cout << "FAILED: origin not resolved" << std::endl;
    return false;
  }

  if (!worker.is_resolved(d1) || !worker.is_resolved(d2) ||
      !worker.is_resolved(d3))
  {
    std::cout << "FAILED: not all dependents resolved" << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Test 9: Re-adding already seen node is no-op
// ============================================================================

bool test_readd_node()
{
  std::cout << "Test: re-adding node is no-op... ";

  Node n = NodeDef::create(TestNode);
  NodeWorker<TestWork> worker{TestWork{}};

  worker.add(n);
  worker.add(n); // Should be ignored
  worker.add(n); // Should be ignored
  worker.run();

  if (worker.state(n).seed_count != 1)
  {
    std::cout << "FAILED: seed called " << worker.state(n).seed_count
              << " times, expected 1" << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Test 10: Custom state data is preserved
// ============================================================================

bool test_custom_state()
{
  std::cout << "Test: custom state data preserved... ";

  Node n = NodeDef::create(TestNode);

  // Use a lambda to customize seed behavior
  TestWork work;
  NodeWorker<TestWork> worker{work};

  worker.add(n);
  worker.state(n).custom_data = "seeded"; // Set custom data after adding
  worker.run();

  if (worker.state(n).custom_data != "seeded")
  {
    std::cout << "FAILED: custom_data not preserved" << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Test 11: Process children recursively
// ============================================================================

bool test_process_children()
{
  std::cout << "Test: process children recursively... ";

  // Build a tree: root -> [child1, child2 -> [grandchild]]
  Node grandchild = NodeDef::create(TestLeaf);
  Node child1 = NodeDef::create(TestLeaf);
  Node child2 = NodeDef::create(TestNode);
  child2->push_back(grandchild);

  Node root = NodeDef::create(TestNode);
  root->push_back(child1);
  root->push_back(child2);

  TestWork work;
  work.process_children = true;
  work.block_mode = BlockMode::All;

  NodeWorker<TestWork> worker{work};
  worker.add(root);
  worker.run();

  // All nodes should be resolved
  if (!worker.is_resolved(root))
  {
    std::cout << "FAILED: root not resolved" << std::endl;
    return false;
  }
  if (!worker.is_resolved(child1))
  {
    std::cout << "FAILED: child1 not resolved" << std::endl;
    return false;
  }
  if (!worker.is_resolved(child2))
  {
    std::cout << "FAILED: child2 not resolved" << std::endl;
    return false;
  }
  if (!worker.is_resolved(grandchild))
  {
    std::cout << "FAILED: grandchild not resolved" << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Test 12: Cycles terminate but remain unresolved
// ============================================================================

bool test_cycle_terminates()
{
  std::cout << "Test: cycles terminate but remain unresolved... ";

  // Create a cycle: A -> B -> C -> A
  Node a = NodeDef::create(TestNode);
  Node b = NodeDef::create(TestNode);
  Node c = NodeDef::create(TestNode);

  TestWork work;
  work.dependencies[a] = {b}; // A depends on B
  work.dependencies[b] = {c}; // B depends on C
  work.dependencies[c] = {a}; // C depends on A (cycle!)

  NodeWorker<TestWork> worker{work};

  worker.add(a);
  worker.run(); // Should terminate!

  // All nodes should be blocked (not resolved) due to cycle
  if (worker.is_resolved(a))
  {
    std::cout << "FAILED: A should not be resolved (cycle)" << std::endl;
    return false;
  }
  if (worker.is_resolved(b))
  {
    std::cout << "FAILED: B should not be resolved (cycle)" << std::endl;
    return false;
  }
  if (worker.is_resolved(c))
  {
    std::cout << "FAILED: C should not be resolved (cycle)" << std::endl;
    return false;
  }

  // Verify all nodes are in Blocked state
  if (worker.state(a).kind != WorkerStatus::Blocked)
  {
    std::cout << "FAILED: A should be Blocked" << std::endl;
    return false;
  }
  if (worker.state(b).kind != WorkerStatus::Blocked)
  {
    std::cout << "FAILED: B should be Blocked" << std::endl;
    return false;
  }
  if (worker.state(c).kind != WorkerStatus::Blocked)
  {
    std::cout << "FAILED: C should be Blocked" << std::endl;
    return false;
  }

  std::cout << "PASSED" << std::endl;
  return true;
}

// ============================================================================
// Main
// ============================================================================

int main()
{
  std::cout << "NodeWorker Tests" << std::endl;
  std::cout << "================" << std::endl;

  int failed = 0;

  if (!test_single_node())
    failed++;
  if (!test_multiple_independent())
    failed++;
  if (!test_simple_blocking())
    failed++;
  if (!test_block_on_resolved())
    failed++;
  if (!test_block_on_all())
    failed++;
  if (!test_block_on_any())
    failed++;
  if (!test_chain())
    failed++;
  if (!test_multiple_dependents())
    failed++;
  if (!test_readd_node())
    failed++;
  if (!test_custom_state())
    failed++;
  if (!test_process_children())
    failed++;
  if (!test_cycle_terminates())
    failed++;

  std::cout << "================" << std::endl;
  if (failed == 0)
  {
    std::cout << "All tests passed!" << std::endl;
    return 0;
  }
  else
  {
    std::cout << failed << " test(s) failed!" << std::endl;
    return 1;
  }
}
