#pragma once

#include <memory>

namespace func::cache {
template <typename T>
class Cache {
 public:
  Cache() : is_val_(false) {}

  ~Cache() {
    if (is_val_) {
      std::destroy_at(&value);
    }
  }

  Cache(const Cache& other) : is_val_(other.is_val_) {
    if (other.is_val_) {
      std::construct_at(&value, other.value);
    }
  }

  Cache(Cache&& other) : is_val_(other.is_val_) {
    if (other.is_val_) {
      std::construct_at(&value, std::move(other.value));
    }
  }

  const T& get() const { return value; }
  T& get() { return value; }

  bool has_value() const { return is_val_; }

  template <typename... Args>
  void emplace(Args&&... args) {
    is_val_ = true;
    std::construct_at(&value, std::forward<Args>(args)...);
  }

 private:
  union {
    T value;
  };
  bool is_val_;
};
}  // namespace func::cache