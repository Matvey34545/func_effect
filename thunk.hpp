#pragma once

#include <memory>

#include "result.hpp"
#include "testing/sleep.hpp"
#include "cache.hpp"
#include "combinator.hpp"

namespace func {
template <typename Ok, typename Err, typename Thunk>
class Effect;

struct _ {
  template <typename T>
  operator T() const {
    throw std::runtime_error("Placeholder conversion called!");
  };
};

namespace detail {

template <typename T, typename = void>
struct Contract {
  using ResultT = decltype(std::declval<std::decay_t<T>>()());
  using Ok = typename ResultT::value_type;
  using Err = typename ResultT::error_type;
};

template <typename T>
struct Contract<T, std::void_t<typename std::decay_t<T>::value_type>> {
  using Ok = typename std::decay_t<T>::value_type;
  using Err = typename std::decay_t<T>::error_type;
};

template <typename Expected, typename Fallback>
using Resolve =
    std::conditional_t<std::is_same_v<Expected, _>, Fallback, Expected>;

template <typename F, typename Source>
struct MapTraits {
  using In = Contract<Source>;
  using NewOk = std::invoke_result_t<std::decay_t<F>, typename In::Ok>;
  using NewErr = typename In::Err;
  using ResultT = result::Result<NewOk, NewErr>;
};

template <typename F, typename Source>
struct MapErrorTraits {
  using In = Contract<Source>;
  using NewOk = typename In::Ok;
  using NewErr = std::invoke_result_t<std::decay_t<F>, typename In::Err>;
  using ResultT = result::Result<NewOk, NewErr>;
};

template <typename F, typename Source>
struct AndThenTraits {
  using In = Contract<Source>;
  using NextEff = std::invoke_result_t<std::decay_t<F>, typename In::Ok>;
  using NewOk = Resolve<typename NextEff::value_type, typename In::Ok>;
  using NewErr = Resolve<typename NextEff::error_type, typename In::Err>;
  using ResultT = result::Result<NewOk, NewErr>;
};

template <typename F, typename Source>
struct OrElseTraits {
  using In = Contract<Source>;
  using NextEff = std::invoke_result_t<std::decay_t<F>, typename In::Err>;
  using NewOk = Resolve<typename NextEff::value_type, typename In::Ok>;
  using NewErr = Resolve<typename NextEff::error_type, typename In::Err>;
  using ResultT = result::Result<NewOk, NewErr>;
};

} // namespace detail

namespace thunk {
template <typename PrevThunk, typename F>
class MapThunk {
 private:
  using Traits = detail::MapTraits<F, PrevThunk>;
  using NewResult = typename Traits::ResultT;

 public:
  template <typename P, typename Func>
  MapThunk(P&& prev, Func&& func)
      : prev_(std::forward<P>(prev)), func_(std::forward<Func>(func)) {}

  auto operator()() & {
    auto res = prev_();

    if (!res.HasValue()) {
      return NewResult::failure(std::move(res).Error());
    }
    return NewResult::success(func_(std::move(res).Value()));
  }

  auto operator()() && {
    auto res = std::move(prev_)();

    if (!res.HasValue()) {
      return NewResult::failure(std::move(res).Error());
    }
    return NewResult::success(std::move(func_)(std::move(res).Value()));
  }

 private:
  PrevThunk prev_;
  F func_;
};

template <typename PrevThunk, typename F>
class MapErrorThunk {
 private:
  using Traits = detail::MapErrorTraits<F, PrevThunk>;
  using NewResult = typename Traits::ResultT;

 public:
  template <typename P, typename Func>
  MapErrorThunk(P&& prev, Func&& func)
      : prev_(std::forward<P>(prev)), func_(std::forward<Func>(func)) {}

  auto operator()() & {
    auto res = prev_();

    if (res.HasValue()) {
      return NewResult::success(std::move(res).Value());
    }
    return NewResult::failure(func_(std::move(res).Error()));
  }

  auto operator()() && {
    auto res = std::move(prev_)();

    if (res.HasValue()) {
      return NewResult::success(std::move(res).Value());
    }
    return NewResult::failure(std::move(func_)(std::move(res).Error()));
  }

 private:
  PrevThunk prev_;
  F func_;
};

template <typename PrevThunk, typename F>
class AndThenThunk {
 private:
  using Traits = detail::AndThenTraits<F, PrevThunk>;
  using NewResult = typename Traits::ResultT;

 public:
  template <typename P, typename Func>
  AndThenThunk(P&& prev, Func&& func)
      : prev_(std::forward<P>(prev)), func_(std::forward<Func>(func)) {}

  auto operator()() & {
    auto res = prev_();
    if (!res.HasValue()) {
      return NewResult::failure(std::move(res).Error());
    }

    auto next_eff = func_(std::move(res).Value());
    auto& next_res = next_eff.force();
    if (next_res.HasValue()) {
      return NewResult::success(std::move(next_res).Value());
    }
    return NewResult::failure(std::move(next_res).Error());
  }

