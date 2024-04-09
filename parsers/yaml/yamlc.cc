#include "trieste/json.h"
#include "trieste/trieste.h"
#include "trieste/yaml.h"

#include <CLI/CLI.hpp>

using namespace trieste;

int main(int argc, char** argv)
{
  CLI::App app;

  std::filesystem::path input_path;
  app.add_option("input", input_path, "Path to the input file ")->required();

  std::filesystem::path output_path;
  app.add_option("output", output_path, "Path to the output file");

  std::filesystem::path debug_path;
  app.add_option(
    "-a,--ast",
    debug_path,
    "Output the AST (debugging for the reader/rewriter/writer workflows)");

  bool wf_checks{false};
  app.add_flag("-w,--wf", wf_checks, "Enable well-formedness checks (slow)");

  bool prettyprint{false};
  app.add_flag("--prettyprint", prettyprint, "Pretty print the output (for JSON)");

  auto modes = {"event", "json", "yaml"};
  std::string mode;
  app.add_option("-m,--mode", mode, "Output mode.")
    ->transform(CLI::IsMember(modes));

  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  if (mode.empty())
  {
    if (output_path.extension() == ".event")
    {
      mode = "event";
    }
    else if (output_path.extension() == ".json")
    {
      mode = "json";
    }
    else if (output_path.extension() == ".yaml")
    {
      mode = "yaml";
    }
    else
    {
      std::cerr << "Output mode not specified and could not be inferred from "
                   "the output file extension."
                << std::endl;
      return 1;
    }
  }

  trieste::Reader reader = yaml::reader()
                             .file(input_path)
                             .debug_enabled(!debug_path.empty())
                             .debug_path(debug_path)
                             .wf_check_enabled(wf_checks);
  ProcessResult result;
  if (mode == "event")
  {
    result = reader >> yaml::event_writer(output_path.filename().string())
                         .dir(output_path.parent_path())
                         .debug_enabled(!debug_path.empty())
                         .debug_path(debug_path)
                         .wf_check_enabled(wf_checks);
    ;
  }
  else if (mode == "json")
  {
    result = reader >> yaml::to_json()
                         .debug_enabled(!debug_path.empty())
                         .debug_path(debug_path)
                         .wf_check_enabled(wf_checks) >>
      json::writer(output_path.filename().string(), prettyprint)
        .dir(output_path.parent_path())
        .debug_enabled(!debug_path.empty())
        .debug_path(debug_path)
        .wf_check_enabled(wf_checks);
    ;
  }
  else
  {
    result = reader >> yaml::writer(output_path.filename().string())
                         .dir(output_path.parent_path())
                         .debug_enabled(!debug_path.empty())
                         .debug_path(debug_path)
                         .wf_check_enabled(wf_checks);
    ;
  }

  if (!result.ok)
  {
    std::cerr << result.error_message() << std::endl;
    return 1;
  }

  return 0;
}
