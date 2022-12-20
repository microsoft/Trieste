// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
#include "xoroshiro.h"

#include <functional>
#include <regex>

namespace trieste
{
  using sv_match = std::match_results<std::string_view::const_iterator>;

  using Rand = xoroshiro::p128r32;
  using Seed = uint64_t;
  using Result = uint32_t;

  using GenLocationF = std::function<std::string(Rand& rnd)>;
  using GenNodeLocationF = std::function<Location(Rand&, Node)>;
}
