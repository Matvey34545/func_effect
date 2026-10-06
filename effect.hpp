#pragma once

#include <memory>

#include "result.hpp"
#include "combinator.hpp"
#include "thunk.hpp"
#include "cache.hpp"

namespace func {

namespace storage {
template <typename Ok, typename Err, typename Thunk>
class StorageEffect {
 public:
  StorageEffect(Thunk&& thunk) : thunk_(std::move(thunk)) {}
  StorageEffect(const Thunk& thunk) : thunk_(thunk) {}

  auto& force() const& {
    if (!cache_.has_value()) {
      cache_.emplace(thunk_());
    }
    return cache_.get();
  }

  auto force() && {
    if (!cache_.has_value()) {
      cache_.emplace(std::move(thunk_)());
    }
    return std::move(cache_.get());
  }

  auto get_thunk() && { return std::move(thunk_); }
  auto get_thunk() const& { return thunk_; }

 private:
  mutable Thunk thunk_;
  mutable cache::Cache<result::Result<Ok, Err>> cache_;
};

template <typename Ok, typename Err>
class StorageEffect<Ok, Err, result::Result<Ok, Err>> {
 private:
  using Thunk = result::Result<Ok, Err>;

 public:
  StorageEffect(Thunk&& thunk) : thunk_(std::move(thunk)) {}
  StorageEffect(const Thunk& thunk) : thunk_(thunk) {}

  auto force() && { return std::move(thunk_); }
  auto& force() const& { return thunk_; }
  auto get_thunk() && { return std::move(thunk_); }
  auto get_thunk() const& { return thunk_; }

 private:
  mutable result::Result<Ok, Err> thunk_;
};

}  // namespace storage

template <typename T>
concept IsValueThunk = requires { typename T::IsValueTag; };

template <typename Ok, typename Err, typename Thunk = result::Result<Ok, Err>>
class Effect {
 public:
  // NOLINTBEGIN
  using ThunkType = Thunk;
  using value_type = Ok;
  using error_type = Err;
  using IsEffectTag = void;
  // NOLINTEND

  template <typename, typename, typename>
  friend class Effect;

 public:
  explicit Effect(Thunk&& thunk) : storage_(std::move(thunk)) {}
  explicit Effect(const Thunk& thunk) : storage_(thunk) {}

  template <typename OtherOk, typename OtherThunk>
  Effect(Effect<OtherOk, _, OtherThunk>&& other)
    requires(!std::is_same_v<OtherOk, _>
             && std::is_same_v<Thunk, result::Result<Ok, Err>>
             && (std::is_convertible_v<OtherOk, Ok> || std::is_same_v<Ok, _>))
      : storage_([&other]() mutable {
          auto res = std::move(other).ExtractThunk()();
          if constexpr (std::is_same_v<Ok, _>) {
            return result::Result<Ok, Err>::success(_{});
          } else {
            return result::Result<Ok, Err>::success(std::move(res).Value());
          }
        }()) {}

  template <typename OtherErr, typename OtherThunk>
  Effect(Effect<_, OtherErr, OtherThunk>&& other)
    requires(!std::is_same_v<OtherErr, _>
             && std::is_same_v<Thunk, result::Result<Ok, Err>>
             && (std::is_convertible_v<OtherErr, Err>
                 || std::is_same_v<Err, _>))
      : storage_([&other]() mutable {
          auto res = std::move(other).ExtractThunk()();
          if constexpr (std::is_same_v<Err, _>) {
            return result::Result<Ok, Err>::failure(_{});
          } else {
            return result::Result<Ok, Err>::failure(std::move(res).Error());
          }
        }()) {}

  template <typename OtherThunk>
  Effect(Effect<_, _, OtherThunk>&& other)
    requires(std::is_same_v<Thunk, result::Result<Ok, Err>>)
      : storage_([&other]() mutable {
          auto res = std::move(other).ExtractThunk()();
          if (res.HasValue()) {
            return result::Result<Ok, Err>::success(Ok{res.Value()});
          }
          return result::Result<Ok, Err>::failure(Err{res.Error()});
        }()) {}

  Effect(Effect&& other) = default;
  Effect& operator=(Effect&& other) = default;

