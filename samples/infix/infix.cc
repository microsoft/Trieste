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

  auto modes = {"result", "infix", "postfix"};
  std::string mode = "result";
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

  auto reader = infix::reader().file(input_path);
  Destination dest =
    output_path.empty() ? DestinationDef::console() : DestinationDef::dir(".");
  if (output_path.empty())
  {
    output_path = mode;
  }

  ProcessResult result;
  if (mode == "result")
  {
    result = reader >> infix::result_writer(output_path).destination(dest);
  }
  else if (mode == "infix")
  {
    result = reader >> infix::writer(output_path).destination(dest);
  }
  else if (mode == "postfix")
  {
    result = reader >> infix::postfix_writer(output_path).destination(dest);
  }

  if (!result.ok)
  {
    std::cerr << result.error_message() << std::endl;
    return 1;
  }

  return 0;
}
