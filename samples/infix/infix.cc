#include "infix.h"
#include <CLI/CLI.hpp>

using namespace trieste;

int main(int argc, char** argv)
{
CLI::App app;

  std::filesystem::path input_path;
  app.add_option("input", input_path, "Path to the input file ")->required();

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
  ProcessResult result;
  if(mode == "result"){
    result = reader >> infix::result_writer().debug_enabled(true).debug_path("test");
  }

  if(!result.ok){
    std::cerr << result.error_message() << std::endl;
    return 1;
  }

  return 0;
}