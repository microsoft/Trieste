#include "infix.h"

#include <CLI/CLI.hpp>

using namespace trieste;

int main(int argc, char** argv)
{
  CLI::App app;

  std::filesystem::path input_path;
  app.add_option("input", input_path, "Path to the input file ")->required();

  std::filesystem::path output_path;
  app.add_option("output", output_path, "Path to the output file ");

  auto modes = {"calculate", "infix", "postfix"};
  std::string mode = "calculate";
  app.add_option("-m,--mode", mode, "Output mode.")
    ->transform(CLI::IsMember(modes));

  infix::Config config;
  config.install_cli(&app);

  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  auto reader = infix::reader(config).file(input_path);
  Destination dest =
    output_path.empty() ? DestinationDef::console() : DestinationDef::dir(".");
  if (output_path.empty())
  {
    output_path = mode;
  }

  try
  {
    ProcessResult result;
    if (mode == "calculate")
    {
      result = reader >> infix::calculate() >>
        infix::calculate_output_writer(output_path).destination(dest);
    }
    if (mode == "infix")
    {
      result = reader >> infix::writer(output_path).destination(dest);
    }
    else if (mode == "postfix")
    {
      result = reader >> infix::postfix_writer(output_path).destination(dest);
    }

    if (!result.ok)
    {
      logging::Error err;
      result.print_errors(err);
      return 1;
    }
  }
  catch (const std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return 1;
  }

  return 0;
}
