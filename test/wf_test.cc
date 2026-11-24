#include <iostream>
#include <string>
#include <trieste/trieste.h>
#include <trieste/wf.h>

using namespace trieste;
using namespace trieste::wf;
using namespace trieste::wf::ops;

namespace
{
  inline const auto FieldName = TokenDef("wf-test-field-name");
  inline const auto OtherName = TokenDef("wf-test-other-name");
  inline const auto ThirdName = TokenDef("wf-test-third-name");
  inline const auto Parent = TokenDef("wf-test-parent");
  inline const auto ValueA = TokenDef("wf-test-value-a");
  inline const auto ValueB = TokenDef("wf-test-value-b");
  inline const auto RefScope = TokenDef("wf-test-ref-scope", flag::symtab);
  inline const auto RefBind = TokenDef("wf-test-ref-bind", flag::lookup);
  inline const auto RefUse = TokenDef("wf-test-ref-use");
  inline const auto RefExpr = TokenDef("wf-test-ref-expr");
  inline const auto RefKey = TokenDef("wf-test-ref-key", flag::print);
  inline const auto RefNum = TokenDef("wf-test-ref-num", flag::print);

  template<typename F>
  bool throws(F&& fn)
  {
    try
    {
      fn();
    }
    catch (const std::runtime_error&)
    {
      return true;
    }
    return false;
  }

  bool check(bool condition, const std::string& message)
  {
    if (condition)
      return true;

    std::cerr << message << std::endl;
    return false;
  }

  template<typename F>
  bool expect_duplicate(F&& fn, const std::string& name)
  {
    try
    {
      fn();
    }
    catch (const std::runtime_error& error)
    {
      return check(
        error.what() == "duplicate WF field name: " + name,
        "unexpected duplicate-field error: " + std::string(error.what()));
    }

    std::cerr << "expected duplicate WF field name: " << name << std::endl;
    return false;
  }
}

