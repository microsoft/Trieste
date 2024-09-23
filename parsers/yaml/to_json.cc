#include "internal.h"
#include "trieste/json.h"

namespace
{
  using namespace trieste;
  using namespace trieste::yaml;
  using namespace wf::ops;

  const auto KeyValue = TokenDef("yaml-keyvalue");

  const auto ValueToken =
    T(Mapping,
      Sequence,
      Value,
      Int,
      Float,
      FlowMapping,
      FlowSequence,
      Null,
      True,
      False,
      Hex,
      Empty);

  inline const auto wf_strings_tokens =
    wf_tokens - (Literal | Folded | Plain | DoubleQuote | SingleQuote);
  inline const auto wf_strings_flow_tokens =
    wf_flow_tokens - (Plain | DoubleQuote | SingleQuote);

  // clang-format off
  inline const auto wf_strings =
    yaml::wf
    | (Document <<= Directives * DocumentStart * (Value >>= wf_strings_tokens) * DocumentEnd)
    | (Sequence <<= wf_strings_tokens++)
    | (FlowSequence <<= wf_strings_flow_tokens++)
    | (FlowMappingItem <<= (Key >>= wf_strings_flow_tokens) * (Value >>= wf_strings_flow_tokens))
    | (MappingItem <<= (Key >>= wf_strings_tokens) * (Value >>= wf_strings_tokens))
    ;
  // clang-format on

  inline const auto wf_lookup_tokens = wf_strings_tokens - Alias;
  inline const auto wf_lookup_flow_tokens = wf_strings_tokens - Alias;

  // clang-format off
  inline const auto wf_lookup =
    wf_strings
    | (Document <<= Directives * DocumentStart * (Value >>= wf_lookup_tokens) * DocumentEnd)
    | (Sequence <<= wf_lookup_tokens++)
    | (FlowSequence <<= wf_lookup_flow_tokens++)
    | (FlowMappingItem <<= (Key >>= wf_lookup_flow_tokens) * (Value >>= wf_lookup_flow_tokens))
    | (MappingItem <<= (Key >>= wf_lookup_tokens) * (Value >>= wf_lookup_tokens))
    ;
  // clang-format on

  inline const auto wf_tags_tokens =
    wf_lookup_tokens - (TagValue | AnchorValue);
  inline const auto wf_tags_flow_tokens =
    wf_lookup_flow_tokens - (TagValue | AnchorValue);

  // clang-format off
  inline const auto wf_tags =
    wf_lookup
    | (Document <<= Directives * DocumentStart * (Value >>= wf_tags_tokens) * DocumentEnd)
    | (Sequence <<= wf_tags_tokens++)
    | (FlowSequence <<= wf_tags_flow_tokens++)
    | (FlowMappingItem <<= (Key >>= wf_tags_flow_tokens) * (Value >>= wf_tags_flow_tokens))
    | (MappingItem <<= (Key >>= wf_tags_tokens) * (Value >>= wf_tags_tokens))
    ;
  // clang-format off

  inline const auto wf_value_tokens = Mapping | FlowMapping | Sequence |
    FlowSequence | Int | Float | Hex | True | False | Null | Value | Empty;

  // clang-format off
  inline const auto wf_value =
    wf_strings
    | (Top <<= json::Value++)
    | (Sequence <<= json::Value++)
    | (FlowSequence <<= json::Value++)
    | (FlowMapping <<= json::Member++)
    | (Mapping <<= json::Member++)
    | (json::Member <<= json::Key * json::Value)
    | (json::Value <<= wf_value_tokens)
    ;
  // clang-format on

  PassDef strings()
  {
    return PassDef{
      "strings",
      wf_strings,
      dir::bottomup | dir::once,
      {
        T(Literal, Folded, Plain)[Block] >>
          [](Match& _) {
            std::ostringstream os;
            os << '"';
            block_to_string(os, _(Block));
            os << '"';
            return Value ^ os.str();
          },

        T(DoubleQuote, SingleQuote)[Value] >>
          [](Match& _) {
            std::ostringstream os;
            os << '"';
            quote_to_string(os, _(Value));
            os << '"';
            return Value ^ os.str();
          },

        T(Value)[Value] >>
          [](Match& _) {
            std::ostringstream os;
            os << '"' << escape_chars(_(Value)->location().view(), {'\\', '"'})
               << '"';
            return Value ^ os.str();
          },
      }};
  }

  PassDef lookup()
  {
    PassDef pass = PassDef{
      "lookup",
      wf_lookup,
      dir::bottomup | dir::once,
      {
        T(Alias)[Alias] >>
          [](Match& _) {
            Nodes defs = _(Alias)->lookup();
            if (defs.empty())
            {
              return err(_(Alias), "Invalid alias");
            }
            else
            {
              return defs.back()->clone();
            }
          },

      }};

    return pass;
  }

