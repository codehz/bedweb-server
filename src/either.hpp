#pragma once

#include <rpc.hpp>
#include <variant>

template <typename T> class either : std::variant<rpc::json, T> {
public:
  inline either(T const &inp) { this->emplace(inp); }
  inline either(rpc::json const &inp) { this->emplace(inp); }
  inline either(either const &rhs) { this->emplace(rhs); }
  inline bool is_left() { return this->index() == 0; }
  inline bool is_right() { return this->index() == 1; }
  template <typename L, typename R> inline auto map(L l, R r) {
    if (is_left()) return l(std::get<0>(*this));
    if (is_right()) return r(std::get<1>(*this));
  }
};