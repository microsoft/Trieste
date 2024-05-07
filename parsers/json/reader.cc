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

  bool value_equal(Node lhs, Node rhs);

  bool object_equal(Node lhs, Node rhs)
  {
    if (lhs->size() != rhs->size())
    {
      return false;
    }

    std::vector<Node> lhs_members(lhs->begin(), lhs->end());
    std::vector<Node> rhs_members(rhs->begin(), rhs->end());
    std::sort(lhs_members.begin(), lhs_members.end(), [](Node x, Node y) {
      return x->front()->location().view() < y->front()->location().view();
    });
    std::sort(rhs_members.begin(), rhs_members.end(), [](Node x, Node y) {
      return x->front()->location().view() < y->front()->location().view();
    });

    for (std::size_t i = 0; i < lhs_members.size(); ++i)
    {
      if (
        lhs_members[i]->front()->location().view() !=
        rhs_members[i]->front()->location().view())
      {
        return false;
      }
      if (!value_equal(lhs_members[i]->back(), rhs_members[i]->back()))
      {
        return false;
      }
    }

    return true;
  }

  bool array_equal(Node lhs, Node rhs)
  {
    if (lhs->size() != rhs->size())
    {
      return false;
    }

    for (std::size_t i = 0; i < lhs->size(); ++i)
    {
      if (!value_equal(lhs->at(i), rhs->at(i)))
      {
        return false;
      }
    }

    return true;
  }

  bool value_equal(Node lhs, Node rhs)
  {
    if (lhs->type() != rhs->type())
    {
      return false;
    }

    if (lhs->type() == Object)
    {
      return object_equal(lhs, rhs);
    }

    if (lhs->type() == Array)
    {
      return array_equal(lhs, rhs);
    }

    if (lhs->type() == Top)
    {
      return array_equal(lhs, rhs);
    }

    if (lhs->type() == Number)
    {
      double lhs_value = std::stod(std::string(lhs->location().view()));
      double rhs_value = std::stod(std::string(rhs->location().view()));
      return lhs_value == rhs_value;
    }

    return lhs->location().view() == rhs->location().view();
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
            if (values.first == values.second)
            {
              return err("Invalid JSON");
            }

            if (
              std::distance(values.first, values.second) > 1 && !allow_multiple)
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
}

namespace trieste
{
  namespace json
  {
    Reader reader(bool allow_multiple)
    {
      return Reader{"json", {groups(allow_multiple), structure()}, parser()};
    }

    bool equal(Node lhs, Node rhs)
    {
      return value_equal(lhs, rhs);
    }
  }
}
