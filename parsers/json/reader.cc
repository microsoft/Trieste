#include "internal.h"

namespace
{
  using namespace trieste;
  using namespace trieste::json;

  std::size_t
  invalid_tokens(Node node, const std::map<Token, std::string>& token_messages)
  {
    std::size_t changes = 0;

    node->traverse([&](Node& n) {
      if (n->type() == Error)
        return false;

      for (Node& child : *n)
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


  // clang-format off
  inline const auto wf_groups =
    (Top <<= wf_value_tokens++[1])
    | (Object <<= ObjectGroup)
    | (Array <<= ArrayGroup)
    | (ObjectGroup <<= (wf_value_tokens | Colon | Comma)++)
    | (ArrayGroup <<= (wf_value_tokens | Comma)++)
    ;
  // clang-format on

  const auto ValueToken = T(Object, Array, String, Number, True, False, Null);

  PassDef groups(bool allow_multiple)
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
            (T(File) << ((T(Group) << (ValueToken++[Value] * End)) * End)) >>
          [allow_multiple](Match& _) {
            auto values = _[Value];
            if (values.empty())
            {
              return err("Invalid JSON");
            }

            if (values.size() > 1 && !allow_multiple)
            {
              return err("Multiple top-level values not allowed");
            }

            return Seq << _[Value];
          },

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
      json::wf,
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
          [](Match& _) {
            Location key = _(Lhs)->location();
            key.pos += 1;
            key.len -= 2;
            return (Member << (Key ^ key) << _(Rhs));
          },

        In(ObjectGroup) *
            (T(Member)[Member] * T(Comma) * T(String)[Lhs] * T(Colon) *
             ValueToken[Rhs]) >>
          [](Match& _) {
            Location key = _(Lhs)->location();
            key.pos += 1;
            key.len -= 2;
            return Seq << _(Member) << (Member << (Key ^ key) << _(Rhs));
          },

        In(Object) * (T(ObjectGroup) << (T(Member)++[Object] * End)) >>
          [](Match& _) { return Seq << _[Object]; },
      }};

    structure.post([&](Node n) {
      return invalid_tokens(
        n, {{ObjectGroup, "Invalid object"}, {ArrayGroup, "Invalid array"}});
    });

    return structure;
  }
}

namespace trieste
{
  namespace json
  {
    Reader reader(bool allow_multiple)
    {
      return Reader{"json", {groups(allow_multiple), structure()}, parser()};
    }
  }

}
