# `func/effect`

Ленивая типизированная abstraction для построения вычислительных pipeline в C++.

`Effect<T, E>` представляет не готовый результат вычисления, а **отложенную программу**, которая при запуске завершается либо значением `T`, либо ошибкой `E`.

По семантике `Effect` близок к `Result<T, E>`, но вычисления выполняются только в момент форсирования pipeline.

```cpp
auto effect = effect::Value(10)
    | Map([](int value) {
        return value + 1;
      })
    | AndThen([](int value) {
        if (value < 20) {
          return effect::Error("too small");
        }

        return effect::Return(value * 2);
      });
```

На этом этапе пользовательский код не выполняется.

Pipeline будет вычислен только после вызова терминальной операции.

---

## Основные свойства

- **Lazy evaluation** — комбинаторы только строят pipeline и не запускают вычисления.
- **Static composition** — структура pipeline представлена типами на этапе компиляции.
- **Expression Templates** — композиция операций реализована без type erasure.
- **Zero-overhead abstractions** — отсутствуют виртуальные вызовы и `std::function`.
- **Без динамических аллокаций для представления pipeline**.
- **Short-circuit semantics** для `Ok` / `Error` веток.
- **Memoization** — thunk вычисляется не более одного раза.
- **Retry как часть pipeline**, а не внешний цикл вокруг вычисления.
- **Immutable pipeline** — каждый комбинатор создаёт новый тип вычисления.
- Поддерживается композиция отдельных pipeline до привязки к входному `Effect`.

---

## Мотивация

Обычный `Result<T, E>` хранит уже вычисленное значение:

```cpp
Result<int, Error> result = Compute();
```

Вызов `Compute()` происходит сразу.

`Effect<T, E>` вместо результата хранит способ его получения:

```cpp
auto effect = MakeEffect()
    | Map(...)
    | AndThen(...)
    | Retry().Times(3);
```

Это позволяет отделить:

1. описание вычисления;
2. композицию операций;
3. момент запуска вычисления.

До терминальной операции `Effect` остаётся обычным thunk.

---

## Создание Effect

### `Value`

Создаёт успешно завершающийся `Effect`.

```cpp
auto effect = effect::Value(42);
```

### `Return`

Эквивалентен `Value` с точки зрения наблюдаемой семантики.

```cpp
auto effect = effect::Return(42);
```

### `Error`

Создаёт вычисление, завершающееся ошибкой.

```cpp
auto effect = effect::Error("connection failed");
```

---

## Pipeline

Все комбинаторы применяются через оператор `|`.

```cpp
auto effect = effect::Value(10)
    | Map([](int x) {
        return x * 2;
      })
    | AndThen([](int x) {
        return effect::Value(x + 1);
      });
```

Комбинаторы строго ленивы: переданные им функции не вызываются во время построения pipeline.

---

## `Map`

Преобразует успешное значение.

```cpp
Effect<T, E>
    -> (T -> U)
    -> Effect<U, E>
```

Пример:

```cpp
auto effect = effect::Value(10)
    | Map([](int x) {
        return x * 2;
      });
```

Если предыдущий этап завершился ошибкой, функция `Map` не вызывается.

---

## `MapError`

Преобразует ошибку.

```cpp
Effect<T, E>
    -> (E -> Y)
    -> Effect<T, Y>
```

```cpp
auto effect = effect::Error("failed")
    | MapError([](std::string error) {
        return MyError{std::move(error)};
      });
```

Для успешного значения mapper ошибки не вызывается.

---

## `AndThen`

Последовательно связывает вычисления.

```cpp
Effect<T, E>
    -> (T -> Effect<U, E>)
    -> Effect<U, E>
```

```cpp
auto effect = effect::Value(10)
    | AndThen([](int value) {
        if (value > 0) {
          return effect::Value(value * 2);
        }

        return effect::Error("invalid value");
      });
```

`AndThen` вызывается только для успешного результата предыдущей стадии.

Ошибка проходит дальше без вызова handler'а.

---

## `OrElse`

Обрабатывает ошибочную ветку.

```cpp
Effect<T, E>
    -> (E -> Effect<T, Y>)
    -> Effect<T, Y>
```

