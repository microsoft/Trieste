#include <iostream>
#include <string>
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

  if (failures != 0)
  {
    std::cerr << failures << " WF test(s) failed" << std::endl;
    return 1;
  }

  std::cout << "All WF tests passed" << std::endl;
  return 0;
}
