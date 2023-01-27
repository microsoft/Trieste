// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "source.h"

#include <re2/re2.h>

namespace trieste
{
  class REMatch
  {
  private:
    Source source;
    re2::StringPiece sp;
    std::vector<re2::StringPiece> match;
    std::vector<Location> locations;
    size_t matches = 0;

  public:
    REMatch(Source source, size_t max_capture = 0)
    : source(source), sp(source->view())
    {
      match.resize(max_capture + 1);
      locations.resize(max_capture + 1);
      locations[0] = {source, 0, 1};
    }

    bool empty()
    {
      return sp.empty();
    }

    bool consume(const RE2& regex)
    {
      matches = regex.NumberOfCapturingGroups() + 1;

      if (match.size() < matches)
        match.resize(matches);

      if (locations.size() < matches)
        locations.resize(matches);

      auto matched = regex.Match(
        sp,
        0,
        sp.length(),
        re2::RE2::ANCHOR_START,
        match.data(),
        static_cast<int>(matches));

      if (!matched || (match.at(0).size() == 0))
      {
        skip(0);
        return false;
      }

      for (size_t i = 0; i < matches; i++)
      {
        locations[i] = {
          source,
          static_cast<size_t>(match.at(i).data() - source->view().data()),
          match.at(i).size()};
      }

      sp.remove_prefix(match.at(0).size());
      return true;
    }

    const Location& at(size_t index = 0) const
    {
      if (index >= matches)
        return locations.at(0);

      return locations.at(index);
    }

    template<typename T>
    T parse(size_t index = 0) const
    {
      if (index >= matches)
        return T();

      T t;
      RE2::Arg arg(&t);
      auto& m = match.at(index);
      arg.Parse(m.data(), m.size());
      return t;
    }

    void skip(size_t count = 1)
    {
      sp.remove_prefix(count);
      match[0] = {};
      locations[0] = {
        source, static_cast<size_t>(sp.data() - source->view().data()), 1};
      matches = 0;
    }
  };
}