  auto operator()() && {
    auto res = std::move(prev_)();
    if (!res.HasValue()) {
      return NewResult::failure(std::move(res).Error());
    }

    auto next_res = std::move(func_)(std::move(res).Value()).force();
    if (next_res.HasValue()) {
      return NewResult::success(std::move(next_res).Value());
    }
    return NewResult::failure(std::move(next_res).Error());
  }

 private:
  PrevThunk prev_;
  F func_;
};

template <typename PrevThunk, typename F>
class OrElseThunk {
 private:
  using Traits = detail::OrElseTraits<F, PrevThunk>;
  using NewResult = typename Traits::ResultT;

 public:
  template <typename P, typename Func>
  OrElseThunk(P&& prev, Func&& func)
      : prev_(std::forward<P>(prev)), func_(std::forward<Func>(func)) {}

  auto operator()() & {
    auto res = prev_();
    if (res.HasValue()) {
      return NewResult::success(std::move(res).Value());
    }

    auto next_eff = func_(std::move(res).Error());
    auto& next_res = next_eff.force();

    if (next_res.HasValue()) {
      return NewResult::success(std::move(next_res).Value());
    }
    return NewResult::failure(std::move(next_res).Error());
  }

  auto operator()() && {
    auto res = std::move(prev_)();
    if (res.HasValue()) {
      return NewResult::success(std::move(res).Value());
    }

    auto next_res = std::move(func_)(std::move(res).Error()).force();
    if (next_res.HasValue()) {
      return NewResult::success(std::move(next_res).Value());
    }
    return NewResult::failure(std::move(next_res).Error());
  }

 private:
  PrevThunk prev_;
  F func_;
};

template <typename PrevThunk, typename Predicate>
class RetryThunk;

template <typename Predicate>
struct RetryConfig {
  size_t times_count = 0;
  std::chrono::milliseconds delay_ms{0};
  Predicate predicate;

  RetryConfig(size_t time, std::chrono::milliseconds delay, Predicate pred)
      : times_count(time), delay_ms(delay), predicate(std::move(pred)) {}

  // NOLINTNEXTLINE
  auto Times(size_t n) const {
    return RetryConfig<Predicate>{n, delay_ms, predicate};
  }
  // NOLINTNEXTLINE
  auto After(std::chrono::milliseconds ms) const {
    return RetryConfig<Predicate>{times_count, ms, predicate};
  }

  template <typename NewPred>
  // NOLINTNEXTLINE
  auto While(NewPred&& p) const {
    return RetryConfig<std::decay_t<NewPred>>{times_count, delay_ms,
                                              std::forward<NewPred>(p)};
  }

  auto build_combinator() && {
    auto modifier = [config = std::move(*this)](auto&& eff) mutable {
      using Eff = std::decay_t<decltype(eff)>;
      using Ok = typename Eff::value_type;
      using Err = typename Eff::error_type;
      using RThunk = RetryThunk<typename Eff::ThunkType, Predicate>;

      return Effect<Ok, Err, RThunk>(RThunk(
          std::forward<decltype(eff)>(eff).ExtractThunk(), std::move(config)));
    };

    return combinator::Combinator{
        combinator::AtomModifier{std::move(modifier)}};
  }
};

template <typename PrevThunk, typename Predicate>
class RetryThunk {
 public:
  RetryThunk(PrevThunk&& prev, RetryConfig<Predicate> config)
      : prev_(std::move(prev)), config_(std::move(config)) {}

  auto operator()() {
    if (memoized_result_.has_value()) {
      return memoized_result_.get();
    }

    for (std::size_t i = 0;; ++i) {
      auto res = prev_();

      if (res.HasValue()) {
        memoized_result_.emplace(res);
        return res;
      }

      bool can_retry =
          (i < config_.times_count) && config_.predicate(res.Error());

      if (!can_retry) {
        return res;
      }

      if (config_.delay_ms.count() > 0) {
        testing::SleepFor(config_.delay_ms);
      }
    }
  }

 private:
  PrevThunk prev_;
  RetryConfig<Predicate> config_;
  cache::Cache<decltype(std::declval<PrevThunk>()())> memoized_result_;
};

template <typename Eff, typename Predicate>
  requires combinator::IsEffect<Eff>
auto operator|(Eff&& eff, RetryConfig<Predicate>&& config) {
  return std::forward<Eff>(eff) | std::move(config).build_combinator();
}

template <typename Predicate, typename C>
  requires combinator::IsCombinator<C>
auto operator|(RetryConfig<Predicate>&& config, C&& second) {
  return std::move(config).build_combinator() | std::forward<C>(second);
}

}  // namespace thunk
}  // namespace func