  Effect(const Effect&) = delete;
  Effect& operator=(const Effect&) = delete;
  // NOLINTBEGIN
  Ok Value() && { return std::move(std::move(*this).force()).Value(); }
  const Ok& Value() const& { return force().Value(); }

  Err Error() && { return std::move(std::move(*this).force()).Error(); }
  const Err& Error() const& { return force().Error(); }

  bool HasValue() const { return force().HasValue(); }
  bool HasError() const { return !HasValue(); }

  Thunk ExtractThunk() && { return std::move(storage_).get_thunk(); }
  Thunk ExtractThunk() const& { return storage_.get_thunk(); }
  // NOLINTEND

  // NOLINTNEXTLINE
  auto Evaluate() && {
    using ForceResult = std::decay_t<decltype(std::move(*this).force())>;
    return Effect<typename ForceResult::value_type,
                  typename ForceResult::error_type, ForceResult>(
        std::move(*this).force());
  }
  // NOLINTNEXTLINE
  auto Evaluate() const& {
    using ForceResult = std::decay_t<decltype(force())>;
    return Effect<typename ForceResult::value_type,
                  typename ForceResult::error_type, ForceResult>(force());
  }

  template <typename OnSuccess, typename OnError>
  // NOLINTNEXTLINE
  void Match(OnSuccess&& on_success, OnError&& on_error) && {
    auto res = std::move(*this).force();

    if (res.HasValue()) {
      std::forward<OnSuccess>(on_success)(std::move(res).Value());
    } else {
      std::forward<OnError>(on_error)(std::move(res).Error());
    }
  }

  template <typename OnSuccess, typename OnError>
  // NOLINTNEXTLINE
  void Match(OnSuccess&& on_success, OnError&& on_error) const& {
    const auto& res = force();

    if (res.HasValue()) {
      std::forward<OnSuccess>(on_success)(res.Value());
    } else {
      std::forward<OnError>(on_error)(res.Error());
    }
  }

 public:
  auto& force() const& { return storage_.force(); }
  auto force() && { return std::move(storage_).force(); }

 private:
  storage::StorageEffect<Ok, Err, Thunk> storage_;
};

namespace effect {
template <typename T>
auto Value(T&& value) {
  using Ok = std::decay_t<T>;
  using Err = _;
  return Effect<Ok, Err>(
      result::Result<Ok, Err>::success(std::forward<T>(value)));
}

template <typename T>
auto Return(T&& value) {
  return Value(std::forward<T>(value));
}

template <typename E>
auto Error(E&& error) {
  using Ok = _;
  using Err = std::decay_t<E>;
  return Effect<Ok, Err>(
      result::Result<Ok, Err>::failure(std::forward<E>(error)));
}

}  // namespace effect

namespace terminal {
template <typename T>
struct ValueOrTerminal {
  T fallback;

  template <typename Eff>
    requires combinator::IsEffect<Eff>
  friend auto operator|(Eff&& eff, ValueOrTerminal&& self) {
    return std::forward<Eff>(eff).HasValue() ? std::forward<Eff>(eff).Value() :
                                               std::move(self.fallback);
  }
};

template <typename E>
struct ErrorOrTerminal {
  E fallback;

  template <typename Eff>
    requires combinator::IsEffect<Eff>
  friend auto operator|(Eff&& eff, ErrorOrTerminal&& self) {
    return std::forward<Eff>(eff).HasError() ? std::forward<Eff>(eff).Error() :
                                               std::move(self.fallback);
  }
};

struct EvaluateTerminal {
  template <typename Eff>
    requires combinator::IsEffect<Eff>
  friend auto operator|(Eff&& eff, EvaluateTerminal&& /*unused*/) {
    return std::forward<Eff>(eff).Evaluate();
  }
};

template <typename OnSuccess, typename OnError>
struct MatchTerminal {
  OnSuccess on_success;
  OnError on_error;

