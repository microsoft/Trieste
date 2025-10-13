// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "parse.h"
#include "passes.h"

#include <optional>
#include <variant>

namespace trieste
{
  class Reader
  {
  private:
    constexpr static auto parse_only = "parse";

    using InputSpec =
      std::optional<std::variant<std::filesystem::path, Source>>;

    std::string language_name_;
    std::vector<Pass> passes_;
    Parse parser_;
    InputSpec input_{};
    bool debug_enabled_;
    bool wf_check_enabled_;
    std::filesystem::path debug_path_;
    std::string start_pass_;
    std::string end_pass_;
    std::size_t offset_;

  public:
    Reader(
      const std::string& language_name,
      const std::vector<Pass>& passes,
      const Parse& parser)
    : language_name_(language_name),
      passes_(passes),
      parser_(parser),
      debug_enabled_(false),
      wf_check_enabled_(false),
      debug_path_("."),
      start_pass_(""),
      end_pass_(""),
      offset_(0)
    {}

    ProcessResult read()
    {
      if (!input_)
      {
        return {false, parse_only, nullptr, {(Error ^ "No source provided")}};
      }

      auto& input = *input_;
      PassRange pass_range(
        passes_.begin(), passes_.end(), parser_.wf(), parse_only);

      if (!end_pass_.empty())
      {
        if (end_pass_ == parse_only)
        {
          pass_range.disable();
        }
        else if (!pass_range.move_end(end_pass_))
        {
          return {
            false,
            parse_only,
            nullptr,
            {Error ^ ("Unknown pass: " + end_pass_)}};
        }
      }

      Node ast;
      auto parse_start = std::chrono::high_resolution_clock::now();
      if (!start_pass_.empty())
      {
        if (!pass_range.move_start(start_pass_))
        {
          return {
            false,
            parse_only,
            nullptr,
            {Error ^ ("Unknown pass: " + start_pass_)}};
        }

        Source source;
        if (std::holds_alternative<std::filesystem::path>(input))
        {
          auto& path = std::get<std::filesystem::path>(input);
          if (std::filesystem::is_directory(path))
            return {
              false,
              parse_only,
              nullptr,
              {Error ^ "Cannot use directory with intermediate pass."}};
          source = SourceDef::load(path);
        }
        else
        {
          source = std::get<Source>(input);
        }

        // Pass range is currently pointing at pass, but the output is the
        // dump of that, so advance it one, so we start processing on the
        // next pass.
        ++pass_range;

        ast = build_ast(source, offset_);
      }
      else
      {
        std::visit([&](auto x) { ast = parser_.parse(x); }, input);
      }
      auto parse_end = std::chrono::high_resolution_clock::now();

      logging::Info summary;
      std::filesystem::path debug_path;
      if (debug_enabled_)
      {
        debug_path = debug_path_;
      }

      summary << "---------" << std::endl;
      summary << "Parse time (us): "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                   parse_end - parse_start).count()
              << std::endl;

      auto result =
        Process(pass_range)
          .set_check_well_formed(wf_check_enabled_)
          .set_default_pass_complete(summary, language_name_, debug_path)
          .run(ast);
      summary << "---------" << std::endl;
      return result;
    }

    template<typename StringLike>
    size_t pass_index(const StringLike& name_) const
    {
      if (name_ == parse_only)
        return 0;

      for (size_t i = 0; i < passes_.size(); i++)
      {
        if (passes_[i]->name() == name_)
          return i + 1;
      }

      return std::numeric_limits<size_t>::max();
    }

    std::vector<std::string> pass_names() const
    {
      std::vector<std::string> names;
      names.push_back(parse_only);
      std::transform(
        passes_.begin(),
        passes_.end(),
        std::back_inserter(names),
        [](const auto& p) { return p->name(); });
      return names;
    }

    Reader& executable(const std::filesystem::path& path)
    {
      parser_.executable(path);
      return *this;
    }

    Reader& language_name(const std::string& name)
    {
      language_name_ = name;
      return *this;
    }

    const std::string& language_name() const
    {
      return language_name_;
    }

    const std::vector<Pass>& passes() const
    {
      return passes_;
    }

    const Parse& parser() const
    {
      return parser_;
    }

    Reader& debug_enabled(bool value)
    {
      debug_enabled_ = value;
      return *this;
    }

    bool debug_enabled() const
    {
      return debug_enabled_;
    }

    Reader& wf_check_enabled(bool value)
    {
      wf_check_enabled_ = value;
      return *this;
    }

    bool wf_check_enabled() const
    {
      return wf_check_enabled_;
    }

    Reader& debug_path(const std::filesystem::path& path)
    {
      debug_path_ = path;
      return *this;
    }

    const std::filesystem::path& debug_path() const
    {
      return debug_path_;
    }

    template<typename StringLike>
    Reader& start_pass(const StringLike& pass)
    {
      start_pass_ = pass;
      return *this;
    }

    const std::string& start_pass() const
    {
      return start_pass_;
    }

    template<typename StringLike>
    Reader& end_pass(const StringLike& pass)
    {
      end_pass_ = pass;
      return *this;
    }

    const std::string& end_pass() const
    {
      return end_pass_;
    }

    Reader& offset(std::size_t pos)
    {
      offset_ = pos;
      return *this;
    }

    std::size_t offset() const
    {
      return offset_;
    }

    Reader& source(const Source& s)
    {
      input_ = s;
      return *this;
    }

    Reader& file(const std::filesystem::path& path)
    {
      input_ = path;
      return *this;
    }

    Reader& synthetic(const std::string& contents, const std::string& origin="")
    {
      input_ = SourceDef::synthetic(contents, origin);
      return *this;
    }

    Reader& postparse(Parse::PostF func)
    {
      parser_.postparse(func);
      return *this;
    }

    const wf::Wellformed& output_wf() const
    {
      return passes_.back()->wf();
    }
  };
}
