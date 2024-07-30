// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace trieste
{
  class SourceDef;
  struct Location;
  class NodeDef;
  using Source = std::shared_ptr<SourceDef>;
  using Node = std::shared_ptr<NodeDef>;

  class SourceDef
  {
  private:
    std::string origin_;
    std::string contents;
    std::vector<size_t> lines;

  public:
    static Source load(const std::filesystem::path& file)
    {
      std::ifstream f(file, std::ios::binary | std::ios::in | std::ios::ate);

      if (!f)
        return {};

      auto size = f.tellg();
      f.seekg(0, std::ios::beg);

      auto source = std::make_shared<SourceDef>();
      source->origin_ = std::filesystem::relative(file).string();
      source->contents.resize(static_cast<std::size_t>(size));
      f.read(&source->contents[0], size);

      if (!f)
        return {};

      source->find_lines();
      return source;
    }

    static Source synthetic(const std::string& contents)
    {
      auto source = std::make_shared<SourceDef>();
      source->contents = contents;
      source->find_lines();
      return source;
    }

    const std::string& origin() const
    {
      return origin_;
    }

    std::string_view view() const
    {
      return std::string_view(contents);
    }

    std::pair<size_t, size_t> linecol(size_t pos) const
    {
      // Lines and columns are 0-indexed.
      auto it = std::lower_bound(lines.begin(), lines.end(), pos);

      auto line = it - lines.begin();
      auto col = pos;

      if (it != lines.begin())
        col -= *(it - 1) + 1;

      return {line, col};
    }

    std::pair<size_t, size_t> linepos(size_t line) const
    {
      // Lines are 0-indexed.
      if (line > lines.size())
        return {std::string::npos, 0};

      size_t start = 0;
      auto end = contents.size();

      if (line > 0)
        start = lines[line - 1] + 1;

      if (line < lines.size())
        end = lines[line];

      return {start, end - start};
    }

  private:
    // Semantics note:
    // The code here only looks for \n and is not intended to be
    // platform-sensitive. Effectively, sources operate in binary mode and leave
    // encoding issues to the language implementation. There are however some
    // cosmetic fixes in error printing, such as in Location::str(), which
    // ensure that control characters don't leak into Trieste's output in that
    // case.
    void find_lines()
    {
      // Find the lines.
      auto pos = contents.find('\n');

      while (pos != std::string::npos)
      {
        lines.push_back(pos);
        pos = contents.find('\n', pos + 1);
      }
    }
  };

  struct Location
  {
    Source source;
    size_t pos;
    size_t len;

    Location() = default;

    Location(Source source_, size_t pos_, size_t len_)
    : source(source_), pos(pos_), len(len_)
    {}

    Location(const std::string& s)
    : source(SourceDef::synthetic(s)), pos(0), len(s.size())
    {}

    std::string_view view() const
    {
      if (!source)
        return {};

      return source->view().substr(pos, len);
    }

    std::string origin_linecol() const
    {
      std::stringstream ss;

      if (source && !source->origin().empty())
      {
        auto [line, col] = linecol();
        ss << source->origin() << ":" << (line + 1) << ":" << (col + 1);
      }

      return ss.str();
    }

    std::string str() const
    {
      if (!source)
        return {};

      std::stringstream ss;
      auto write_chars_skipping_r = [&ss](const std::string_view& str) -> void {
        for (char ch : str)
        {
          if (ch != '\r')
          {
            ss << ch;
          }
        }
      };
      auto write_indexed_skipping_r =
        [&ss](const std::string_view& str, auto fn) -> void {
        size_t idx = 0;
        for (char ch : str)
        {
          if (ch != '\r')
          {
            ss << fn(idx);
          }
          ++idx;
        }
      };

      auto [line, col] = linecol();
      auto [linepos, linelen] = source->linepos(line);

      if (view().find_first_of('\n') != std::string::npos)
      {
        auto line_view_first = source->view().substr(linepos, linelen);
        size_t col_last;
        std::string_view interim_view;
        std::string_view line_view_last;
        {
          auto [line2, col2] = source->linecol(pos + len);
          auto [linepos2, linelen2] = source->linepos(line2);
          line_view_last = source->view().substr(linepos2, linelen2);
          col_last = col2;

          // Find the lines in between first and last to insert, if there are
          // any such lines. If the lines are adjacent, this creates a 1 char
          // line view with the new line between the two.
          size_t interim_pos = linepos + linelen;
          interim_view =
            source->view().substr(interim_pos, linepos2 - interim_pos);
        }

        write_indexed_skipping_r(
          line_view_first, [&](size_t idx) { return idx < col ? ' ' : '~'; });
        ss << std::endl;
        write_chars_skipping_r(line_view_first);
        write_chars_skipping_r(interim_view);
        write_chars_skipping_r(line_view_last);
        ss << std::endl;
        write_indexed_skipping_r(
          line_view_last.substr(0, col_last), [&](size_t) { return '~'; });
        ss << std::endl;
      }
      else
      {
        auto line_view = source->view().substr(linepos, linelen);
        write_chars_skipping_r(line_view);
        ss << std::endl;

        assert(pos >= linepos);
        write_indexed_skipping_r(
          line_view.substr(0, pos - linepos + len),
          [&](size_t idx) { return idx < col ? ' ' : '~'; });
        ss << std::endl;
      }

      return ss.str();
    }

    std::pair<size_t, size_t> linecol() const
    {
      if (!source)
        return {0, 0};

      return source->linecol(pos);
    }

    Location operator*(const Location& that) const
    {
      if (source != that.source)
        return *this;

      auto lo = std::min(pos, that.pos);
      auto hi = std::max(pos + len, that.pos + that.len);
      return {source, lo, hi - lo};
    }

    Location& operator*=(const Location& that)
    {
      *this = *this * that;
      return *this;
    }

    bool operator==(const Location& that) const
    {
      return view() == that.view();
    }

    bool operator!=(const Location& that) const
    {
      return !(*this == that);
    }

    bool operator<(const Location& that) const
    {
      return view() < that.view();
    }

    bool operator<=(const Location& that) const
    {
      return (*this < that) || (*this == that);
    }

    bool operator>(const Location& that) const
    {
      return !(*this <= that);
    }

    bool operator>=(const Location& that) const
    {
      return !(*this < that);
    }
  };
}
