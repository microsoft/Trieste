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

    // YAML starts out with the possibility of one or more directives that will
    // apply to a subsequent document.
    p("directives",
      {
        R"([ \t]+)" >> [](auto&) { return; },

        R"(\r?\n)" >> [](auto&) { return; },

        R"(#[^\r\n]*)" >> [](auto&) { return; },

        // YAML directive
        // %YAML[ \t]+ : The text "%YAML" followed by one or more spaces or tabs
        // [0-9] : A single digit
        // \.[0-9] : A period followed by a single digit
        // [ \t]+[^#\r\n]+ : One or more spaces or tabs followed by text which
        // is NOT a comment (error)
        // (?:[ \t]|\r?\n) : Either a space or tab, or a newline
        R"((%YAML[ \t]+([0-9])\.([0-9]))([ \t]+[^#\r\n]+)?(?:[ \t]|\r?\n))" >>
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

        R"(%TAG ([^\s]+) ([^\s]+))" >>
          [](auto& m) {
            m.push(TagDirective);
            m.add(TagPrefix, 1);
            m.add(TagHandle, 2);
            m.term();
            m.pop(TagDirective);
          },

        R"(%[[:alpha:]]+[^#\r\n]*)" >>
          [](auto& m) {
            std::cerr << "Unknown directive: " << m.match(1).view()
                      << std::endl;
            m.add(UnknownDirective, 1);
          },

        R"([ \t]*\.\.\.(?:\r?\n| )+)" >> [](auto&) { return; },

        R"([ \t]*(---)([ \t]+))" >>
          [](auto& m) {
            m.push(Document);
            m.add(DocumentStart, 1);
            m.add(Whitespace, 2);
            m.mode("document");
          },

        R"([ \t]*(---)(\r?\n))" >>
          [](auto& m) {
            m.push(Document);
            m.add(DocumentStart, 1);
            m.add(NewLine, 2);
            m.mode("document");
          },

        // If we reach this point, then there was no preamble and what follows
        // is the document itself
        "^" >>
          [](auto& m) {
            m.push(Document);
            m.mode("document");
          },
      });

    // Every stream is made up of zero or more documents.  Document
    // mode is the mode most would think of as "YAML", but can contain
    // JSON-like flow documents, which are handled in a separate mode.
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

        // text that looks like a directive in a document
        // %[[:alpha:]]+ : A percent sign followed by one or more alphabetic
        // characters
        // (?:[ \t]+[^\s]+) : One or more spaces or tabs followed by one or more
        // characters which are NOT whitespace
        // ([ \t]+#[^\r\n]*)? : Zero or one spaces or tabs followed by a comment
        // (optional)
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
            m.term();
            m.pop(Document);
            m.push(Document);
            m.add(DocumentStart, 1);
            m.add(NewLine, 2);
          },

        R"((---)([ \t]+))" >>
          [](auto& m) {
            m.term();
            m.pop(Document);
            m.push(Document);
            m.add(DocumentStart, 1);
            m.add(Whitespace, 2);
          },

        R"((\.\.\.)([ \t]*|[ \t]+#[^\r\n]*)?\r?\n)" >>
          [](auto& m) {
            m.add(DocumentEnd, 1);
            m.term();
            m.pop(Document);
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

        // Key with a colon
        // [a-zA-Z0-9\?:-] : An alphanumeric character, a colon, a question
        // mark, or a hyphen
        // (?:[^\s]|[^:\r\n] [^\s#])* : Either a character which is NOT
        // whitespace,
        //                              or a character which is NOT a colon or
        //                              newline, followed by a space, followed
        //                              by a character which is NOT whitespace
        //                              or a hash, zero or more times
        //  *(:) : zero or more spaces followed by a colon
        // (?:[ \t]+|\r?(\n)) : Either one or more spaces or tabs, or a newline
        R"(([[a-zA-Z0-9\?:-](?:[^\s]|[^:\r\n] [^\s#])*) *(:)(?:[ \t]+|\r?(\n)))" >>
          [anchors](auto& m) {
            m.add(Value, 1);
            m.add(Colon, 2);
            if (m.match(3).len > 0)
            {
              m.add(NewLine, 3);
            }
          },

        // Alias with a colon
        // \*([^\[\]\{\}\, \r\n]+) : An asterisk followed by one or more
        // characters which are NOT brackets, braces,
        //                           commas, whitespace, or newline
        // (:) : A colon
        // (?:[ \t]+|\r?(\n)) : Either one or more spaces or tabs, or a newline
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

        // Anchor
        // &([^\[\]\{\}\, \r\n]+) : An ampersand followed by one or more
        // characters which are NOT brackets, braces,
        //                          commas, whitespace, or newline
        // (?:[ \t]+|\r?(\n)) : Either one or more spaces or tabs, or a newline
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
        // ![0-9A-Za-z\-]+!|!!|! : An exclamation mark followed by one or more
        // alphanumeric characters or hyphens,
        //                         followed by an exclamation mark, or two
        //                         exclamation marks, or a single exclamation
        //                         mark
        // <(?:[\w#;\/\?:@&=+$,_.!~*'()[\]{}]|%\d+)+> : A less than sign
        // followed by
        //                                              one or more of: a word
        //                                              character and
        //                                              #;/?:@&=+$,_.!~*'()[\]{}],
        //                                                  or a percent sign
        //                                                  followed by one or
        //                                                  more digits,
        //                                              followed by a greater
        //                                              than sign
        R"((![0-9A-Za-z\-]+!|!!|!)(<(?:[\w#;\/\?:@&=+$,_.!~*'()[\]{}]|%\d+)+>)(?:[ \t]+|\r?(\n)))" >>
          [](auto& m) {
            m.push(Tag);
            m.add(TagPrefix, 1);
            m.add(VerbatimTag, 2);
            m.term();
            m.pop(Tag);

            if (m.match(3).len > 0)
            {
              m.add(NewLine, 3);
            }
          },

        // ns-shorthand-tag
        // ![0-9A-Za-z\-]*!|!!|! : An exclamation mark followed by zero or more
        // alphanumeric characters or hyphens,
        //                         followed by an exclamation mark, or two
        //                         exclamation marks, or a single exclamation
        //                         mark
        // (?:[\w#;\/\?:@&=+$_.~*'()]|%\d\d)+ : One or more of: a word character
        // and #;/?:@&=+$_.~*'(),
        //                                      or a percent sign followed by
        //                                      two digits
        // (?:[ \t]+|\r?(\n)) : Either one or more spaces or tabs, or a newline
        R"((![0-9A-Za-z\-]+!|!!|!)((?:[\w#;\/\?:@&=+$,_.!~*'()[\]{}]|%\d+)+)(?:[ \t]+|\r?(\n)))" >>
          [](auto& m) {
            m.push(Tag);
            m.add(TagPrefix, 1);
            m.add(ShorthandTag, 2);
            m.term();
            m.pop(Tag);

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
            m.term();
            m.pop(Tag);
            if (m.match(2).len > 0)
            {
              m.add(NewLine, 3);
            }
          },

        R"(\*[^\[\]\{\}\, \r\n]+)" >> [](auto& m) { m.add(Alias); },

        // Block scalar
        // [>|\|] : A greater than sign or a pipe
        // ([0-9]|[+-])? : Either a digit, or a plus or minus sign (optional)
        // ([0-9]|[+-])? : Either a digit, or a plus or minus sign (optional)
        // (#)? : A hash sign (error)
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

        // Single-quote. NB this captures absolutely everything at this stage,
        // and is cleaned up in the quotes() pass, because the semantics of
        // quoted strings are too complex to handle in this parser.
        // '(?:''|[^'])*' : A single quote followed by zero or more of:
        //                    two single quotes, or
        //                    a character which is NOT a single quote
        //                  followed by a single quote
        // (#)? : A hash sign (error)
        R"('(?:''|[^'])*'(#)?)" >>
          [](auto& m) {
            if (m.match(1).len > 0)
            {
              m.error(
                "Comment without whitespace after singlequoted scalar", 1);
            }

            m.add(SingleQuote);
          },

        // Double-quote. NB this captures absolutely everything at this stage,
        // and is cleaned up in the quotes() pass, because the semantics of
        // quoted strings are too complex to handle in this parser.
        // "(?:\\\\|\\"|[^"])*" : A double quote followed by zero or more of:
        //                          two backslashes, or
        //                          a backslash and a double quote, or
        //                          a character which is NOT a double quote
        //                        followed by a double quote
        // (#)? : A hash sign (error)
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

        // Value. Fairly expansive in YAML, it is either:
        // [^\s:\?-] : A character which is NOT whitespace, a colon, a question
        // mark, or a hyphen
        // :[^\s] : A colon followed by a character which is NOT whitespace
        // \?[^\s] : A question mark followed by a character which is NOT
        // whitespace
        // -[^\s] : A hyphen followed by a character which is NOT whitespace
        // And then zero or more of either:
        // [^\r\n \t:#] : Not a newline, space, tab, or hash
        // :[^\s] : A colon followed by a character which is NOT whitespace
        // #[^\s] : A hash followed by a character which is NOT whitespace
        // [ \t][^\r\n \t:#] : A space or tab followed by a character which is
        // NOT a newline, space, tab, or hash
        R"((?:[^\s:\?-]|:[^\s]|\?[^\s]|-[^\s])(?:[^\s:#]|:[^\s]|#[^\s]|[ \t][^\s:#])*)" >>
          [](auto& m) { m.add(Value); },
      });

    // Flow mode is very similar to document mode, but provides extra characters
    // to disambiguate the structure of mappings and sequences. These characters
    // were chosen such that flow mode acts a superset of JSON. However,
    // importantly, it is NOT JSON, and document-style constructs can still
    // appear within flow mode.
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
            m.term();
            m.pop(FlowMapping);
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
            m.term();
            m.pop(FlowSequence);
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

        // Anchor. See above for explanation.
        R"((&[^\[\]\{\}\, \r\n]+)(?:[ \t]+|\r?\n))" >>
          [anchors](auto& m) {
            m.add(Anchor, 1);
            anchors->insert(m.match(1).view());
          },

        // verbatim-tag. See above for explanation.
        R"((![0-9A-Za-z\-]*!|!!|!)(<(?:[\w#;\/\?:@&=+$,_.!~*'()]|%\d+)+>)(?:[ \t]+|\r?\n|(,)))" >>
          [](auto& m) {
            m.push(Tag);
            m.add(TagPrefix, 1);
            m.add(VerbatimTag, 2);
            m.term();
            m.pop(Tag);
            if (m.match(3).len > 0)
            {
              m.add(Comma, 3);
            }
          },

        // ns-shorthand-tag. See above for explanation.
        R"((![0-9A-Za-z\-]*!|!!|!)((?:[\w#;\/\?:@&=+$_.~*'()]|%\d\d)+)(?:[ \t]+|\r?\n|(,)))" >>
          [](auto& m) {
            m.push(Tag);
            m.add(TagPrefix, 1);
            m.add(ShorthandTag, 2);
            m.term();
            m.pop(Tag);
            if (m.match(3).len > 0)
            {
              m.add(Comma, 3);
            }
          },

        // non-specific-tag. See above for explanation.
        R"((!)(?:[ \t]+|\r?\n))" >>
          [](auto& m) {
            m.push(Tag);
            m.add(TagPrefix, 1);
            m.term();
            m.pop(Tag);
          },

        R"(\*[^\[\]\{\}\, \r\n]+)" >> [](auto& m) { m.add(Alias); },

        R"((?:\d+-)+\d*)" >> [](auto& m) { m.add(Value); },

        // Value.
        // This is the same RE as the one above for document mode, but in flow
        // mode there are additional "value exit" characters, namely ,{}[],
        // which is why you see them added everywhere.
        R"((?:[^\s:\?\-,{}[\]]|:[^\s,]|\?[^\s,{}[\]]|-[^\s,{}[\]])(?:[^\s:#,{}[\]]|:[^\s,{}[\]]|#[^\s,{}[\]]|[ \t][^\s:#,{}[\]])*)" >>
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
