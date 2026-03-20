#include "trieste/logging.h"

#include <CLI/CLI.hpp>
#include <trieste/fuzzer.h>
#include <trieste/yaml.h>

using namespace trieste;

int main(int argc, char** argv)
{
  CLI::App app;

  app.set_help_all_flag("--help-all", "Expand all help");

  std::string transform;
  app.add_option("transform", transform, "Transform to test")
    ->check(CLI::IsMember({"reader", "writer", "event_writer", "to_json", "all"}))
    ->required(true);

  uint32_t seed = std::random_device()();
  app.add_option("-s,--seed", seed, "Random seed");

  uint32_t count = 100;
  app.add_option("-c,--count", count, "Number of seed to test");

  bool sequence = false;
  app.add_flag("--sequence", sequence, "Run passes in sequence");

  bool failfast = false;
  app.add_flag("-f,--failfast", failfast, "Stop on first failure");

  std::string log_level;
  app
    .add_option(
      "-l,--log_level",
      log_level,
      "Set Log Level to one of "
      "Trace, Debug, Info, "
      "Warning, Output, Error, "
      "None")
    ->check(logging::set_log_level_from_string);

  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  logging::Output() << "Testing x" << count << ", seed: " << seed << std::endl;

  Fuzzer fuzzer;
  Reader reader = yaml::reader();
  if (transform == "reader")
  {
    fuzzer = Fuzzer(reader);
  }
  else if (transform == "writer")
  {
    fuzzer = Fuzzer(yaml::writer("fuzzer"), reader.parser().generators());
  }
  else if (transform == "event_writer")
  {
    fuzzer = Fuzzer(yaml::event_writer("fuzzer"), reader.parser().generators());
  }
  else if (transform == "to_json")
  {
    fuzzer = Fuzzer(yaml::to_json(), reader.parser().generators());
  }
  else if (transform == "all")
  {
    std::vector<Pass> passes = reader.passes();
    std::vector<Pass> to_json_passes = yaml::to_json().passes();
    passes.insert(passes.end(), to_json_passes.begin(), to_json_passes.end());
    fuzzer = Fuzzer(passes, reader.parser().wf(), reader.parser().generators());
  }

  return fuzzer.start_seed(seed)
    .seed_count(count)
    .failfast(failfast)
    .max_retries(count * 2)
    .test_sequence(sequence)
    .test();
}
