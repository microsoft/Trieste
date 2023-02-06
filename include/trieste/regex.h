// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "source.h"

#include <re2/re2.h>

namespace trieste
{
  class REMatch
  {
    friend class REIterator;

  private:
    std::vector<re2::StringPiece> match;
    std::vector<Location> locations;
    size_t matches = 0;

    bool match_regexp(const RE2& regex, re2::StringPiece& sp, Source& source)
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
        return false;
      }

      for (size_t i = 0; i < matches; i++)
      {
        locations[i] = {
          source,
          static_cast<size_t>(match.at(i).data() - source->view().data()),
          match.at(i).size()};
      }

      return true;
    }

  public:
    REMatch(size_t max_capture = 0)
    {
      match.resize(max_capture + 1);
      locations.resize(max_capture + 1);
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
  };

  class REIterator
  {
  private:
    Source source;
    re2::StringPiece sp;

  public:
    REIterator(Source source) : source(source), sp(source->view()) {}

    bool empty()
    {
      return sp.empty();
    }

    bool consume(const RE2& regex, REMatch& m)
    {
      if (!m.match_regexp(regex, sp, source))
        return false;

      sp.remove_prefix(m.at(0).len);
      return true;
    }

    Location current() const
    {
      return {
        source, static_cast<size_t>(sp.data() - source->view().data()), 1};
    }

    void skip(size_t count = 1)
    {
      sp.remove_prefix(count);
    }
  };
}
