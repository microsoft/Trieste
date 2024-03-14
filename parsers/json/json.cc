#include "json.h"

namespace
{
  using namespace trieste;
  using namespace trieste::json;

  std::size_t
  invalid_tokens(Node node, const std::map<Token, std::string>& token_messages)
  {
    std::size_t changes = 0;

    node->traverse([&](Node& n){
      if (n->type() == Error)
        return false;

      for (auto child : *n)
      {
        if (token_messages.count(child->type()) > 0)
        {
          n->replace(child, err(child, token_messages.at(child->type())));
          changes += 1;
        }
      }
      return true;
    });

    return changes;
  }
}

namespace trieste::json
{

  const auto ValueToken = T(Object, Array, String, Number, True, False, Null);

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
            (T(File) << ((T(Group) << (ValueToken[Value] * End)) * End)) >>
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
        In(ArrayGroup) * (Start * ValueToken[Value]) >>
          [](Match& _) { return (Value << _(Value)); },

        In(ArrayGroup) * (T(Value)[Lhs] * T(Comma) * ValueToken[Rhs]) >>
          [](Match& _) { return Seq << _(Lhs) << (Value << _(Rhs)); },

        In(Array) * (T(ArrayGroup) << (T(Value)++[Array] * End)) >>
          [](Match& _) { return Seq << _[Array]; },

        In(Array) * T(Value)[Value] >>
          [](Match& _) { return _(Value)->front(); },

        In(ObjectGroup) *
            (Start * T(String)[Lhs] * T(Colon) * ValueToken[Rhs]) >>
          [](Match& _) { return (Member << _(Lhs) << _(Rhs)); },

        In(ObjectGroup) *
            (T(Member)[Member] * T(Comma) * T(String)[Lhs] * T(Colon) *
             ValueToken[Rhs]) >>
          [](Match& _) {
            return Seq << _(Member) << (Member << _(Lhs) << _(Rhs));
          },

        In(Object) * (T(ObjectGroup) << (T(Member)++[Object] * End)) >>
          [](Match& _) { return Seq << _[Object]; },
      }};

    structure.post([&](Node n) {
      return invalid_tokens(
        n, {{ObjectGroup, "Invalid object"}, {ArrayGroup, "Invalid array"}});
    });

    return structure;
  };

  std::vector<Pass> passes()
  {
    return {groups(), structure()};
  }
}
