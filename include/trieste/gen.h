// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "ast.h"
#include "xoroshiro.h"

#include <functional>

namespace trieste
{
  using Rand = xoroshiro::p128r32;
  using Seed = uint64_t;
  using Result = uint32_t;

  using GenLocationF = std::function<std::string(Rand& rnd)>;
  using GenNodeLocationF = std::function<Location(Rand&, Node)>;
}
