// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "passes.h"
#include "trieste/wf.h"

#include <filesystem>

namespace trieste
{
  class DestinationDef;
  using Destination = std::shared_ptr<DestinationDef>;

  class DestinationDef
  {
  private:
    enum class Mode
    {
      // Files will be written to the file system
      FileSystem,
      // Files will be output directly to console
      Console,
      // Files are stored in memory and accessible via dst.files()
      Synthetic
    };

    Mode mode_;
    std::ofstream fstream_;
    std::ostringstream sstream_;
    std::map<std::string, std::string> files_;
    std::filesystem::path path_;
    bool is_open_;

  public:
    ~DestinationDef()
    {
      close();
    }

    std::ostream& stream()
    {
      switch (mode_)
      {
        case Mode::FileSystem:
          return fstream_;

        case Mode::Console:
          return std::cout;

        case Mode::Synthetic:
          return sstream_;

        default:
          throw std::runtime_error("Invalid destination mode");
      }
    }

    void push_directory(const std::filesystem::path& path)
    {
      path_ /= path;
    }

    void pop_directory()
    {
      path_ = path_.parent_path();
    }

    bool open(const std::filesystem::path& path)
    {
      close();
      path_ = path_ / path;
      switch (mode_)
      {
        case Mode::FileSystem:
          if (!path_.parent_path().empty())
          {
            std::filesystem::create_directories(path_.parent_path());
          }
          fstream_.open(path_);
          return is_open_ = fstream_.is_open();

        case Mode::Console:
          std::cout << "OPEN " << path_;
          return is_open_ = true;

        case Mode::Synthetic:
          return is_open_ = true;

        default:
          throw std::runtime_error("Invalid destination mode");
      }
    }

    void close()
    {
      if (!is_open_)
      {
        return;
      }

      std::string contents;
      switch (mode_)
      {
        case Mode::FileSystem:
          if (fstream_.is_open())
          {
            fstream_.close();
          }
          break;

        case Mode::Console:
          std::cout << std::endl << "CLOSE " << path_ << std::endl;
          break;

        case Mode::Synthetic:
          contents = sstream_.str();
          if (!contents.empty())
          {
            files_[path_.string()] = contents;
            sstream_ = std::ostringstream();
          }
          break;
      }

      path_ = path_.parent_path();
      is_open_ = false;
    }

    const std::map<std::string, std::string>& files() const
    {
      return files_;
    }

    const std::string& file(const std::filesystem::path& path) const
    {
      return files_.at(path.string());
    }

    static Destination dir(const std::filesystem::path& path)
    {
      auto d = std::make_shared<DestinationDef>();
      d->mode_ = Mode::FileSystem;
      d->path_ = path;
      return d;
    }

    static Destination console()
    {
      auto d = std::make_shared<DestinationDef>();
      d->mode_ = Mode::Console;
      d->path_ = ".";
      return d;
    }

    static Destination synthetic()
    {
      auto d = std::make_shared<DestinationDef>();
      d->mode_ = Mode::Synthetic;
      d->path_ = ".";
      return d;
    }
  };

  using WriteFile = std::function<bool(std::ostream&, Node)>;
  using namespace wf::ops;

  inline const auto Path = TokenDef("path", flag::print);
  inline const auto FileSeq = TokenDef("fileseq");
  inline const auto Contents = TokenDef("contents");

  // clang-format off
  inline const auto wf_writer =
    (Top <<= Directory | File)
    | (Directory <<= Path * FileSeq)
    | (FileSeq <<= (Directory | File)++)
    | (File <<= Path * Contents)
    ;
  // clang-format on

  class Writer
  {
  private:
    std::string language_name_;
    std::vector<Pass> passes_;
    const wf::Wellformed* wf_;
    WriteFile write_file_;
    Destination destination_;
    bool debug_enabled_;
    bool wf_check_enabled_;
    std::filesystem::path debug_path_;

  public:
    Writer(
      const std::string& language_name,
      const std::vector<Pass>& passes,
      const wf::Wellformed& input_pass,
      WriteFile write_file)
    : language_name_(language_name),
      passes_(passes),
      wf_(&input_pass),
      write_file_(write_file),
      debug_enabled_(false),
      wf_check_enabled_(true),
      debug_path_(".")
    {
      console();
    }

    ProcessResult write(Node ast)
    {
      PassRange pass_range(
        passes_.begin(), passes_.end(), *wf_, language_name_);

      logging::Info summary;
      std::filesystem::path debug_path;
      if (debug_enabled_)
      {
        debug_path = debug_path_;
      }

      summary << "---------" << std::endl;
      auto result =
        Process(pass_range)
          .set_check_well_formed(wf_check_enabled_)
          .set_default_pass_complete(summary, language_name_, debug_path)
          .run(ast);
      summary << "---------" << std::endl;

      if (!result.ok)
      {
        return result;
      }

      Destination dest = destination_;
      wf::push_back(*wf_);
      wf::push_back(wf_writer);

      Nodes error_nodes;
      std::vector<Node> stack;
      stack.push_back(ast);
      while (!stack.empty())
      {
        Node current = stack.back();
        stack.pop_back();
        if (current == Directory)
        {
          try
          {
            dest->push_directory((current / Path)->location().view());
          }
          catch (std::exception& e)
          {
            error_nodes.push_back(
              Error << (ErrorMsg ^ e.what()) << (ErrorAst << current->clone()));
          }

          auto files = current / FileSeq;
          stack.push_back(NoChange);
          for (auto& file : *files)
          {
            stack.push_back(file);
          }
        }
        else if (current == NoChange)
        {
          dest->pop_directory();
        }
        else if (current == File)
        {
          try
          {
            dest->open((current / Path)->location().view());
            write_file_(dest->stream(), current / Contents);
            dest->close();
          }
          catch (std::exception& e)
          {
            error_nodes.push_back(
              Error << (ErrorMsg ^ e.what()) << (ErrorAst << current->clone()));
          }
        }
        else if (current == Top)
        {
          stack.insert(stack.end(), current->begin(), current->end());
        }
      }

      wf::pop_front();
      wf::pop_front();

      if (!error_nodes.empty())
      {
        result.ok = false;
        result.errors = error_nodes;
      }

      return result;
    }

    Writer& debug_enabled(bool value)
    {
      debug_enabled_ = value;
      return *this;
    }

    bool debug_enabled() const
    {
      return debug_enabled_;
    }

    Writer& wf_check_enabled(bool value)
    {
      wf_check_enabled_ = value;
      return *this;
    }

    bool wf_check_enabled() const
    {
      return wf_check_enabled_;
    }

    Writer& debug_path(const std::filesystem::path& path)
    {
      debug_path_ = path;
      return *this;
    }

    const std::filesystem::path& debug_path() const
    {
      return debug_path_;
    }

    Writer& destination(const Destination& destination)
    {
      destination_ = destination;
      return *this;
    }

    Writer& dir(const std::filesystem::path& path)
    {
      destination_ = DestinationDef::dir(path);
      return *this;
    }

    Writer& console()
    {
      destination_ = DestinationDef::console();
      return *this;
    }

    Writer& synthetic()
    {
      destination_ = DestinationDef::synthetic();
      return *this;
    }

    Destination destination() const
    {
      return destination_;
    }
  };
}