```cpp
auto effect = effect::Error("temporary failure")
    | OrElse([](std::string error) {
        return effect::Value(0);
      });
```

Если предыдущий этап завершился успешно, обработчик ошибки не вызывается.

---

## Short-circuit semantics

Pipeline сохраняет семантику `Result`.

Для:

```cpp
auto effect = source
    | Map(f)
    | AndThen(g)
    | MapError(h)
    | OrElse(recover);
```

выполняется только активная ветка.

При ошибке:

```text
source
  |
  v
Error
  |
  +---- Map(f) -------- skipped
  |
  +---- AndThen(g) ---- skipped
  |
  +---- MapError(h) --- executed
```

При успешном результате:

```text
source
  |
  v
Value
  |
  +---- Map(f) -------- executed
  |
  +---- AndThen(g) ---- executed
  |
  +---- MapError(h) --- skipped
```

---

## Retry

`Retry` является частью самого pipeline.

```cpp
auto effect = operation
    | Retry().Times(3);
```

При ошибке повторно выполняется только соответствующий **segment** pipeline.

Segment определяется как участок:

- от начала pipeline до `Retry`;
- либо от предыдущего `Retry` до текущего.

Таким образом, retry не обязан повторять всю программу целиком.

---

## `Times`

Ограничивает количество попыток.

```cpp
auto effect = operation
    | Retry().Times(3);
```

---

## `After`

Добавляет задержку между попытками.

```cpp
using namespace std::chrono_literals;

auto effect = operation
    | Retry()
        .Times(3)
        .After(100ms);
```

Backoff является частью описания вычисления и начинает работать только после форсирования `Effect`.

---

## `While`

Позволяет повторять операцию только для определённых ошибок.

```cpp
auto effect = operation
    | Retry()
        .Times(5)
        .While([](const Error& error) {
            return error.retryable;
          });
```

Retry продолжается только если:

```cpp
predicate(error) == true
```

Иначе ошибка сразу передаётся следующему этапу pipeline.

---

## Комбинация Retry-стратегий

```cpp
auto effect = operation
    | Retry()
        .Times(5)
        .After(50ms)
        .While([](const Error& error) {
            return error.retryable;
          });
```

Можно использовать несколько retry-сегментов:

```cpp
auto effect = effect::Value(input)
    | AndThen(Stage1)
    | Retry().Times(3)
    | Map(Stage2)
    | AndThen(Stage3)
    | Retry()
        .Times(5)
        .After(100ms);
```

Каждый `Retry` отвечает только за свой участок pipeline.

---

## Форсирование вычисления

Пока terminal operation не вызван, pipeline остаётся ленивым.

```cpp
bool called = false;

auto effect = effect::Value(42)
    | Map([&](int x) {
        called = true;
        return x + 1;
      });

assert(!called);
```

После форсирования:

```cpp
auto value = std::move(effect).Value();

assert(called);
```

---

## `Value`

Извлекает успешное значение и при необходимости запускает вычисление.

```cpp
auto value = effect | Value();
```

Также поддерживается перемещающий доступ:

```cpp
auto value = std::move(effect) | Value();
```

---

## `Error`

Извлекает ошибку.

Если pipeline ещё не был вычислен, он сначала форсируется.

```cpp
auto error = std::move(effect).Error();
```

---

## `HasValue` / `HasError`

Позволяют наблюдать итоговое состояние вычисления.

```cpp
if (effect.HasValue()) {
    // ...
}

if (effect.HasError()) {
    // ...
}
```

Так как наблюдение требует знания результата, эти методы при необходимости запускают thunk.

---

## `Match`

Выполняет один из двух обработчиков в зависимости от результата.

```cpp
effect
    | Match(
        [](auto value) {
            std::cout << "value: " << value << '\n';
        },
        [](auto error) {
            std::cout << "error: " << error << '\n';
        });
```

---

## `Evaluate`

Принудительно вычисляет pipeline, сохраняя сам `Effect`.

```cpp
auto evaluated = effect | Evaluate();
```

После этого результат уже закэширован.

---

## Кэширование

Каждый `Effect` вычисляется не более одного раза.

```cpp
int counter = 0;

auto effect = effect::Value(1)
    | Map([&](int value) {
        ++counter;
        return value;
      });

effect.HasValue();
effect.HasValue();
effect.Value();

assert(counter == 1);
```

