#include "trieste/source.h"
#include "yaml.h"

namespace trieste::yaml
{
  YAMLReader::YAMLReader(const std::filesystem::path& path)
  : YAMLReader(SourceDef::load(path))
  {}

  YAMLReader::YAMLReader(const std::string& yaml)
  : YAMLReader(SourceDef::synthetic(yaml))
  {}

  YAMLReader::YAMLReader(const Source& source)
  : m_source(source),
    m_debug_enabled(false),
    m_debug_path("."),
    m_well_formed_checks_enabled(false)
  {}

  YAMLReader& YAMLReader::debug_enabled(bool value)
  {
    m_debug_enabled = value;
    return *this;
  }

  bool YAMLReader::debug_enabled() const
  {
    return m_debug_enabled;
  }

  YAMLReader& YAMLReader::well_formed_checks_enabled(bool value)
  {
    m_well_formed_checks_enabled = value;
    return *this;
  }

  bool YAMLReader::well_formed_checks_enabled() const
  {
    return m_well_formed_checks_enabled;
  }

  YAMLReader& YAMLReader::debug_path(const std::filesystem::path& path)
  {
    m_debug_path = path;
    return *this;
  }

  const std::filesystem::path& YAMLReader::debug_path() const
  {
    return m_debug_path;
  }

  void YAMLReader::read()
  {
    auto ast = NodeDef::create(Top);
    Parse parse = yaml::parser();
    ast << parse.sub_parse("yaml", File, m_source);
    auto passes = yaml::passes();
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
        summary, m_well_formed_checks_enabled, "yaml", debug_path);

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
      m_stream = ast;
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

    m_stream = error_result;
  }

  const Node& YAMLReader::stream() const
  {
    return m_stream;
  }

  bool YAMLReader::has_errors() const
  {
    return m_stream->type() == ErrorSeq;
  }

  std::string YAMLReader::error_message() const
  {
    std::ostringstream error;
    error << m_stream;
    return error.str();
  }
}
