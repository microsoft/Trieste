#pragma once

#include <array>

namespace trieste::detail
{
  /**
   * Maps tokens to values, with a modifiable default value.
   *
   * This is used by matching system.  If a rule applies generally, it is added
   * to all tokens, and if it applies to a specific token, it is added to that
   * token only.
   */
  template<typename T>
  class DefaultMap
  {
    // The default value for this map. This is returned when a specific value
    // has has not been set for the looked up token.
    T def{};

    // The map of specific values for tokens.
    std::array<T*, TokenDef::DEFAULT_MAP_TABLE_SIZE> map;

    // If this is true, then the map is empty, and the default value has not
    // been modified.
    bool empty_{true};

    bool is_index_default(size_t index) const
    {
      return map[index] == &def;
    }

    size_t token_index(const Token& t) const
    {
      return t.default_map_hash();
    }

  public:
    DefaultMap()
    {
      map.fill(&def);
    }

    DefaultMap(const DefaultMap& dm) : def(dm.def), empty_(dm.empty_)
    {
      for (size_t index = 0; index < map.size(); index++)
      {
        if (dm.is_index_default(index))
          map[index] = &def;
        else
          map[index] = new T(*dm.map[index]);
      }
    }

    /**
     *  Modify all values in the map, including the default value.
     *
     *  This is used for adding rules that do not specify an explicit start
     * token, or an explicit parent, so they need to apply generally.
     */
    template<typename F>
    void modify_all(F f)
    {
      empty_ = false;
      for (size_t i = 0; i < map.size(); i++)
        if (!is_index_default(i))
          f(*map[i]);
      f(def);
    }

    /**
     * Get a mutable reference to the value for a token.  If this does not have
     * a current value, first fill it with the current default value.
     */
    T& modify(const Token& t)
    {
      auto i = token_index(t);
      empty_ = false;
      // Use existing default set of rules.
      if (is_index_default(i))
        map[i] = new T(def);
      return *map[i];
    }

    /**
     * Get the value for a token. If this token has no specific value, return
     * the default value.
     */
    T& get(const Token& t)
    {
      return *map[token_index(t)];
    }

    /**
     * Clear all the values in the map, and the default value.
     */
    void clear()
    {
      empty_ = true;
      for (size_t i = 0; i < map.size(); i++)
      {
        if (!is_index_default(i))
        {
          delete map[i];
          map[i] = &def;
        }
      }
      def.clear();
    }

    ~DefaultMap()
    {
      clear();
    }

    /**
     * Returns true if modify has not been called since the last clear.
     */
    bool empty() const
    {
      return empty_;
    }
  };
}
