#include "json.h"
#include "trieste/source.h"

namespace trieste::json
{
  JSONReader::JSONReader(const std::filesystem::path& path)
  : JSONReader(SourceDef::load(path))
  {}

  JSONReader::JSONReader(const std::string& yaml)
  : JSONReader(SourceDef::synthetic(yaml))
  {}

  JSONReader::JSONReader(const Source& source)
  : m_source(source),
    m_debug_enabled(false),
    m_debug_path("."),
    m_well_formed_checks_enabled(false)
  {}

  JSONReader& JSONReader::debug_enabled(bool value)
  {
    m_debug_enabled = value;
    return *this;
  }

  bool JSONReader::debug_enabled() const
  {
    return m_debug_enabled;
  }

  JSONReader& JSONReader::well_formed_checks_enabled(bool value)
  {
    m_well_formed_checks_enabled = value;
    return *this;
  }

  bool JSONReader::well_formed_checks_enabled() const
  {
    return m_well_formed_checks_enabled;
  }

  JSONReader& JSONReader::debug_path(const std::filesystem::path& path)
  {
    m_debug_path = path;
    return *this;
  }

  const std::filesystem::path& JSONReader::debug_path() const
  {
    return m_debug_path;
  }

  void JSONReader::read()
  {
    auto ast = NodeDef::create(Top);
    Parse parse = json::parser();
    ast << parse.sub_parse("json", File, m_source);

    auto passes = json::passes();
    PassRange pass_range(passes, parse.wf(), "parse");
    bool ok;
    Nodes error_nodes;
    std::string failed_pass;
    {
      logging::Info summary;
      summary << "---------" << std::endl;
      std::filesystem::path debug_path;
      if (m_debug_enabled)
        debug_path = m_debug_path;
      auto p = default_process(
        summary, m_well_formed_checks_enabled, "json", debug_path);

      p.set_error_pass(
        [&error_nodes, &failed_pass](Nodes& errors, std::string pass_name) {
          error_nodes = errors;
          failed_pass = pass_name;
        });

      ok = p.build(ast, pass_range);
      summary << "---------" << std::endl;
    }

    if (ok)
    {
      m_element = ast;
      return;
    }

    logging::Trace() << "Read failed: " << failed_pass;
    if (error_nodes.empty())
    {
      logging::Trace() << "No error nodes so assuming wf error";
      error_nodes.push_back(err(ast->clone(), "Failed at pass " + failed_pass));
    }

    Node error_result = NodeDef::create(ErrorSeq);
    for (auto& error : error_nodes)
    {
      error_result->push_back(error);
    }

    m_element = error_result;
  }

  bool JSONReader::has_errors() const
  {
    return m_element->type() == ErrorSeq;
  }

  std::string JSONReader::error_message() const
  {
    std::ostringstream error;
    error << m_element;
    return error.str();
  }

  const Node& JSONReader::element() const
  {
    return m_element;
  }
}