int main()
{
  int failures = 0;

  auto repeated = FieldName * FieldName * FieldName;
  failures += !check(
    repeated.fields.size() == 3 && repeated.index(FieldName) == 0,
    "repeated implicit fields should remain valid");
  failures += !check(
    !repeated.fields[0].explicit_name && !repeated.fields[1].explicit_name &&
      !repeated.fields[2].explicit_name,
    "token product fields should be implicit");

  failures += !expect_duplicate(
    []() { (void)((FieldName >>= ValueA) * FieldName); }, FieldName.name);
  failures += !expect_duplicate(
    []() { (void)(FieldName * (FieldName >>= ValueA)); }, FieldName.name);
  failures += !expect_duplicate(
    []() { (void)((FieldName >>= ValueA) * (FieldName >>= ValueB)); },
    FieldName.name);

  auto choices = ValueA | ValueB;
  failures += !expect_duplicate(
    [&choices]() { (void)((FieldName >>= choices) * FieldName); },
    FieldName.name);
  failures += !expect_duplicate(
    []() { (void)((FieldName >>= (ValueA | ValueB)) * FieldName); },
    FieldName.name);
  failures += !expect_duplicate(
    []() { (void)((FieldName >>= ValueA) * OtherName * FieldName); },
    FieldName.name);

  auto distinct = (FieldName >>= ValueA) * (OtherName >>= ValueB);
  failures += !check(
    distinct.index(FieldName) == 0 && distinct.index(OtherName) == 1,
    "distinct explicit fields should remain valid");

  auto bound = FieldName * OtherName;
  bound[FieldName];
  auto extended = bound * (ThirdName >>= ValueA);
  failures += !check(
    bound.binding == FieldName && extended.binding == FieldName,
    "appending a field should preserve binding metadata");
  failures += !expect_duplicate(
    [&bound]() { (void)(bound * (FieldName >>= ValueA)); }, FieldName.name);
  failures += !check(
    bound.fields.size() == 2 && bound.binding == FieldName,
    "a failed append should not modify its source");

  failures += !expect_duplicate(
    []() {
      const auto explicit_field =
        Field{FieldName, Choice{std::vector<Token>{ValueA}}};
      (void)Fields{
        std::vector<Field>{
          explicit_field,
          Field{FieldName, Choice{std::vector<Token>{ValueB}}, false}},
        Invalid};
    },
    FieldName.name);

  auto choice_shape = Parent <<= (ValueA | ValueB);
  auto token_shape = Parent <<= ValueA;
  failures += !check(
    !std::get<Fields>(choice_shape.shape).fields[0].explicit_name &&
      !std::get<Fields>(token_shape.shape).fields[0].explicit_name,
    "unnamed shape fields should be implicit");

  failures += !expect_duplicate(
    []() { (void)Fields{std::vector<Field>{Field{}, Field{}}, Invalid}; },
    "<invalid>");

  // Reference tokens (`&Token`): DSL only, checking/generation ignore them.
  {
    auto ref_shape = Parent <<= &ValueA;
    auto& ref_fields = std::get<Fields>(ref_shape.shape);
    failures += !check(
      ref_fields.fields.size() == 1 && ref_fields.fields[0].name == ValueA &&
        !ref_fields.fields[0].explicit_name &&
        ref_fields.fields[0].choice.is_reference(ValueA),
      "&Token shape should be an implicit field named after the token");

    auto plain_shape = Parent <<= ValueA;
    failures += !check(
      !std::get<Fields>(plain_shape.shape)
         .fields[0]
         .choice.is_reference(ValueA),
      "plain tokens should not be references");

    auto mixed = &ValueA | ValueB;
    failures += !check(
      mixed.types.size() == 2 && mixed.is_reference(ValueA) &&
        !mixed.is_reference(ValueB),
      "references should be tracked per alternative");

    auto mixed2 = ValueB | &ValueA;
    auto mixed3 = (&ValueA | ValueB) | OtherName;
    failures += !check(
      mixed2.is_reference(ValueA) && !mixed2.is_reference(ValueB) &&
        mixed3.is_reference(ValueA) && !mixed3.is_reference(OtherName),
      "references should survive combining choices");

    auto removed = mixed - ValueA;
    failures += !check(
      removed.types.size() == 1 && removed.refs.empty(),
      "removing a token should remove its reference mark");

    auto seq = (&ValueA)++;
    auto opt = ~&ValueA;
    failures += !check(
      seq.choice.is_reference(ValueA) && opt.choice.is_reference(ValueA) &&
        opt.max_len == 1,
      "reference tokens should work in sequences");

    auto named = (FieldName >>= &ValueA) * (OtherName >>= (&ValueA | ValueB));
    failures += !check(
      named.fields[0].choice.is_reference(ValueA) &&
        named.fields[1].choice.is_reference(ValueA) &&
        !named.fields[1].choice.is_reference(ValueB),
      "named fields should keep reference marks");

    auto product = (&ValueA) * ValueB * (&OtherName);
    failures += !check(
      product.fields.size() == 3 &&
        product.fields[0].choice.is_reference(ValueA) &&
        !product.fields[1].choice.is_reference(ValueB) &&
        product.fields[2].choice.is_reference(OtherName),
      "token products should keep reference marks");

    // A token that is both a reference and plain is treated as a reference.
    auto both = ValueA | &ValueA;
    failures += !check(
      both.is_reference(ValueA), "`Foo | &Foo` should behave like `&Foo`");
  }

  // Checking references.
  {
    // clang-format off
    auto wf =
      (RefScope <<= (RefBind | RefUse)++)
      | (RefBind <<= RefKey * RefNum)[RefKey]
      | (RefUse <<= &RefKey)
      ;
    // clang-format on
    wf.validate();

    auto make = [&](const char* bound, const char* used) {
      auto scope = NodeDef::create(RefScope);
      scope->push_back(RefBind << (RefKey ^ bound) << (RefNum ^ "1"));
      scope->push_back(RefUse << (RefKey ^ used));
      return scope;
    };

    auto good = make("x", "x");
    failures += !check(
      wf.build_st(good) && wf.check(good), "bound reference should be valid");

    auto bad = make("x", "y");
    failures += !check(
      wf.build_st(bad) && !wf.check(bad),
      "unbound reference should be invalid");

    // The reference precedes the binding, but the scope is not defbeforeuse.
    auto fwd = NodeDef::create(RefScope);
    fwd->push_back(RefUse << (RefKey ^ "x"));
    fwd->push_back(RefBind << (RefKey ^ "x") << (RefNum ^ "1"));
    failures += !check(
      wf.build_st(fwd) && wf.check(fwd), "forward reference should be valid");

    // Non-leaf reference token.
    failures += !check(
      throws([&]() {
        auto nonleaf = (RefScope <<= RefUse++) | (RefBind <<= RefKey)[RefKey] |
          (RefUse <<= &RefBind) | (RefBind <<= RefKey * RefNum)[RefKey];
        nonleaf.validate();
      }),
      "non-leaf reference token should be rejected");

    // Reference token that nothing binds.
    failures += !check(
      throws([&]() {
        auto unbound = (RefScope <<= RefUse++) | (RefUse <<= &RefKey);
        unbound.validate();
      }),
      "reference token that is never bound should be rejected");
  }

  // Generating references.
  {
    auto gloc = [](Rand& rand, Node) {
      return Location("v" + std::to_string(rand() % 1000));
    };

    // The reference is a child of a sibling of the binders. Siblings are all
    // chosen before any of them is expanded, so a reference that is a direct
    // sibling of its binder (`RefScope <<= (RefBind | &RefKey)++`) is never
    // generated. As a child, it is chosen after the earlier siblings have
    // been expanded and bound.
    // clang-format off
    auto wf =
      (Top <<= RefScope)
      | (RefScope <<= (RefBind | RefExpr)++)
      | (RefBind <<= RefKey * RefNum)[RefKey]
      | (RefExpr <<= (&RefKey | RefNum))
      ;
    // clang-format on

    size_t refs = 0;
    bool before_bind = false;
    for (Seed seed = 1; seed <= 300; ++seed)
    {
      auto ast = wf.gen(gloc, seed, 6, false);

      // Traversal is in tree order, so a reference must come after a binder.
      bool seen_bind = false;
      ast->traverse([&](Node& n) {
        seen_bind = seen_bind || (n == RefBind);
        if (n == RefKey && n->parent() == RefExpr)
        {
          refs++;
          before_bind = before_bind || !seen_bind;
        }
        return true;
      });

      failures += !check(
        wf.build_st(ast) && wf.check(ast),
        "generated tree should be well-formed (seed " + std::to_string(seed) +
          ")");
    }
    failures += !check(refs > 0, "references should be generated sometimes");
    failures += !check(!before_bind, "references should come after a binding");

    // Nothing can ever be bound before the only possible reference, so no
    // tree can be generated.
    // clang-format off
    auto unbound =
      (Top <<= RefUse)
      | (RefBind <<= RefKey * RefNum)[RefKey]
      | (RefUse <<= &RefKey)
      ;
    // clang-format on
    failures += !check(
      throws([&]() { (void)unbound.gen(gloc, 1, 4, false); }),
      "a tree that requires an unbound reference cannot be generated");
  }

  if (failures != 0)
  {
    std::cerr << failures << " WF test(s) failed" << std::endl;
    return 1;
  }

  std::cout << "All WF tests passed" << std::endl;
  return 0;
}
