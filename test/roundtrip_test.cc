#include <iostream>
#include <sstream>
#include <trieste/trieste.h>

using namespace trieste;

namespace
{
  // Custom tokens for testing.
  inline const auto TestPrint = TokenDef("test-print", flag::print);
  inline const auto TestNode = TokenDef("test-node");
  inline const auto TestRoot = TokenDef("test-root");

  // Check that a specific str() output matches an expected string.
  bool
  check_format(const std::string& name, Node ast, const std::string& expected)
  {
    std::ostringstream out;
    ast->str(out);
    auto actual = out.str();

    if (actual != expected)
    {
      std::cout << name << ": format mismatch" << std::endl;
      std::cout << "  expected: " << expected << std::endl;
      std::cout << "  actual:   " << actual << std::endl;
      return false;
    }

    return true;
  }

  // Serialize an AST, parse it back via build_ast(), then verify:
  //  1. Idempotency: re-serializing produces identical text.
  //  2. Location metadata: origin, pos, len, and view() match.
  bool check_roundtrip(const std::string& name, Node ast)
  {
    std::ostringstream out1;
    ast->str(out1);
    auto text1 = out1.str();

    auto header = std::string("test\ntest\n");
    auto full = header + text1;
    auto src = SourceDef::synthetic(full, "roundtrip");
    auto ast2 = build_ast(src, header.size());

    if (!ast2)
    {
      std::cout << name << ": build_ast returned null" << std::endl;
      std::cout << "  input: " << text1 << std::endl;
      return false;
    }

    bool ok = true;

    // Check idempotency.
    std::ostringstream out2;
    ast2->str(out2);
    auto text2 = out2.str();

    if (text1 != text2)
    {
      std::cout << name << ": not idempotent" << std::endl;
      std::cout << "  first:  " << text1 << std::endl;
      std::cout << "  second: " << text2 << std::endl;
      ok = false;
    }

    // Walk both trees in parallel and compare locations.
    // Note: recursive — will stack overflow on very deep ASTs.
    std::function<bool(Node, Node, const std::string&)> compare =
      [&](Node a, Node b, const std::string& path) -> bool {
      bool cmp_ok = true;
      auto& aloc = a->location();
      auto& bloc = b->location();

      auto a_origin = aloc.source ? aloc.source->origin() : std::string();
      auto b_origin = bloc.source ? bloc.source->origin() : std::string();
      if (a_origin != b_origin)
      {
        std::cout << name << " " << path << ": origin mismatch: '" << a_origin
                  << "' vs '" << b_origin << "'" << std::endl;
        cmp_ok = false;
      }

      if (a->type() & flag::print)
      {
        if (aloc.view() != bloc.view())
        {
          std::cout << name << " " << path << ": view mismatch: '"
                    << aloc.view() << "' vs '" << bloc.view() << "'"
                    << std::endl;
          cmp_ok = false;
        }
      }

      if (aloc.source && !a_origin.empty())
      {
        if (aloc.pos != bloc.pos)
        {
          std::cout << name << " " << path << ": pos mismatch: " << aloc.pos
                    << " vs " << bloc.pos << std::endl;
          cmp_ok = false;
        }
        if (aloc.len != bloc.len)
        {
          std::cout << name << " " << path << ": len mismatch: " << aloc.len
                    << " vs " << bloc.len << std::endl;
          cmp_ok = false;
        }
      }

      if (a->size() != b->size())
      {
        std::cout << name << " " << path << ": child count mismatch"
                  << std::endl;
        return false;
      }

      for (size_t i = 0; i < a->size(); i++)
      {
        if (!compare(a->at(i), b->at(i), path + "/" + std::to_string(i)))
          cmp_ok = false;
      }
      return cmp_ok;
    };

    if (!compare(ast, ast2, "root"))
      ok = false;

    return ok;
  }
}

