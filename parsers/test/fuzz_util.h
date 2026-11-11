#pragma once

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <trieste/fuzzer.h>

namespace trieste
{
  enum class ReadMode
  {
    ParseOnly,
    Full,
  };

  inline Node
  load_sample_tree(Reader reader, const std::filesystem::path& p, ReadMode mode)
  {
    if (mode == ReadMode::ParseOnly)
    {
      Node ast = reader.parser().parse(p);
      return is_valid_sample(reader.parser(), ast) ? ast : nullptr;
    }

    // Enabling the WF check ensures that the sample tree is well-formed
    auto result = reader.wf_check_enabled(true).file(p).read();
    return (result.ok && result.ast) ? result.ast : nullptr;
  }

  // Load sample trees for fuzz testing from `path`, which may be a single file
  // or a directory walked recursively for files with extension `ext`. Samples
  // that fail to parse or are not well-formed are skipped with a warning (both
  // for single files and directory entries). An empty path (no samples
  // requested) yields no samples; a non-existent path yields no samples and a
  // warning.
  inline Nodes load_sample_trees(
    const Reader& reader,
    const std::filesystem::path& path,
    std::string_view ext,
    ReadMode mode)
  {
    Nodes sample_trees;
    if (path.empty())
      return sample_trees;

    if (!std::filesystem::exists(path))
    {
      logging::Warn() << "Sample path " << path << " does not exist"
                      << std::endl;
      return sample_trees;
    }

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
      if (Node sample_program = load_sample_tree(reader, p, mode))
        sample_trees.push_back(sample_program);
      else
        logging::Warn() << "Skipping invalid sample " << p << std::endl;
    }

    logging::Info() << "Collected " << sample_trees.size()
                    << " sample trees for fuzz testing" << std::endl;
    return sample_trees;
  }
}