Первое наблюдение форсирует thunk.

Последующие обращения используют закэшированный результат.

---

## Pipeline composition

Комбинаторы можно объединять отдельно от конкретного `Effect`.

```cpp
auto pipeline =
    Map([](int x) {
        return x + 1;
      })
    | AndThen([](int x) {
        return effect::Value(x * 2);
      })
    | Map([](int x) {
        return x - 3;
      });
```

После этого pipeline можно применить к вычислению:

```cpp
auto effect =
    effect::Value(10)
    | pipeline;
```

Это позволяет описывать reusable вычислительные цепочки независимо от источника данных.

---

## Compile-time composition

Внутренняя структура pipeline кодируется в типе объекта.

Концептуально:

```cpp
effect::Value(10)
    | Map(f)
    | AndThen(g)
```

представляется не как контейнер:

```cpp
std::vector<std::function<...>>
```

а как статически вложенная композиция узлов:

```text
AndThenNode<
    MapNode<
        ValueNode<int>,
        F
    >,
    G
>
```

Благодаря этому реализация не требует:

- `std::function`;
- виртуальных методов;
- общего runtime-интерфейса для узлов;
- heap allocation для хранения каждого этапа pipeline.

Компилятор видит полную структуру вычисления и может оптимизировать её как обычный C++ код.

---

## Placeholder types

`Effect` поддерживает placeholder-типы для случаев, когда один из типов ещё невозможно определить в момент создания вычисления.

Например:

```cpp
auto effect = effect::Value(5);
```

Ошибка на этом этапе отсутствует, поэтому её конкретный тип может быть выведен позднее при дальнейшей композиции pipeline.

Placeholder не добавляет runtime-состояния и не влияет на размер `Effect`.

---

## Immutability

Pipeline иммутабелен.

Применение комбинатора не изменяет существующий `Effect`, а создаёт новое вычисление:

```cpp
auto first = effect::Value(10);

auto second =
    std::move(first)
    | Map([](int x) {
        return x * 2;
      });
```

Структура вычисления после создания не модифицируется.

---

## Design goals

Основной целью проекта было реализовать функциональную abstraction для ленивых вычислений без необходимости платить за неё дополнительным runtime-overhead.

Особое внимание уделялось:

- стоимости абстракций;
- количеству аллокаций;
- forwarding пользовательских callable-объектов;
- move semantics;
- short-circuit control flow;
- статическому выводу типов;
- lifetime вложенных pipeline;
- корректной retry-семантике;
- единственному вычислению thunk.

---

## Пример

```cpp
using namespace std::chrono_literals;

auto request =
    effect::Value(42)
    | Map([](int value) {
        return value + 1;
      })
    | AndThen([](int value) {
        if (value < 100) {
          return effect::Error(Error{
              .message = "temporary error",
              .retryable = true,
          });
        }

        return effect::Value(value);
      })
    | Retry()
        .Times(3)
        .After(100ms)
        .While([](const Error& error) {
            return error.retryable;
          })
    | OrElse([](Error error) {
        return effect::Value(-1);
      });

auto result = std::move(request).Value();
```

До последней строки pipeline только описывает вычисление.

Вся пользовательская логика начинает выполняться только при вызове `Value()`.

---

## Аналогии

По идее `Effect` близок к abstraction из функциональных effect systems:

- `cats-effect IO`;
- lazy monadic computations;
- typed error pipelines.

При этом реализация адаптирована под C++ и ориентирована на **compile-time composition и минимальный runtime overhead**.

---

## Используемые техники C++

Проект активно использует:

- C++20;
- variadic templates;
- perfect forwarding;
- move semantics;
- template metaprogramming;
- Expression Templates;
- compile-time type deduction;
- static polymorphism;
- `std::invoke`;
- concepts / constraints;
- RAII.

---

## Ограничения реализации

`Effect` сознательно не использует type erasure для представления вычислений.

Поэтому тип объекта зависит от структуры pipeline — аналогично типу lambda expression.

Это является частью дизайна и позволяет избежать runtime-dispatch и динамических аллокаций, сохраняя pipeline полностью доступным для оптимизации компилятором.

---