  template <typename Eff>
    requires combinator::IsEffect<std::remove_cvref_t<Eff>>
  friend void operator|(Eff&& eff, MatchTerminal&& self) {
    std::forward<Eff>(eff).Match(std::move(self.on_success),
                                 std::move(self.on_error));
  }
};

}  // namespace terminal

inline auto HasValue() {
  return terminal::HasValue{};
}
inline auto HasError() {
  return terminal::HasError{};
}
inline auto Value() {
  return terminal::Value{};
}
inline auto Error() {
  return terminal::Error{};
}

inline auto Retry(std::size_t n = std::numeric_limits<std::size_t>::max()) {
  return thunk::RetryConfig{n, std::chrono::milliseconds(0),
                            [](const auto&) { return true; }};
}

inline auto Evaluate() {
  return terminal::EvaluateTerminal{};
}

template <typename T>
auto ValueOr(T&& fallback) {
  return terminal::ValueOrTerminal<std::decay_t<T>>{std::forward<T>(fallback)};
}

template <typename E>
auto ErrorOr(E&& fallback) {
  return terminal::ErrorOrTerminal<std::decay_t<E>>{std::forward<E>(fallback)};
}

template <typename OnSuccess, typename OnError>
auto Match(OnSuccess&& on_ok, OnError&& on_err) {
  return terminal::MatchTerminal<std::decay_t<OnSuccess>,
                                 std::decay_t<OnError>>{
      std::forward<OnSuccess>(on_ok), std::forward<OnError>(on_err)};
}

template <typename F>
auto Map(F&& f_user) {
  auto raw_logic = [func = std::forward<F>(f_user)](auto&& eff) mutable {
    using EffT = std::decay_t<decltype(eff)>;
    using Traits = detail::MapTraits<decltype(func), EffT>;
    using NewThunk = thunk::MapThunk<typename EffT::ThunkType, std::decay_t<F>>;
    using NewEffect =
        Effect<typename Traits::NewOk, typename Traits::NewErr, NewThunk>;

    return NewEffect(NewThunk(std::forward<decltype(eff)>(eff).ExtractThunk(),
                              std::forward<decltype(func)>(func)));
  };

  using Atom = combinator::AtomModifier<decltype(raw_logic)>;
  return combinator::Combinator<Atom>{Atom{std::move(raw_logic)}};
}

template <typename F>
auto MapError(F&& f_user) {
  auto raw_logic = [func = std::forward<F>(f_user)](auto&& eff) mutable {
    using EffT = std::decay_t<decltype(eff)>;
    using Traits = detail::MapErrorTraits<decltype(func), EffT>;
    using NewThunk =
        thunk::MapErrorThunk<typename EffT::ThunkType, std::decay_t<F>>;
    using NewEffect =
        Effect<typename Traits::NewOk, typename Traits::NewErr, NewThunk>;

    return NewEffect(NewThunk(std::forward<decltype(eff)>(eff).ExtractThunk(),
                              std::forward<decltype(func)>(func)));
  };

  using Atom = combinator::AtomModifier<decltype(raw_logic)>;
  return combinator::Combinator<Atom>{Atom{std::move(raw_logic)}};
}

template <typename F>
auto AndThen(F&& f_user) {
  auto raw_logic = [func = std::forward<F>(f_user)](auto&& eff) mutable {
    using EffT = std::decay_t<decltype(eff)>;
    using Traits = detail::AndThenTraits<decltype(func), EffT>;
    using NewThunk =
        thunk::AndThenThunk<typename EffT::ThunkType, std::decay_t<F>>;
    using NewEffect =
        Effect<typename Traits::NewOk, typename Traits::NewErr, NewThunk>;

    return NewEffect(NewThunk(std::forward<decltype(eff)>(eff).ExtractThunk(),
                              std::forward<decltype(func)>(func)));
  };

  using Atom = combinator::AtomModifier<decltype(raw_logic)>;
  return combinator::Combinator<Atom>{Atom{std::move(raw_logic)}};
}

template <typename F>
auto OrElse(F&& f_user) {
  auto raw_logic = [func = std::forward<F>(f_user)](auto&& eff) mutable {
    using EffT = std::decay_t<decltype(eff)>;
    using Traits = detail::OrElseTraits<decltype(func), EffT>;
    using NewThunk =
        thunk::OrElseThunk<typename EffT::ThunkType, std::decay_t<F>>;
    using NewEffect =
        Effect<typename Traits::NewOk, typename Traits::NewErr, NewThunk>;

    return NewEffect(NewThunk(std::forward<decltype(eff)>(eff).ExtractThunk(),
                              std::forward<decltype(func)>(func)));
  };

  using Atom = combinator::AtomModifier<decltype(raw_logic)>;
  return combinator::Combinator<Atom>{Atom{std::move(raw_logic)}};
}

}  // namespace func