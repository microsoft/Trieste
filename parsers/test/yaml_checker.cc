#include "trieste/logging.h"

#include <CLI/CLI.hpp>
#include <trieste/checker.h>
#include <trieste/yaml.h>

using namespace trieste;

int main(int argc, char** argv)
{
  CLI::App app;

  app.set_help_all_flag("--help-all", "Expand all help");

  bool check_against_wf = false;
  app.add_flag(
    "-w", check_against_wf, "Check pattern against well-formedness rules");

  std::vector<std::string> ignored_tokens;
  app.add_option(
    "-i,--ignore_token",
    ignored_tokens,
    "Ignore this token when checking patterns against well-formedness rules.");

  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  logging::Output() << "Checking patterns" << std::endl;

  Checker reader_checker = Checker(yaml::reader())
                             .check_against_wf(check_against_wf)
                             .ignored_tokens(ignored_tokens);
  Checker writer_checker = Checker(yaml::writer("checker"))
                             .check_against_wf(check_against_wf)
                             .ignored_tokens(ignored_tokens);
  Checker event_writer_checker = Checker(yaml::event_writer("checker"))
                                   .check_against_wf(check_against_wf)
                                   .ignored_tokens(ignored_tokens);
  Checker to_json_checker = Checker(yaml::to_json())
                              .check_against_wf(check_against_wf)
                              .ignored_tokens(ignored_tokens);

  return reader_checker.check() + writer_checker.check() +
    event_writer_checker.check() + to_json_checker.check();
}