int main()
{
  int failures = 0;

  // Case 1: No location, no print — bare token.
  {
    auto ast = NodeDef::create(TestRoot);
    if (!check_format("bare_token", ast, "(test-root)"))
      failures++;
    if (!check_roundtrip("bare_token", ast))
      failures++;
  }

  // Case 2: No location, print — bare netstring (ErrorMsg style).
  {
    auto ast = ErrorMsg ^ "hello world";
    if (!check_format("bare_netstring", ast, "(errormsg 11:hello world)"))
      failures++;
    if (!check_roundtrip("bare_netstring", ast))
      failures++;
  }

  // Case 3: Location with filename, non-print.
  {
    auto src = SourceDef::synthetic("abcdefghij", "input.json");
    auto ast = NodeDef::create(TestNode, Location(src, 2, 5));
    if (!check_format(
          "loc_filename_noprint", ast, "(test-node 10:input.json|2|5)"))
      failures++;
    if (!check_roundtrip("loc_filename_noprint", ast))
      failures++;
  }

  // Case 4: Location with filename, print.
  {
    auto src = SourceDef::synthetic("abcdefghij", "input.json");
    auto ast = NodeDef::create(TestPrint, Location(src, 3, 4));
    if (!check_format(
          "loc_filename_print", ast, "(test-print 10:input.json|3|4:defg)"))
      failures++;
    if (!check_roundtrip("loc_filename_print", ast))
      failures++;
  }

  // Case 5: Elided filename — children share parent source.
  {
    auto src = SourceDef::synthetic("abcdefghij", "test.txt");
    auto parent = NodeDef::create(TestNode, Location(src, 0, 10));
    auto child1 = NodeDef::create(TestNode, Location(src, 2, 3));
    auto child2 = NodeDef::create(TestPrint, Location(src, 5, 4));
    parent << child1 << child2;

    if (!check_format(
          "elided_filename",
          parent,
          "(test-node 8:test.txt|0|10\n"
          "  (test-node |2|3)\n"
          "  (test-print |5|4:fghi))"))
      failures++;
    if (!check_roundtrip("elided_filename", parent))
      failures++;
  }

  // Case 6: Mixed sources — child has different source than parent.
  {
    auto src1 = SourceDef::synthetic("abcdefghij", "file1.txt");
    auto src2 = SourceDef::synthetic("klmnopqrst", "file2.txt");
    auto parent = NodeDef::create(TestNode, Location(src1, 0, 10));
    auto child = NodeDef::create(TestPrint, Location(src2, 1, 3));
    parent << child;

    if (!check_format(
          "mixed_sources",
          parent,
          "(test-node 9:file1.txt|0|10\n"
          "  (test-print 9:file2.txt|1|3:lmn))"))
      failures++;
    if (!check_roundtrip("mixed_sources", parent))
      failures++;
  }

  // Case 7: No location parent with located children.
  {
    auto src = SourceDef::synthetic("abcdefghij", "test.txt");
    auto parent = NodeDef::create(TestRoot);
    auto child = NodeDef::create(TestNode, Location(src, 0, 5));
    parent << child;

    if (!check_format(
          "null_parent_loc_child",
          parent,
          "(test-root\n"
          "  (test-node 8:test.txt|0|5))"))
      failures++;
    if (!check_roundtrip("null_parent_loc_child", parent))
      failures++;
  }

  // Case 8: Nested tree with symtab.
  {
    auto src = SourceDef::synthetic("abcdefghij", "test.txt");
    auto top = NodeDef::create(Top);
    auto child = NodeDef::create(TestNode, Location(src, 0, 5));
    top << child;

    if (!check_roundtrip("with_symtab", top))
      failures++;
  }

  // Case 9: Print node with empty content (zero-length location).
  {
    auto src = SourceDef::synthetic("abcdefghij", "test.txt");
    auto ast = NodeDef::create(TestPrint, Location(src, 3, 0));
    if (!check_format("zero_len_print", ast, "(test-print 8:test.txt|3|0:)"))
      failures++;
    if (!check_roundtrip("zero_len_print", ast))
      failures++;
  }

  // Case 10: Filename containing colons (netstring handles this).
  {
    auto origin = std::string("C:\\Users\\test:file.json");
    auto src = SourceDef::synthetic("abcdefghij", origin);
    auto ast = NodeDef::create(TestNode, Location(src, 0, 5));
    auto expected =
      "(test-node " + std::to_string(origin.size()) + ":" + origin + "|0|5)";
    if (!check_format("colon_in_filename", ast, expected))
      failures++;
    if (!check_roundtrip("colon_in_filename", ast))
      failures++;
  }

  // Case 11: Backward compatibility — old format without location info.
  {
    auto input = std::string("test\ntest\n(errormsg 5:hello)");
    auto source = SourceDef::synthetic(input, "old.trieste");
    auto ast = build_ast(source, 10);

    if (!ast)
    {
      std::cout << "backward_compat: build_ast returned null" << std::endl;
      failures++;
    }
    else
    {
      auto view = ast->location().view();
      if (view != "hello")
      {
        std::cout << "backward_compat: expected 'hello', got '" << view << "'"
                  << std::endl;
        failures++;
      }
    }
  }

  // Case 12: Deeply nested tree.
  {
    auto src = SourceDef::synthetic("abcdefghijklmnop", "deep.txt");
    auto root = NodeDef::create(TestNode, Location(src, 0, 16));
    auto current = root;
    for (int i = 1; i <= 5; i++)
    {
      auto child = NodeDef::create(TestNode, Location(src, i, 16 - (size_t)i));
      current << child;
      current = child;
    }
    auto leaf = NodeDef::create(TestPrint, Location(src, 6, 3));
    current << leaf;

    if (!check_roundtrip("deep_nesting", root))
      failures++;
  }

  // Case 13: Multiple children, some with location, some without.
  {
    auto src = SourceDef::synthetic("abcdefghij", "mix.txt");
    auto parent = NodeDef::create(TestNode, Location(src, 0, 10));
    auto child1 = NodeDef::create(TestNode, Location(src, 0, 3));
    auto child2 = NodeDef::create(TestRoot); // no location
    auto child3 = NodeDef::create(TestPrint, Location(src, 5, 2));
    parent << child1 << child2 << child3;

    if (!check_roundtrip("mixed_loc_noloc", parent))
      failures++;
  }

  // Case 14: Mixed sources — each child gets correct origin.
  {
    auto src1 = SourceDef::synthetic("abcdefghij", "file1.txt");
    auto src2 = SourceDef::synthetic("klmnopqrst", "file2.txt");
    auto parent = NodeDef::create(TestNode, Location(src1, 0, 10));
    auto child1 = NodeDef::create(TestPrint, Location(src1, 1, 3));
    auto child2 = NodeDef::create(TestPrint, Location(src2, 2, 4));
    parent << child1 << child2;
    if (!check_roundtrip("loc_mixed_origin", parent))
      failures++;
  }

  // Case 15: Filename with colons — print node origin preserved.
  {
    auto origin = std::string("C:\\Users\\test:file.json");
    auto src = SourceDef::synthetic("abcdefghij", origin);
    auto ast = NodeDef::create(TestPrint, Location(src, 0, 5));
    if (!check_roundtrip("loc_colon_origin", ast))
      failures++;
  }

  // --- Idempotency / regression tests ---

  // Bug 1 regression: old-format bare netstring must not gain a spurious
  // origin after round-trip.
  {
    auto ast = ErrorMsg ^ "hello";
    if (!check_roundtrip("idempotent_bare_netstring", ast))
      failures++;
  }

  // Bug 3 regression: elision must fire after round-trip (origin string
  // comparison, not pointer comparison).
  {
    auto src = SourceDef::synthetic("abcdefghij", "elide.txt");
    auto parent = NodeDef::create(TestNode, Location(src, 0, 10));
    auto child1 = NodeDef::create(TestNode, Location(src, 2, 3));
    auto child2 = NodeDef::create(TestPrint, Location(src, 5, 4));
    parent << child1 << child2;
    if (!check_roundtrip("idempotent_elision", parent))
      failures++;
  }

  // Idempotency of mixed-source tree.
  {
    auto src1 = SourceDef::synthetic("abcdefghij", "f1.txt");
    auto src2 = SourceDef::synthetic("klmnopqrst", "f2.txt");
    auto parent = NodeDef::create(TestNode, Location(src1, 0, 10));
    auto child1 = NodeDef::create(TestPrint, Location(src1, 1, 3));
    auto child2 = NodeDef::create(TestPrint, Location(src2, 2, 4));
    parent << child1 << child2;
    if (!check_roundtrip("idempotent_mixed_sources", parent))
      failures++;
  }

  // Idempotency of no-location parent with located children.
  {
    auto src = SourceDef::synthetic("abcdefghij", "test.txt");
    auto parent = NodeDef::create(TestRoot);
    auto child = NodeDef::create(TestPrint, Location(src, 0, 5));
    parent << child;
    if (!check_roundtrip("idempotent_noloc_parent", parent))
      failures++;
  }

  // Print content containing special characters: digits, colons, parens.
  {
    auto content = std::string("42:foo(bar)");
    auto src = SourceDef::synthetic(content, "special.txt");
    auto ast = NodeDef::create(TestPrint, Location(src, 0, content.size()));
    if (!check_roundtrip("special_content", ast))
      failures++;
  }

  // Print content containing newlines.
  {
    auto content = std::string("line1\nline2\nline3");
    auto src = SourceDef::synthetic(content, "multi.txt");
    auto ast = NodeDef::create(TestPrint, Location(src, 0, content.size()));
    if (!check_roundtrip("newline_content", ast))
      failures++;
  }

  if (failures > 0)
  {
    std::cout << failures << " test(s) failed" << std::endl;
    return 1;
  }

  std::cout << "All round-trip tests passed" << std::endl;
  return 0;
}
