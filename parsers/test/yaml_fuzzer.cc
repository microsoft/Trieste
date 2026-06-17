#include "fuzz_util.h"
#include "trieste/logging.h"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <trieste/fuzzer.h>
#include <trieste/yaml.h>

using namespace trieste;

int main(int argc, char** argv)
{
  CLI::App app;

  app.set_help_all_flag("--help-all", "Expand all help");

  std::string transform;
  app.add_option("transform", transform, "Transform to test")
    ->check(
      CLI::IsMember(
        {"reader", "writer", "event_writer", "to_json", "reader_to_json"}))
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

  std::string pass;
  app.add_option("-p,--start-pass", pass, "Test only this pass");

  // Sampling options
  std::filesystem::path sample_files;
  app.add_option(
    "--samples",
    sample_files,
    "Files to extract sample nodes from for fuzz testing");

  size_t sampling_level = 1;
  app.add_option(
    "--sampling-level",
    sampling_level,
    "Level of sampling to use for selecting sample nodes");

  size_t sampling_frequency = 50; // Default to 50% sampling frequency
  app.add_option(
    "--sampling-frequency",
    sampling_frequency,
    "Frequency of using sampled nodes over randomly generated nodes during "
    "testing (0-100)");

  auto bound_vars = true;
  app.add_option(
    "--gen_bound", bound_vars, "Generate bound variable names if possible");

  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  logging::Output() << "Testing x" << count << ", seed: " << seed << std::endl;

  Reader reader = yaml::reader();
  Reader reader_tojson = yaml::reader() >>= yaml::to_json();

  // Determine which reader to use for parsing sample files.
  Reader& sample_reader =
    (transform == "reader_to_json") ? reader_tojson : reader;

  Nodes sample_trees = load_sample_trees(sample_reader, sample_files, ".yaml");

  Fuzzer fuzzer;
  if (transform == "reader")
    fuzzer = Fuzzer(reader);
  else if (transform == "writer")
    fuzzer = Fuzzer(yaml::writer("fuzzer"), reader.parser().generators());
  else if (transform == "event_writer")
    fuzzer = Fuzzer(yaml::event_writer("fuzzer"), reader.parser().generators());
  else if (transform == "to_json")
    fuzzer = Fuzzer(yaml::to_json(), reader.parser().generators());
  else if (transform == "reader_to_json")
    fuzzer = Fuzzer(reader_tojson);

  const auto names = fuzzer.pass_names();
  size_t start_index = pass.empty() ? 1 : fuzzer.pass_index(pass);
  if (start_index == std::numeric_limits<size_t>::max())
  {
    std::string joined;
    for (const auto& n : names)
      joined += (joined.empty() ? "" : ", ") + n;
    logging::Error() << "Pass '" << pass << "' not in {" << joined << "}"
                     << std::endl;
    return 1;
  }

  size_t end_index = pass.empty() || sequence ? names.size() : start_index;

  return fuzzer.start_seed(seed)
    .start_index(start_index)
    .end_index(end_index)
    .seed_count(count)
    .failfast(failfast)
    .max_retries(count * 2)
    .test_sequence(sequence)
    .bound_vars(bound_vars)
    .sampling_level(sampling_level)
    .sampling_enabled(!sample_trees.empty())
    .sampling_frequency(sampling_frequency)
    .sample_trees(sample_trees)
    .test();
}
