#pragma once

#include "pass.h"
#include "wf.h"
#include "wf_meta.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace trieste
{
  template<typename PassIterator>
  class PassRange
  {
    PassIterator start;
    PassIterator end;
    // Well-formed condition for entry into this Range.
    const wf::Wellformed* wf;
    std::string entry_name;

  public:
    PassRange(
      PassIterator start_,
      PassIterator end_,
      const wf::Wellformed& wf_,
      std::string entry_name_)
    : start(start_), end(end_), wf(&wf_), entry_name(entry_name_)
    {}

    template<typename Range>
    PassRange(Range& range, const wf::Wellformed& wf_, std::string entry_name_)
    : start(range.begin()), end(range.end()), wf(&wf_), entry_name(entry_name_)
    {}

    template<typename StringLike>
    bool move_start(StringLike name)
    {
      auto it = std::find_if(
        start, end, [&](auto& pass) { return pass->name() == name; });
      if (it == end)
        return false;

      wf = &((*it)->wf());
      entry_name = (*it)->name();
      start = it;
      return true;
    }

    template<typename StringLike>
    bool move_end(StringLike name)
    {
      auto it = std::find_if(
        start, end, [&](auto& pass) { return pass->name() == name; });
      if (it == end)
        return false;
      end = ++it;
      return true;
    }

    Pass& operator()()
    {
      return *start;
    }

    void operator++()
    {
      wf = &((*start)->wf());
      entry_name = (*start)->name();
      start++;
    }

    bool has_next()
    {
      return start != end;
    }

    const wf::Wellformed& input_wf() const
    {
      return *wf;
    }

    Pass& last_pass()
    {
      for (auto it = start; it != end; ++it)
      {
        if (it + 1 == end)
          return *it;
      }
      throw std::runtime_error("No passes in range");
    }

    std::string entry_pass_name()
    {
      return entry_name;
    }
  };

  // Deduction guide require for constructor of PassRange
  template<typename PassIterator>
  PassRange(PassIterator, PassIterator, const wf::Wellformed&, std::string)
    -> PassRange<PassIterator>;

  // Deduction guide require for constructor of PassRange
  template<typename Range>
  PassRange(Range, const wf::Wellformed&, std::string)
    -> PassRange<typename Range::iterator>;

  struct PassStatistics
  {
    size_t count;
    size_t changes;
    std::chrono::microseconds duration;
  };

  struct ProcessResult
  {
    bool ok;
    std::string last_pass;
    Node ast;
    Nodes errors;

    void print_errors(logging::Log& err) const
    {
      logging::Sep sep{"----------------"};
      err << "Errors:";

      size_t count = 0;

      for (auto& error : errors)
      {
        err << sep << std::endl;
        for (auto& child : *error)
        {
          if (child->type() == ErrorMsg)
            err << child->location().view() << std::endl;
          else
          {
            err << "-- " << child->location().origin_linecol() << std::endl
                << child->location().str() << std::endl;
          }
        }
        if (count++ > 20)
        {
          err << "Too many errors, stopping here" << std::endl;
          break;
        }
      }
      err << "Pass " << last_pass << " failed with " << errors.size()
          << (count > 1 ? " errors!" : " error!") << std::endl;
    }
  };

  /**
   * @brief Process is used to run a collection of rewrite passes on an Ast.
   * It provides a collection of hooks to produce output.
   */
  template<typename PassIterator>
  class Process
  {
    PassRange<PassIterator> pass_range;

    bool check_well_formed{true};

    std::function<bool(
      Node&, std::string, const wf::Wellformed&, size_t index, PassStatistics&)>
      pass_complete;

    std::function<Nodes(Nodes&, std::string)> error_pass;

  public:
    Process(const PassRange<PassIterator>& passes) : pass_range(passes) {}

    /**
     * @brief After each pass the supplied function is called with the current
     * AST and details of the pass that has just completed.
     */
    Process& set_pass_complete(
      std::function<bool(
        Node&, std::string, const wf::Wellformed&, size_t, PassStatistics&)> f)
    {
      pass_complete = f;
      return *this;
    }

    Process& set_pass_complete(
      std::function<bool(Node&, std::string, size_t, PassStatistics&)> f)
    {
      pass_complete = [f](
                        Node& ast,
                        std::string pass_name,
                        const wf::Wellformed&,
                        size_t index,
                        PassStatistics& stats) {
        return f(ast, pass_name, index, stats);
      };
      return *this;
    }

    Process& set_default_pass_complete(
      logging::Log& summary,
      const std::string& language_name = "",
      std::filesystem::path output_directory = {})
    {
      pass_complete = [output_directory, language_name, &summary](
                        Node& ast,
                        std::string pass_name,
                        const wf::Wellformed& wf,
                        size_t index,
                        PassStatistics& stats) {
        auto [count, changes, duration] = stats;
        std::string delim{"\t"};
        if (index == 0)
        {
          summary << "Pass" << delim << "Iterations" << delim << "Changes"
                  << delim << "Time (us)" << std::endl;
        }

        summary << pass_name << delim << count << delim << changes << delim
                << static_cast<size_t>(duration.count()) << std::endl;
        if (output_directory.empty())
          return true;

        // Check if output_directory exists, and if not create it.
        if (!std::filesystem::exists(output_directory))
        {
          if (!std::filesystem::create_directories(output_directory))
          {
            logging::Error()
              << "Could not create output directory " << output_directory;
            return false;
          }
        }

        auto open_dbg_file =
          [&](const std::string& ext) -> std::pair<std::ofstream, bool> {
          std::filesystem::path output;
          if (index < 10)
          {
            output = output_directory /
              ("0" + std::to_string(index) + "_" + pass_name + ext);
          }
          else
          {
            output = output_directory /
              (std::to_string(index) + "_" + pass_name + ext);
          }

          std::ofstream f(output, std::ios::binary | std::ios::out);
          bool ok = true;
          if (!f)
          {
            logging::Error() << "Could not open " << output << " for writing.";
            ok = false;
          }
          return {std::move(f), ok};
        };

        auto [f, ok] = open_dbg_file(".trieste");
        if (!ok)
          return false;
        // Write the AST to the output file.
        f << language_name << std::endl << pass_name << std::endl << ast;

        std::tie(f, ok) = open_dbg_file(".trieste_wf");
        if (!ok)
          return false;
        // Write the well-formedness definition to a neighboring output file.
        f << language_name << std::endl << pass_name << std::endl;
        wf::meta::write_wf_node(f, wf::meta::wf_to_node(wf));

        return true;
      };

      return *this;
    }

    /**
     * @brief Specified is well-formedness should be checked between passes.
     */
    Process& set_check_well_formed(bool b)
    {
      check_well_formed = b;
      return *this;
    }

    bool validate(Node ast, Nodes& errors)
    {
      auto wf = pass_range.input_wf();
      auto ok = bool(ast);

      ok = ok && wf.build_st(ast);

      if (ast)
        ast->get_errors(errors);
      ok = ok && errors.empty();

      ok = ok && (!check_well_formed || wf.check(ast));

      return ok;
    }

    /**
     * @brief Run the supplied passes on the Ast.
     *
     * Returns the rewritten Ast, or an empty Node if the process failed.
     */
    ProcessResult run(Node& ast)
    {
      size_t index = 1;

      WFContext context(pass_range.input_wf());

      Nodes errors;

      // Check ast is well-formed before starting.
      auto ok = validate(ast, errors);

      PassStatistics stats;
      std::string last_pass = pass_range.entry_pass_name();
      ok =
        pass_complete(
          ast, pass_range.entry_pass_name(), pass_range.input_wf(), 0, stats) &&
        ok;

      for (; ok && pass_range.has_next(); index++)
      {
        logging::Debug() << "Starting pass: \"" << pass_range()->name() << "\"";

        auto now = std::chrono::high_resolution_clock::now();
        auto& pass = pass_range();
        context.push_back(pass->wf());

        auto [new_ast, count, changes] = pass->run(ast);
        ast = new_ast;
        context.pop_front();

        ++pass_range;

        ok = validate(ast, errors);

        auto then = std::chrono::high_resolution_clock::now();
        stats = {
          count,
          changes,
          std::chrono::duration_cast<std::chrono::microseconds>(then - now)};

        ok = pass_complete(ast, pass->name(), pass->wf(), index, stats) && ok;

        last_pass = pass->name();
      }

      return {ok, last_pass, ast, errors};
    }
  };
} // namespace trieste
