#pragma once

#include "pass.h"
#include "wf.h"

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
    const wf::Wellformed* wf; // Well-formed condition for entry into this Range.
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

  /**
   * @brief Process is used to run a collection of rewrite passes on an Ast.
   * It provides a collection of hooks to produce output.
   */
  class Process
  {
    bool check_well_formed{true};

    std::function<bool(Node&, std::string, size_t index, PassStatistics&)>
      pass_complete;

    std::function<void(std::vector<Node>&, std::string)> error_pass;

  public:
    Process() {}

    /**
     * @brief After each pass the supplied function is called with the current
     * AST and details of the pass that has just completed.
     */
    void set_pass_complete(
      std::function<bool(Node&, std::string, size_t, PassStatistics&)> f)
    {
      pass_complete = f;
    }

    /**
     * @brief If a pass fails, then the supplied function is called with the
     * current AST and details of the pass that has just failed.
     */
    void set_error_pass(std::function<void(std::vector<Node>&, std::string)> f)
    {
      error_pass = f;
    }

    /**
     * @brief Specified is well-formedness should be checked between passes.
     */
    void set_check_well_formed(bool b)
    {
      check_well_formed = b;
    }

    template<typename PassIterator>
    bool validate(Node ast, PassRange<PassIterator> passes)
    {
      auto wf = passes.input_wf();
      auto ok = bool(ast);

      ok = ok && wf.build_st(ast);
      ok = ok && (!check_well_formed || wf.check(ast));

      auto errors = ast->get_errors();
      ok = ok && errors.empty();
      if (!ok)
        error_pass(errors, passes.entry_pass_name());

      return ok;
    }

    /**
     * @brief Run the supplied passes on the Ast.
     *
     * Returns the rewritten Ast, or an empty Node if the process failed.
     */
    template<typename PassIterator>
    bool build(Node& ast, PassRange<PassIterator> passes)
    {
      size_t index = 1;

      wf::push_back(passes.input_wf());

      // Check ast is well-formed before starting.
      auto ok = validate(ast, passes);

      for (; ok && passes.has_next(); index++)
      {
        logging::Debug() << "Starting pass: \"" << passes()->name() << "\"";

        auto now = std::chrono::high_resolution_clock::now();
        auto& pass = passes();
        wf::push_back(pass->wf());

        auto [new_ast, count, changes] = pass->run(ast);
        ast = new_ast;
        wf::pop_front();

        ++passes;

        ok = validate(ast, passes);

        auto then = std::chrono::high_resolution_clock::now();
        PassStatistics stats = {
          count,
          changes,
          std::chrono::duration_cast<std::chrono::microseconds>(then - now)};

        ok = pass_complete(ast, pass->name(), index, stats) && ok;
      }

      wf::pop_front();

      return ok;
    }
  };

  inline void print_errors(std::vector<Node>& errors, logging::Log& err)
  {
    logging::Sep sep{"----------------"};
    err << "Errors:";
    for (auto& error : errors)
    {
      err << sep << std::endl;
      for (auto& child : *error)
      {
        if (child->type() == ErrorMsg)
          err << child->location().view();
        else
          err << child->location().origin_linecol() << std::endl
              << child->location().str();
      }
    }
  }

  inline bool write_ast(
    Node& ast,
    std::filesystem::path output_directory,
    std::string language_name,
    std::string pass_name,
    size_t index)
  {
    if (output_directory.empty())
      return true;

    // Check if output_directory exists, and if not create it.
    if (!std::filesystem::exists(output_directory))
    {
      if (!std::filesystem::create_directories(output_directory))
      {
        logging::Error() << "Could not create output directory "
                         << output_directory;
        return false;
      }
    }

    std::filesystem::path output;
    if (index < 10)
    {
      output = output_directory /
        ("0" + std::to_string(index) + "_" + pass_name + ".trieste");
    }
    else
    {
      output = output_directory /
        (std::to_string(index) + "_" + pass_name + ".trieste");
    }

    std::ofstream f(output, std::ios::binary | std::ios::out);

    if (!f)
    {
      logging::Error() << "Could not open " << output << " for writing.";
      return false;
    }

    // Write the AST to the output file.
    f << language_name << std::endl << pass_name << std::endl << ast;
    return true;
  }

  /**
   * @brief A default configuration for the Process class. This configuration
   * can be used to write to a file each time a pass completes.
   *
   * @param check_well_formed - should check well-formedness between passes.
   * @param language_name - the name of the language being processed.
   * @param output_directory - the directory to write the output to.
   */
  inline Process default_process(
    logging::Log& summary,
    bool check_well_formed = false,
    std::string language_name = "",
    std::filesystem::path output_directory = {})
  {
    Process p;

    p.set_check_well_formed(check_well_formed);

    p.set_error_pass([](std::vector<Node>& errors, std::string name) {
      logging::Error err;
      print_errors(errors, err);
      err << "Pass " << name << " failed with " << errors.size() << " errors\n";
    });

    p.set_pass_complete(
      [output_directory, language_name, &summary](
        Node& ast, std::string pass_name, size_t index, PassStatistics& stats) {
        auto [count, changes, duration] = stats;
        std::string delim{"\t"};
        if (index == 1)
        {
          summary << "Pass" << delim << "Iterations" << delim << "Changes"
                  << delim << "Time (us)" << std::endl;
        }

        summary << pass_name << delim << count << delim << changes << delim
                << duration.count() << std::endl;

        return write_ast(
          ast, output_directory, language_name, pass_name, index);
      });

    return p;
  }
} // namespace trieste