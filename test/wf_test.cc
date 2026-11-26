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
  inline const auto RefOrdered =
    TokenDef("wf-test-ref-ordered", flag::symtab | flag::defbeforeuse);
  inline const auto RefName = TokenDef("wf-test-ref-name");

  inline const auto RefKey = TokenDef("wf-test-ref-key", flag::print);
  inline const auto RefNum = TokenDef("wf-test-ref-num", flag::print);

  // Counts the references (RefKey children of `parent`) in a tree, and how
  // many of them come before the first binder in tree order.
  std::pair<size_t, size_t> count_refs(Node ast, Token parent)
  {
    size_t refs = 0;
    size_t before_bind = 0;
    bool seen_bind = false;
    ast->traverse([&](Node& n) {
      seen_bind = seen_bind || (n == RefBind);
      if (n == RefKey && n->parent() == parent)
      {
        refs++;
        before_bind += !seen_bind;
      }
      return true;
    });
    return {refs, before_bind};
  }

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

  auto binder = FieldName * OtherName;
  binder[FieldName];
  auto extended = binder * (ThirdName >>= ValueA);
  failures += !check(
    binder.binding == FieldName && extended.binding == FieldName,
    "appending a field should preserve binding metadata");
  failures += !expect_duplicate(
    [&binder]() { (void)(binder * (FieldName >>= ValueA)); }, FieldName.name);
  failures += !check(
    binder.fields.size() == 2 && binder.binding == FieldName,
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

    auto well_formed = [&](const Wellformed& wf, Node ast, Seed seed) {
      return check(
        wf.build_st(ast) && wf.check(ast),
        "generated tree should be well-formed (seed " + std::to_string(seed) +
          ")");
    };

    // Generates trees for a scope token `scope` containing binders and
    // `user`s, checks that they are well-formed, and counts references (and
    // those that come before every binder).
    auto gen_refs = [&](Token scope, Token user, const Choice& user_choice) {
      // clang-format off
      auto wf =
        (Top <<= scope)
        | (scope <<= (RefBind | user)++)
        | (RefBind <<= RefKey * RefNum)[RefKey]
        | (user <<= user_choice)
        ;
      // clang-format on

      size_t refs = 0;
      size_t forward = 0;
      for (Seed seed = 1; seed <= 300; ++seed)
      {
        constexpr int TARGET_DEPTH = 6;
        constexpr bool GEN_BOUND = false;
        auto ast = wf.gen(gloc, seed, TARGET_DEPTH, GEN_BOUND);
        auto [r, f] = count_refs(ast, user);
        refs += r;
        forward += f;
        failures += !well_formed(wf, ast, seed);
      }
      return std::make_pair(refs, forward);
    };

    // References are filled in once the whole tree is generated, and a
    // binder counts as soon as it is created, so a reference can be a direct
    // sibling of its binder.
    {
      auto [refs, forward] = gen_refs(RefScope, RefUse, Choice{&RefKey});
      failures += !check(refs > 0, "sibling references should be generated");
    }

    // All siblings are created before any is expanded, so a nested reference
    // can refer to a later sibling when the scope is not defbeforeuse.
    {
      auto [refs, forward] = gen_refs(RefScope, RefExpr, &RefKey | RefNum);
      failures += !check(refs > 0, "nested references should be generated");
      failures += !check(forward > 0, "forward references should be generated");
    }

    // With defbeforeuse, a reference must come after its binder.
    {
      auto [refs, forward] = gen_refs(RefOrdered, RefExpr, &RefKey | RefNum);
      failures += !check(refs > 0, "ordered references should be generated");
      failures += !check(
        forward == 0, "references should follow their binder (defbeforeuse)");
    }

    // With gen_bound, positions that may be bound are filled in at the end,
    // sometimes with a bound name and sometimes with a fresh one.
    {
      // clang-format off
      auto wf =
        (Top <<= RefScope)
        | (RefScope <<= (RefBind | RefName)++)
        | (RefBind <<= RefKey * RefNum)[RefKey]
        | (RefName <<= RefKey)
        ;
      // clang-format on

      size_t bound_names = 0;
      size_t fresh_names = 0;
      size_t unfilled = 0;
      for (Seed seed = 1; seed <= 300; ++seed)
      {
        constexpr int TARGET_DEPTH = 6;
        constexpr bool GEN_BOUND = true;
        auto ast = wf.gen(gloc, seed, TARGET_DEPTH, GEN_BOUND);
        wf.build_st(ast);
        ast->traverse([&](Node& n) {
          if (n == RefKey && n->parent() == RefName)
          {
            unfilled += n->location().view().empty();
            if (n->lookup().empty())
              fresh_names++;
            else
              bound_names++;
          }
          return true;
        });
      }
      failures += !check(bound_names > 0, "gen_bound should reuse bound names");
      failures +=
        !check(fresh_names > 0, "gen_bound should use fresh names too");
      failures +=
        !check(unfilled == 0, "every gen_bound hole should be filled");
    }

    // Sampled references are kept if they can be resolved, and replaced
    // otherwise. A sample that would leave a reference that can never be
    // bound is not used.
    {
      // clang-format off
      auto wf =
        (Top <<= RefScope)
        | (RefScope <<= (RefBind | RefExpr)++)
        | (RefBind <<= RefKey * RefNum)[RefKey]
        | (RefExpr <<= (&RefKey | RefNum))
        ;
      // clang-format on

      auto scope = NodeDef::create(RefScope);
      scope->push_back(RefBind << (RefKey ^ "x") << (RefNum ^ "1"));
      scope->push_back(RefExpr << (RefKey ^ "x"));
      scope->push_back(RefExpr << (RefKey ^ "nope"));
      auto lonely = NodeDef::create(RefScope);
      lonely->push_back(RefExpr << (RefKey ^ "nope"));
      std::map<Token, Nodes> samples{
        {RefScope, {scope, lonely}}, {RefExpr, {RefExpr << (RefKey ^ "nope")}}};

      size_t kept = 0;
      for (size_t level = 0; level <= 2; ++level)
      {
        for (Seed seed = 1; seed <= 100; ++seed)
        {
          constexpr int TARGET_DEPTH = 6;
          constexpr bool GEN_BOUND = false;
          const trieste::wf::TokenWeights weights = {};
          constexpr bool ENABLE_SAMPLING = true;
          constexpr int FREQUENCY = 100;
          auto ast = wf.gen(
            gloc,
            seed,
            TARGET_DEPTH,
            GEN_BOUND,
            weights,
            samples,
            level,
            ENABLE_SAMPLING,
            FREQUENCY);
          failures += !well_formed(wf, ast, seed);
          ast->traverse([&](Node& n) {
            kept +=
              (n == RefKey && n->location().view() == "x" &&
               n->parent() == RefExpr);
            return true;
          });
        }
      }
      failures += !check(kept > 0, "resolvable sampled references are kept");
    }

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
