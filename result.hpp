#pragma once

#include <memory>

namespace func {
namespace result {
template <typename Ok, typename Err>
class Result {
 public:
  // NOLINTBEGIN
  using value_type = Ok;
  using error_type = Err;
  // NOLINTEND

 private:
  struct ValueTag {};
  struct ErrorTag {};

  Result(const Ok& val, ValueTag /*unused*/) : value(val), is_value_(true) {}
  Result(const Err& err, ErrorTag /*unused*/) : error(err) {}

  Result(Ok&& val, ValueTag /*unused*/)
      : value(std::move(val)), is_value_(true) {}
  Result(Err&& err, ErrorTag /*unused*/) : error(std::move(err)) {}

 public:
  static Result failure(Err&& err) {
    return Result(std::move(err), ErrorTag{});
  }
  static Result failure(const Err& err) { return Result(err, ErrorTag{}); }

  static Result success(Ok&& val) { return Result(std::move(val), ValueTag{}); }
  static Result success(const Ok& val) { return Result(val, ValueTag{}); }

 public:
  Result(const Result& other)
    requires(std::is_copy_constructible_v<Ok>
             && std::is_copy_constructible_v<Err>)
      : is_value_(other.is_value_) {
    copy_from(other);
  }

  Result& operator=(const Result& other)
    requires(std::is_copy_assignable_v<Ok> && std::is_copy_assignable_v<Err>)
  {
    if (this != &other) {
      destroy_active();
      is_value_ = other.is_value_;
      copy_from(other);
    }
    return *this;
  }

  Result(Result&& other) : is_value_(other.is_value_) {
    construct_from(std::move(other));
  }

  Result& operator=(Result&& other) {
    if (this != &other) {
      destroy_active();
      is_value_ = other.is_value_;

      construct_from(std::move(other));
    }

    return *this;
  }

  auto operator()() && { return std::move(*this); }
  auto operator()() const&
    requires(std::is_copy_constructible_v<Ok>
             && std::is_copy_constructible_v<Err>)
  {
    return *this;
  }

  ~Result() { destroy_active(); }

 public:
  // NOLINTBEGIN
  const Ok& Value() const& { return value; }
  Ok Value() && { return std::move(value); }

  const Err& Error() const& { return error; }
  Err Error() && { return std::move(error); }

  bool HasValue() const { return is_value_; }
  // NOLINTEND

 private:
  void destroy_active() {
    if (is_value_) {
      std::destroy_at(std::addressof(value));
    } else {
      std::destroy_at(std::addressof(error));
    }
  }

  void construct_from(Result&& other) {
    if (is_value_) {
      std::construct_at(std::addressof(value), std::move(other.value));
    } else {
      std::construct_at(std::addressof(error), std::move(other.error));
    }
  }

  void copy_from(const Result& other) {
    if (is_value_) {
      std::construct_at(std::addressof(value), other.value);
    } else {
      std::construct_at(std::addressof(error), other.error);
    }
  }

 private:
  union {
    Ok value;
    Err error;
  };
  bool is_value_ = false;
};

}  // namespace result
}  // namespace func