  PassDef tags()
  {
    return PassDef{
      "tags",
      wf_tags,
      dir::bottomup | dir::once,
      {
        T(AnchorValue) << (T(Anchor) * ValueToken[Value]) >>
          [](Match& _) { return _(Value); },

        T(TagValue)[TagValue] >> [](Match& _) -> Node {
          std::string handle = "";
          Node prefix_node = _(TagValue) / TagPrefix;
          Nodes defs = prefix_node->lookup();
          if (defs.empty())
          {
            return err(prefix_node, "Invalid tag");
          }

          Node handle_node = defs.front();
          if (handle_node != nullptr)
          {
            handle = handle_node->back()->location().view();
          }
          Node name_node = _(TagValue) / TagName;
          auto name = name_node->location().view();
          Node value = _(TagValue) / Value;
          if (
            (handle == "tag:yaml.org,2002:" && name == "str") ||
            (handle == "!" && name.empty()))
          {
            if (value != Value)
            {
              std::ostringstream os;
              os << '"' << value->location().view() << '"';
              value = Value ^ os.str();
            }
          }

          return value;
        },

        // errors

        T(AnchorValue)[AnchorValue] >>
          [](Match& _) { return err(_(AnchorValue), "Invalid anchor"); },
      }};
  }

  PassDef value()
  {
    return PassDef{
      "value",
      wf_value,
      dir::bottomup,
      {
        T(MappingItem, FlowMappingItem)[MappingItem] >>
          [](Match& _) {
            Node key = _(MappingItem) / Key;
            Node value = _(MappingItem) / Value;
            return json::Member << (json::Key << key) << (json::Value << value);
          },

        T(json::Key) << T(Value)[Key] >>
          [](Match& _) {
            Location loc = _(Key)->location();
            auto view = loc.view();
            if (!view.empty() && view.front() == '"' && view.back() == '"')
            {
              loc.pos += 1;
              loc.len -= 2;
            }
            return json::Key ^ loc;
          },

        T(json::Key) << T(Int, Float, Hex, True, False, Null)[Key] >>
          [](Match& _) { return json::Key ^ _(Key)->location(); },

        T(json::Key) << T(Empty) >> [](Match&) { return json::Key ^ ""; },

        In(Sequence, FlowSequence) * ValueToken[Value] >>
          [](Match& _) { return json::Value << _(Value); },

        T(Document)
            << (T(Directives) * T(DocumentStart) * ValueToken[Value] *
                T(DocumentEnd)) >>
          [](Match& _) { return json::Value << _(Value); },

        T(Stream)
            << (T(Directives) *
                (T(Documents) << (T(json::Value)++[Stream] * End))) >>
          [](Match& _) { return Seq << _[Stream]; },

        // errors

        T(json::Key) << T(FlowMapping, FlowSequence, Mapping, Sequence)[Key] >>
          [](Match& _) { return err(_(Key), "Complex keys not supported"); },
      }};
  }

  PassDef convert()
  {
    return PassDef{
      "convert",
      json::wf,
      dir::bottomup | dir::once,
      {
        T(json::Value) << T(Int, Float)[Value] >>
          [](Match& _) { return json::Number ^ _(Value); },

        T(json::Value) << T(True)[Value] >>
          [](Match& _) { return json::True ^ _(Value); },

        T(json::Value) << T(False)[Value] >>
          [](Match& _) { return json::False ^ _(Value); },

        T(json::Value) << T(Null)[Value] >>
          [](Match& _) { return json::Null ^ _(Value); },

        T(json::Value) << T(Empty) >>
          [](Match&) { return json::Null ^ "null"; },

        T(json::Value) << T(Hex)[Hex] >>
          [](Match& _) {
            std::string hex(_(Hex)->location().view());
            std::ostringstream os;
            os << std::stoull(hex, 0, 16);
            return json::Number ^ os.str();
          },

        T(json::Value) << T(Value)[Value] >>
          [](Match& _) { return json::String ^ _(Value)->location(); },

        T(json::Value) << T(Mapping, FlowMapping)[Mapping] >>
          [](Match& _) { return json::Object << *_[Mapping]; },

        T(json::Value) << T(Sequence, FlowSequence)[Sequence] >>
          [](Match& _) { return json::Array << *_[Sequence]; },
      }};
  }
}

namespace trieste
{
  namespace yaml
  {
    Rewriter to_json()
    {
      return Rewriter{
        "yaml_to_json",
        {strings(), lookup(), tags(), value(), convert()},
        yaml::wf};
    }
  }
}
