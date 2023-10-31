#pragma once

#include "parse.h"
#include "passes.h"

namespace CLI
{
  class App;
}

namespace trieste
{
  struct Options
  {
    virtual void configure(CLI::App&) {}
  };
} // namespace trieste