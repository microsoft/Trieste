#pragma once

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <trieste/fuzzer.h>

namespace trieste
{
  // Load sample trees for fuzz testing from `path`, which may be a single file
  // or a directory walked recursively for files with extension `ext`. Samples
  // that fail to parse or are not well-formed are skipped with a warning (both
  // for single files and directory entries); an empty/non-existent path yields
  // no samples.
  inline Nodes load_sample_trees(
    Reader& reader, const std::filesystem::path& path, std::string_view ext)
  {
    Nodes sample_trees;
    if (path.empty() || !std::filesystem::exists(path))
      return sample_trees;

    // Collect matching paths first, then sort them so samples are loaded in a
    // deterministic order, making fuzzing reproducible across filesystems.
    std::vector<std::filesystem::path> files;
    if (std::filesystem::is_directory(path))
    {
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator(path))
      {
        if (entry.is_regular_file() && entry.path().extension() == ext)
          files.push_back(entry.path());
      }
    }
    else
    {
      files.push_back(path);
    }
    std::sort(files.begin(), files.end());

    for (const auto& p : files)
    {
      Node sample_program = reader.parser().parse(p);
      if (is_valid_sample(reader.parser(), sample_program))
        sample_trees.push_back(sample_program);
      else
        logging::Warn() << "Skipping invalid sample " << p << std::endl;
    }

    logging::Info() << "Collected " << sample_trees.size()
                    << " sample trees for fuzz testing" << std::endl;
    return sample_trees;
  }
}
