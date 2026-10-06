#pragma once

#include <memory>

namespace func {
namespace combinator {

template <typename T>
concept IsCombinator = requires { typename std::decay_t<T>::IsCombinatorTag; };

template <typename T>
concept IsEffect = requires { typename std::decay_t<T>::IsEffectTag; };

template <typename T>
concept IsTerminator = requires { typename std::decay_t<T>::IsTerminatorTag; };

template <typename F>
struct AtomModifier {
  F f;

  template <typename Eff>
  auto operator()(Eff&& eff) & {
    return f(std::forward<Eff>(eff));
  }

  template <typename Eff>
  auto operator()(Eff&& eff) && {
    return std::move(f)(std::forward<Eff>(eff));
  }
};

template <typename F, typename G>
struct CompositeModifier {
  F f;
  G g;

  template <typename Eff>
  auto operator()(Eff&& eff) & {
    return g(f(std::forward<Eff>(eff)));
  }

  template <typename Eff>
  auto operator()(Eff&& eff) && {
    return std::move(g)(std::move(f)(std::forward<Eff>(eff)));
  }
};

template <typename M>
struct Terminator {
  using IsTerminatorTag = void;
  M modifier;
};

template <typename M>
struct Combinator {
  using IsCombinatorTag = void;
  M modifier;
};

}  // namespace combinator

namespace terminal {

// NOLINTBEGIN
struct PolicyValue {
  template <typename E>
  static decltype(auto) Apply(E&& e) {
    return std::forward<E>(e).Value();
  }
};

struct PolicyError {
  template <typename E>
  static decltype(auto) Apply(E&& e) {
    return std::forward<E>(e).Error();
  }
};

struct PolicyHasValue {
  template <typename E>
  static decltype(auto) Apply(E&& e) {
    return std::forward<E>(e).HasValue();
  }
};

struct PolicyHasError {
  template <typename E>
  static decltype(auto) Apply(E&& e) {
    return std::forward<E>(e).HasError();
  }
};
// NOLINTEND

template <typename Eff, typename Policy>
class LazyRefProxy {
 public:
  explicit LazyRefProxy(Eff& eff) : eff_(eff) {}
  operator decltype(auto)() const { return Policy::Apply(eff_); }

 private:
  Eff& eff_;
};

template <typename Eff, typename Policy>
class LazyValProxy {
  using RawResult = decltype(Policy::Apply(std::declval<Eff&&>()));
  using StoredResult = std::decay_t<RawResult>;

 public:
  explicit LazyValProxy(Eff&& eff) : eff_(std::move(eff)) {}
  operator decltype(auto)() const { return Policy::Apply(std::move(eff_)); }

 private:
  mutable Eff eff_;
};

template <typename Policy>
struct BaseTerminator {
  using IsTerminatorTag = void;

  struct ModifierFunctor {
    template <typename Eff>
    auto operator()(Eff& eff) const {
      return LazyRefProxy<Eff, Policy>(eff);
    }

    template <typename Eff>
    auto operator()(const Eff& eff) const {
      return LazyRefProxy<const Eff, Policy>(eff);
    }

    template <typename Eff>
    auto operator()(Eff&& eff) const {
      return LazyValProxy<std::decay_t<Eff>, Policy>(std::forward<Eff>(eff));
    }
  };

  ModifierFunctor modifier;
};

template <typename Eff, typename Policy, typename T>
bool operator==(const LazyRefProxy<Eff, Policy>& proxy, const T& value) {
  using RetType = decltype(Policy::Apply(std::declval<Eff&>()));
  return static_cast<RetType>(proxy) == value;
}

template <typename T, typename Eff, typename Policy>
bool operator==(const T& value, const LazyRefProxy<Eff, Policy>& proxy) {
  return proxy == value;
}

template <typename Eff, typename Policy, typename T>
bool operator==(const LazyValProxy<Eff, Policy>& proxy, const T& value) {
  using RetType = decltype(Policy::Apply(std::declval<Eff&&>()));
  return static_cast<RetType>(
             std::move(const_cast<LazyValProxy<Eff, Policy>&>(proxy)))
         == value;
}

template <typename T, typename Eff, typename Policy>
bool operator==(const T& value, const LazyValProxy<Eff, Policy>& proxy) {
  return std::move(proxy) == value;
}

struct Value : BaseTerminator<PolicyValue> {};
struct Error : BaseTerminator<PolicyError> {};
struct HasValue : BaseTerminator<PolicyHasValue> {};
struct HasError : BaseTerminator<PolicyHasError> {};

}  // namespace terminal

template <typename Eff, typename Self>
  requires combinator::IsEffect<Eff>
           && (combinator::IsCombinator<Self> || combinator::IsTerminator<Self>)
auto operator|(Eff&& eff, Self&& self) {
  return std::forward<Self>(self).modifier(std::forward<Eff>(eff));
}

template <typename C1, typename C2>
  requires combinator::IsCombinator<C1>
           && (combinator::IsTerminator<C2> || combinator::IsCombinator<C2>)
auto operator|(C1&& first, C2&& second) {
  using FT = std::decay_t<decltype(first.modifier)>;
  using GT = std::decay_t<decltype(second.modifier)>;

  combinator::CompositeModifier<FT, GT> comp{std::forward<C1>(first).modifier,
                                             std::forward<C2>(second).modifier};

  if constexpr (combinator::IsCombinator<C2>) {
    return combinator::Combinator<decltype(comp)>{std::move(comp)};
  } else {
    return combinator::Terminator<decltype(comp)>{std::move(comp)};
  }
}

}  // namespace func