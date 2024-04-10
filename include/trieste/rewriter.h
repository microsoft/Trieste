#pragma once

#include "passes.h"

namespace trieste
{
  class Rewriter
  {
  private:
    std::string name_;
    std::vector<Pass> passes_;
    const wf::Wellformed* wf_;
    bool debug_enabled_;
    bool wf_check_enabled_;
    std::filesystem::path debug_path_;

  public:
    Rewriter(
      const std::string& name,
      const std::vector<Pass>& passes,
      const wf::Wellformed& wf)
    : name_(name),
      passes_(passes),
      wf_(&wf),
      debug_enabled_(false),
      wf_check_enabled_(true),
      debug_path_(".")
    {}

    ProcessResult rewrite(Node ast)
    {
      PassRange pass_range(passes_, *wf_, name_);

      logging::Info summary;
      std::filesystem::path debug_path;
      if (debug_enabled_)
      {
        debug_path = debug_path_;
      }

      summary << "---------" << std::endl;
      auto result = Process(pass_range)
                      .set_check_well_formed(wf_check_enabled_)
                      .set_default_pass_complete(summary, name_, debug_path)
                      .run(ast);
      summary << "---------" << std::endl;
      return result;
    }

    Rewriter& debug_enabled(bool value)
    {
      debug_enabled_ = value;
      return *this;
    }

    bool debug_enabled() const
    {
      return debug_enabled_;
    }

    Rewriter& wf_check_enabled(bool value)
    {
      wf_check_enabled_ = value;
      return *this;
    }

    bool wf_check_enabled() const
    {
      return wf_check_enabled_;
    }

    Rewriter& debug_path(const std::filesystem::path& path)
    {
      debug_path_ = path;
      return *this;
    }

    const std::filesystem::path& debug_path() const
    {
      return debug_path_;
    }
  };
}
