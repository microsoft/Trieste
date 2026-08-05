// Produce one declared artifact and one unlisted output for update filtering.

#include <filesystem>
#include <fstream>

int main(int argc, char** argv)
{
  if (argc != 2)
    return 1;

  const std::filesystem::path artifact(argv[1]);
  std::ofstream(artifact) << "payload\n";
  std::ofstream(artifact.parent_path() / "extra.txt") << "not a golden\n";
  return 0;
}
