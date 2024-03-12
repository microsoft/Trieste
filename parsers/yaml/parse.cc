#include "trieste/parse.h"

#include "yaml.h"

namespace
{
  using namespace trieste::yaml;

  const std::string alpha =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const std::string numeric = "0123456789";
  const std::string whitespace = " \t";
  const std::string alphanumeric = alpha + numeric;
  const std::string quoted = alphanumeric + whitespace + "\n";

  template<typename T>
  std::string
  rand_string(T& rnd, std::size_t min_length = 0, std::size_t max_length = 10)
  {
    std::ostringstream buf;
    std::size_t length = rnd() % (max_length - min_length) + min_length;
    for (std::size_t i = 0; i < length; ++i)
    {
      buf << alphanumeric[rnd() % alphanumeric.size()];
    }
    return buf.str();
  }

  template<typename T>
  std::string rand_quoted(
    T& rnd, char quote, std::size_t min_length = 0, std::size_t max_length = 10)
  {
    std::ostringstream buf;
    std::size_t length = rnd() % (max_length - min_length) + min_length;
    buf << quote;
    for (std::size_t i = 0; i < length; ++i)
    {
      buf << quoted[rnd() % quoted.size()];
    }
    buf << quote;
    return buf.str();
  }

  template<typename T>
  std::string rand_whitespace(
    T& rnd, std::size_t min_length = 0, std::size_t max_length = 10)
  {
    std::ostringstream buf;
    std::size_t length = rnd() % (max_length - min_length) + min_length;
    buf << " ";
    for (std::size_t i = 0; i < length; ++i)
    {
      buf << whitespace[rnd() % whitespace.size()];
    }
    return buf.str();
  }

  template<typename T>
  std::string rand_int(T& rnd, int min = -50, int max = 50)
  {
    int range = max - min;
    std::ostringstream buf;
    buf << rnd() % range + min;
    return buf.str();
  }

  template<typename T>
  std::string rand_float(T& rnd)
  {
    std::ostringstream buf;
    std::uniform_real_distribution<> dist(-10.0, 10.0);
    buf << dist(rnd);
    return buf.str();
  }

  template<typename T>
  std::string rand_hex(T& rnd)
  {
    std::ostringstream buf;
    buf << "0x";
    for (int i = 0; i < 8; ++i)
    {
      buf << "0123456789ABCDEF"[rnd() % 16];
    }
    return buf.str();
  }

  typedef std::shared_ptr<std::set<std::string_view>> Anchors;

  bool is_alias_key(const Anchors& anchors, const std::string_view& query)
  {
    std::string anchor(query);
    anchor[0] = '&';
    anchor.erase(
      std::find_if(
        anchor.rbegin(),
        anchor.rend(),
        [](unsigned char ch) { return !std::isspace(ch); })
        .base(),
      anchor.end());

    return anchors->find(anchor) != anchors->end();
  }

  void handle_indent_chomp(trieste::detail::Make& m, std::size_t index)
  {
    if (m.match(index).len == 0)
    {
      return;
    }

    auto view = m.match(index).view();
    if (std::isdigit(view[0]))
    {
      if (view.size() > 1 || view[0] == '0')
      {
        m.error("Invalid indent");
        return;
      }

      m.add(IndentIndicator, index);
    }
    else
    {
      m.add(ChompIndicator, index);
    }
  }
}

