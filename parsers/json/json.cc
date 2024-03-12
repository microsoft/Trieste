#include "json.h"

namespace
{
  using namespace trieste;
  using namespace trieste::json;

  std::size_t invalid_tokens(
    Node n, std::initializer_list<Token> tokens, const std::string& message)
  {
    std::size_t changes = 0;
    for (auto child : *n)
    {
      if (child->in(tokens))
      {
        n->replace(child, err(child, message));
        changes += 1;
      }
      else
      {
        changes += invalid_tokens(child, tokens, message);
      }
    }

    return changes;
  }
}

namespace trieste::json
{

  const auto ValueTokens = T(Object, Array, String, Number, True, False, Null);

  PassDef groups()
  {
    PassDef groups = {
      "groups",
      wf_groups,
      dir::bottomup,
      {
        In(Array) * T(Group)[Group] >>
          [](Match& _) { return ArrayGroup << *_[Group]; },

        In(Object) * T(Group)[Group] >>
          [](Match& _) { return ObjectGroup << *_[Group]; },

        In(Top) *
            (T(File) << ((T(Group) << (ValueTokens[Value] * End)) * End)) >>
          [](Match& _) { return _(Value); },

        // errors
        In(Top) * T(File)[File] >>
          [](Match& _) { return err(_[File], "Invalid JSON"); },

        In(ArrayGroup) * T(Colon)[Colon] >>
          [](Match& _) { return err(_[Colon], "Invalid colon in array"); },
      }};

    return groups;
  }

  PassDef structure()
  {
    PassDef structure = {
      "structure",
      wf_structure,
      dir::bottomup,
      {
        In(ArrayGroup) * (Start * ValueTokens[Value]) >>
          [](Match& _) { return (Value << _(Value)); },

        In(ArrayGroup) * (T(Value)[Lhs] * T(Comma) * ValueTokens[Rhs]) >>
          [](Match& _) { return Seq << _(Lhs) << (Value << _(Rhs)); },

        In(Array) * (T(ArrayGroup) << (T(Value)++[Array] * End)) >>
          [](Match& _) { return Seq << _[Array]; },

        In(Array) * T(Value)[Value] >>
          [](Match& _) { return _(Value)->front(); },

        In(ObjectGroup) *
            (Start * T(String)[Lhs] * T(Colon) * ValueTokens[Rhs]) >>
          [](Match& _) { return (Member << _(Lhs) << _(Rhs)); },

        In(ObjectGroup) *
            (T(Member)[Member] * T(Comma) * T(String)[Lhs] * T(Colon) *
             ValueTokens[Rhs]) >>
          [](Match& _) {
            return Seq << _(Member) << (Member << _(Lhs) << _(Rhs));
          },

        In(Object) * (T(ObjectGroup) << (T(Member)++[Object] * End)) >>
          [](Match& _) { return Seq << _[Object]; },
      }};

    structure.post([&](Node n) {
      return invalid_tokens(n, {ObjectGroup}, "Invalid object") +
        invalid_tokens(n, {ArrayGroup}, "Invalid array");
    });

    return structure;
  };

  std::vector<Pass> passes()
  {
    return {groups(), structure()};
  }
}