namespace trieste::yaml
{
  Parse parser()
  {
    Anchors anchors = std::make_shared<std::set<std::string_view>>();
    std::shared_ptr<std::size_t> flow_level = std::make_shared<std::size_t>(0);

    Parse p(depth::file, wf_parse);
    p("start", {R"(^)" >> [](auto& m) {
        m.push(Stream);
        m.mode("directives");
      }});

    p("directives",
      {
        R"(([ \t]*)(#[^\r\n]*))" >> [](auto&) { return; },

        R"((\r?\n)(#[^\r\n]*))" >> [](auto&) { return; },

        R"(\r?\n)" >> [](auto&) { return; },

        R"((%YAML[ \t]+([0-9])\.([0-9]))([ \t]+[^#\r\n]+)?(?:\s+#[^\r\n]*)*[ \t]*\r?\n([ \t]*))" >>
          [](auto& m) {
            if (m.match(4).len > 0)
            {
              m.error("Extra words on %YAML directive", 4);
              return;
            }

            auto major = m.match(2).view()[0] - '0';
            auto minor = m.match(3).view()[0] - '0';
            if (major != 1 || minor > 2)
            {
              std::cerr << "Parsing YAML files with version greater than 1.2 "
                           "may result in unexpected behavior."
                        << std::endl;
            }
            m.add(VersionDirective, 1);
          },

        R"(%YAML [^\r\n]*\r?\n)" >>
          [](auto& m) { m.error("Invalid %YAML directive"); },

        R"(%TAG ([^\s]+) ([^\s]+)(?:\s+#[^\r\n]*)*\r?\n([ \t]*))" >>
          [](auto& m) {
            m.push(TagDirective);
            m.add(TagPrefix, 1);
            m.add(TagHandle, 2);
            m.term({TagDirective});
          },

        R"((%([[:alpha:]]+) ?.*)(?:\s+#[^\r\n]*)*\r?\n([ \t]*))" >>
          [](auto& m) {
            std::cerr << "Unknown directive: " << m.match(1).view()
                      << std::endl;
            m.add(UnknownDirective, 1);
          },

        R"(([ \t]*\.\.\.)(?:\r?\n| )+)" >> [](auto&) { return; },

        R"(([ \t]*)(---)([ \t]+))" >>
          [](auto& m) {
            m.push(Document);
            m.add(DocumentStart, 2);
            m.add(Whitespace, 3);
            m.mode("document");
          },

        R"(([ \t]*)(---)(\r?\n))" >>
          [](auto& m) {
            m.push(Document);
            m.add(DocumentStart, 2);
            m.add(NewLine, 3);
            m.mode("document");
          },

        "(^)" >>
          [](auto& m) {
            m.push(Document);
            m.mode("document");
          },
      });

    p("document",
      {
        R"(([ \t]*)(#[^\r\n]*))" >>
          [](auto& m) {
            if (m.match(1).len > 0)
            {
              m.add(Whitespace, 1);
            }
            m.add(Comment, 2);
          },

        R"([ \t]+)" >> [](auto& m) { m.add(Whitespace); },

        R"((\r?\n)(#[^\r\n]*))" >>
          [](auto& m) {
            m.add(NewLine, 1);
            m.add(Comment, 2);
          },

        R"(\r?\n)" >> [](auto& m) { m.add(NewLine); },

        R"((%[[:alpha:]]+(?:[ \t]+[^\s]+))([ \t]+#[^\r\n]*)?)" >>
          [](auto& m) {
            m.add(MaybeDirective, 1);
            if (m.match(2).len > 0)
            {
              m.add(Comment, 2);
            }
          },

        R"((---)(\r?\n))" >>
          [](auto& m) {
            m.term({Document});
            m.push(Document);
            m.add(DocumentStart, 1);
            m.add(NewLine, 2);
          },

        R"((---)([ \t]+))" >>
          [](auto& m) {
            m.term({Document});
            m.push(Document);
            m.add(DocumentStart, 1);
            m.add(Whitespace, 2);
          },

        R"((\.\.\.)([ \t]*|[ \t]+#[^\r\n]*)?\r?\n)" >>
          [](auto& m) {
            m.add(DocumentEnd, 1);
            m.term({Document});
            m.mode("directives");
          },

        R"(\.\.\.\s+([^\r\n]+))" >>
          [](auto& m) {
            m.error("Invalid content after document end marker", 1);
          },

        R"(-[ \t]+)" >> [](auto& m) { m.add(Hyphen); },

        R"(-$)" >> [](auto& m) { m.add(Hyphen); },

        R"((-)\r?(\n))" >>
          [](auto& m) {
            m.add(Hyphen, 1);
            m.add(NewLine, 2);
          },

        R"(\?[ \t])" >> [](auto& m) { m.add(Key); },

        R"((\?)\r?(\n))" >>
          [](auto& m) {
            m.add(Key, 1);
            m.add(NewLine, 2);
          },

        R"((\{))" >>
          [flow_level](auto& m) {
            m.push(FlowMapping);
            m.add(FlowMappingStart);
            m.mode("flow");
            *flow_level = 1;
          },

        R"((\[))" >>
          [flow_level](auto& m) {
            m.push(FlowSequence);
            m.add(FlowSequenceStart);
            m.mode("flow");
            *flow_level = 1;
          },

        R"(([[a-zA-Z0-9\?:-](?:[^\s]|[^:\r\n] [^\s#])*) *(:)(?:[ \t]+|\r?(\n)|(,)))" >>
          [anchors](auto& m) {
            m.add(Value, 1);
            m.add(Colon, 2);
            if (m.match(3).len > 0)
            {
              m.add(NewLine, 3);
            }
            if (m.match(4).len > 0)
            {
              m.add(Comma, 4);
            }
          },

        R"((\*([^\[\]\{\}\, \r\n]+)(:))(?:[ \t]+|\r?(\n)))" >>
          [anchors](auto& m) {
            if (is_alias_key(anchors, m.match(1).view()))
            {
              // this is not a map key, but rather an alias that ends in a colon
              m.add(Alias, 2);
              m.extend(Alias, 3);
            }
            else
            {
              m.add(Alias, 2);
              m.add(Colon, 3);
            }
            if (m.match(3).len > 0)
            {
              m.add(NewLine, 3);
            }
          },

        R"((:)(?:[ \t]+|\r?(\n)))" >>
          [](auto& m) {
            m.add(Colon, 1);
            if (m.match(2).len > 0)
            {
              m.add(NewLine, 2);
            }
          },

        R"(:$)" >> [](auto& m) { m.add(Colon); },

        R"((&[^\[\]\{\}\, \r\n]+)(?:[ \t]+|\r?(\n)))" >>
          [anchors](auto& m) {
            m.add(Anchor, 1);
            anchors->insert(m.match(1).view());
            if (m.match(2).len > 0)
            {
              m.add(NewLine, 2);
            }
          },

        // verbatim-tag
        R"((![0-9A-Za-z\-]+!|!!|!)(<(?:[\w#;\/\?:@&=+$,_.!~*'()[\]{}]|%\d+)+>)(?:[ \t]+|\r?(\n)))" >>
          [](auto& m) {
            m.push(Tag);
            m.add(TagPrefix, 1);
            m.add(VerbatimTag, 2);
            m.term({Tag});

            if (m.match(3).len > 0)
            {
              m.add(NewLine, 3);
            }
          },

        // ns-shorthand-tag
        R"((![0-9A-Za-z\-]+!|!!|!)((?:[\w#;\/\?:@&=+$,_.!~*'()[\]{}]|%\d+)+)(?:[ \t]+|\r?(\n)))" >>
          [](auto& m) {
            m.push(Tag);
            m.add(TagPrefix, 1);
            m.add(ShorthandTag, 2);
            m.term({Tag});

            if (m.match(3).len > 0)
            {
              m.add(NewLine, 3);
            }
          },

        // non-specific-tag
        R"((!)(?:[ \t]+|\r?(\n)))" >>
          [](auto& m) {
            m.push(Tag);
            m.add(TagPrefix, 1);
            m.term({Tag});
            if (m.match(2).len > 0)
            {
              m.add(NewLine, 3);
            }
          },

        R"(\*[^\[\]\{\}\, \r\n]+)" >> [](auto& m) { m.add(Alias); },

        R"(([>|\|])([0-9]|[+-])?([0-9]|[+-])?(#)?)" >>
          [](detail::Make& m) {
            auto block_match = m.match(1).view();
            if (block_match[0] == '|')
            {
              m.add(Literal);
            }
            else
            {
              m.add(Folded);
            }

            handle_indent_chomp(m, 2);
            handle_indent_chomp(m, 3);

            if (m.match(4).len > 0)
            {
              m.error(
                "Comment without whitespace after block scalar indicator", 4);
            }
          },

        R"('(?:''|[^'])*'(#)?)" >>
          [](auto& m) {
            if (m.match(1).len > 0)
            {
              m.error(
                "Comment without whitespace after singlequoted scalar", 1);
            }

            m.add(SingleQuote);
          },

        R"("(?:\\\\|\\"|[^"])*"(#)?)" >>
          [](auto& m) {
            if (m.match(1).len > 0)
            {
              m.error(
                "Comment without whitespace after doublequoted scalar", 1);
            }

            m.add(DoubleQuote);
          },

        R"(")" >>
          [](auto& m) {
            m.error("Double quoted string without closing quote");
          },

        R"(')" >>
          [](auto& m) {
            m.error("Single quoted string without closing quote");
          },

        R"((?:[^\s:\?-]|:[^\s]|\?[^\s]|-[^\s])(?:[^\r\n \t:#]|:[^\s]|#[^\s]|[ \t][^\r\n \t:#])*)" >>
          [](auto& m) { m.add(Value); },
      });

    p("flow",
      {
        "---" >>
          [](auto& m) { m.error("Invalid document marker in flow style"); },

        R"(\.\.\.)" >>
          [](auto& m) { m.error("Invalid document marker in flow style"); },

        R"(([ \t]+)(#[^\r\n]*))" >>
          [](auto& m) {
            m.add(Comment, 2);
            return;
          },

        R"([ \t]+)" >>
          [](auto& m) {
            m.term();
            return;
          },

        R"((\r?\n)(#[^\r\n]*))" >>
          [](auto& m) {
            m.add(Comment, 2);
            return;
          },

        R"(\r?\n)" >>
          [](auto& m) {
            m.term();
            return;
          },

        R"((\?)\s+)" >> [](auto& m) { m.add(Key, 1); },

        R"((\{))" >>
          [flow_level](auto& m) {
            m.push(FlowMapping);
            m.add(FlowMappingStart);
            *flow_level += 1;
          },

        R"((\[))" >>
          [flow_level](auto& m) {
            m.push(FlowSequence);
            m.add(FlowSequenceStart);
            *flow_level += 1;
          },

        R"((\}))" >>
          [flow_level](auto& m) {
            m.add(FlowMappingEnd);
            m.term({FlowMapping});
            *flow_level -= 1;
            if (*flow_level == 0)
            {
              m.mode("document");
            }
          },

        R"((\])(#)?)" >>
          [flow_level](auto& m) {
            if (m.match(2).len > 0)
            {
              m.error("Invalid comment after end of flow sequence", 2);
            }

            m.add(FlowSequenceEnd, 1);
            m.term({FlowSequence});
            *flow_level -= 1;
            if (*flow_level == 0)
            {
              m.mode("document");
            }
          },

        R"((,)(#)?)" >>
          [](auto& m) {
            m.add(Comma);
            if (m.match(2).len > 0)
            {
              m.error("Invalid comment after comma", 2);
            }
          },

        R"((:)\s+(:)?)" >>
          [](auto& m) {
            m.add(Colon, 1);
            if (m.match(2).len > 0)
            {
              m.add(Value, 2);
            }
          },

        R"((:)(,))" >> [](auto& m) { m.add(Colon, 1), m.add(Comma, 2); },

        R"((:)?('(?:''|[^'])*'))" >>
          [](auto& m) {
            if (m.match(1).len > 0)
            {
              m.add(Colon, 1);
            }
            m.add(SingleQuote, 2);
          },

        R"((:)?("(?:\\\\|\\"|[^"])*"))" >>
          [](auto& m) {
            if (m.match(1).len > 0)
            {
              m.add(Colon, 1);
            }
            m.add(DoubleQuote, 2);
          },

        R"(")" >>
          [](auto& m) {
            m.error("Double quoted string without closing quote");
          },

        R"(')" >>
          [](auto& m) {
            m.error("Single quoted string without closing quote");
          },

        R"((&[^\[\]\{\}\, \r\n]+)(?:[ \t]+|\r?\n))" >>
          [anchors](auto& m) {
            m.add(Anchor, 1);
            anchors->insert(m.match(1).view());
          },

        // verbatim-tag
        R"((![0-9A-Za-z\-]*!|!!|!)(<(?:[\w#;\/\?:@&=+$,_.!~*'()]|%\d+)+>)(?:[ \t]+|\r?\n|(,)))" >>
          [](auto& m) {
            m.push(Tag);
            m.add(TagPrefix, 1);
            m.add(VerbatimTag, 2);
            m.term({Tag});
            if (m.match(3).len > 0)
            {
              m.add(Comma, 3);
            }
          },

        // ns-shorthand-tag
        R"((![0-9A-Za-z\-]*!|!!|!)((?:[\w#;\/\?:@&=+$_.~*'()]|%\d\d)+)(?:[ \t]+|\r?\n|(,)))" >>
          [](auto& m) {
            m.push(Tag);
            m.add(TagPrefix, 1);
            m.add(ShorthandTag, 2);
            m.term({Tag});
            if (m.match(3).len > 0)
            {
              m.add(Comma, 3);
            }
          },

        // non-specific-tag
        R"((!)(?:[ \t]+|\r?\n))" >>
          [](auto& m) {
            m.push(Tag);
            m.add(TagPrefix, 1);
            m.term({Tag});
          },

        R"(\*[^\[\]\{\}\, \r\n]+)" >> [](auto& m) { m.add(Alias); },

        R"((?:\d+-)+\d*)" >> [](auto& m) { m.add(Value); },

        R"(\-?[[:digit:]]+\.[[:digit:]]+(?:e[+-]?[[:digit:]]+)?\b)" >>
          [](auto& m) { m.add(Float); },

        R"(\-?[[:digit:]]+\b)" >> [](auto& m) { m.add(Int); },

        R"(0x[0-9A-Fa-f]+\b)" >> [](auto& m) { m.add(Hex); },

        R"(null\b)" >> [](auto& m) { m.add(Null); },

        R"(true\b)" >> [](auto& m) { m.add(True); },

        R"(false\b)" >> [](auto& m) { m.add(False); },

        R"(((?:[^\s][ \t]*\?|\?[^ \t]|[^\s:,\{\}\[\]]|[ \t]+[^:\?\-\s\[\]\{\},#]|:[^\s,])+))" >>
          [](auto& m) { m.extend(Value); },
      });

    p.done([anchors](detail::Make& m) {
      anchors->clear();
      while (!m.in(Stream))
      {
        m.term({Document, FlowMapping, FlowSequence});
      }
      m.term({Stream});
    });

    p.gen({
      Int >> [](auto& rnd) { return rand_int(rnd); },
      Float >> [](auto& rnd) { return rand_float(rnd); },
      Hex >> [](auto& rnd) { return rand_hex(rnd); },
      True >> [](auto&) { return "true"; },
      False >> [](auto&) { return "false"; },
      Null >> [](auto&) { return "null"; },
      Value >>
        [](auto& rnd) {
          std::string value;
          switch (rnd() % 10)
          {
            case 0:
              value = rand_int(rnd);
              break;

            case 1:
              value = rand_float(rnd);
              break;

            case 2:
              value = "true";
              break;

            case 3:
              value = "false";
              break;

            case 4:
              value = "null";
              break;

            case 5:
              value = rand_hex(rnd);
              break;

            default:
              value = rand_string(rnd);
              break;
          }
          return value;
        },
      DocumentStart >> [](auto&) { return "---"; },
      DocumentEnd >> [](auto&) { return "..."; },
      NewLine >> [](auto&) { return "\n"; },
      Comment >> [](auto& rnd) { return "# " + rand_string(rnd); },
      TagPrefix >> [](auto& rnd) { return "!" + rand_string(rnd, 8) + "!"; },
      TagHandle >> [](auto& rnd) { return rand_string(rnd, 1); },
      VerbatimTag >> [](auto& rnd) { return "<" + rand_string(rnd, 1) + ">"; },
      ShorthandTag >> [](auto& rnd) { return rand_string(rnd); },
      Literal >> [](auto&) { return "|"; },
      Folded >> [](auto&) { return ">"; },
      SingleQuote >> [](auto& rnd) { return rand_quoted(rnd, '\'', 0, 20); },
      DoubleQuote >> [](auto& rnd) { return rand_quoted(rnd, '"', 0, 20); },
      Anchor >> [](auto& rnd) { return "&" + rand_string(rnd, 12, 16); },
      Alias >> [](auto& rnd) { return "*" + rand_string(rnd, 12, 16); },
      FlowMappingStart >> [](auto&) { return "{"; },
      FlowMappingEnd >> [](auto&) { return "}"; },
      FlowSequenceStart >> [](auto&) { return "["; },
      FlowSequenceEnd >> [](auto&) { return "]"; },
      Comma >> [](auto&) { return ","; },
      Colon >> [](auto&) { return ":"; },
      Key >> [](auto&) { return "?"; },
      IndentIndicator >> [](auto& rnd) { return rand_int(rnd, 1, 9); },
      AbsoluteIndent >> [](auto& rnd) { return rand_int(rnd, 1, 9); },
      ChompIndicator >> [](auto& rnd) { return rnd() % 2 == 0 ? "+" : "-"; },
      Hyphen >> [](auto&) { return "-"; },
      Whitespace >> [](auto& rnd) { return rand_whitespace(rnd); },
      WhitespaceLine >>
        [](auto& rnd) {
          std::string line = rand_whitespace(rnd);
          if (rnd() % 2 == 1)
          {
            line += "# " + rand_string(rnd);
          }
          return line;
        },
      EmptyLine >> [](auto&) { return "\n"; },
      BlockLine >> [](auto& rnd) { return rand_string(rnd); },
    });

    return p;
  }
}
