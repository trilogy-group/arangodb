////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2017-2018 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dan Larkin-York
/// @author Jan Christoph Uhde
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_BASICS_RESULT_VALUE_H
#define ARANGODB_BASICS_RESULT_VALUE_H 1

#include <string>
#include <type_traits>

#include "Basics/Result.h"

namespace arangodb {
namespace basics {

template <typename ValueType>
class ResultValue {
 public:
  ValueType value;

 private:
  // define some local type traits to help with constructor declaration
  using BaseValueType = typename std::remove_const<
      typename std::remove_reference<ValueType>::type>::type;

  static constexpr bool kAllowCopyConstructor =
      std::is_copy_constructible<ValueType>::value;

  static constexpr bool kAllowCopyAssignment =
      std::is_copy_assignable<ValueType>::value;

  static constexpr bool kAllowMoveConstructor =
      std::is_move_constructible<ValueType>::value;

  static constexpr bool kAllowMoveAssignment =
      std::is_move_assignable<ValueType>::value;

  static constexpr bool kAllowDefaultConstruction =
      std::is_default_constructible<ValueType>::value;

  static constexpr bool kAllowConstructionFromBaseNonConstRef =
      std::is_lvalue_reference<ValueType>::value ||
      std::is_copy_constructible<BaseValueType>::value;

  static constexpr bool kAllowConstructionFromBaseConstRef =
      (std::is_lvalue_reference<ValueType>::value &&
       std::is_const<typename std::remove_reference<ValueType>::type>::value) ||
      (!std::is_reference<ValueType>::value &&
       std::is_copy_constructible<BaseValueType>::value);

  static constexpr bool kAllowConstructionFromBaseTemporary =
      !std::is_reference<ValueType>::value &&
      std::is_move_constructible<BaseValueType>::value;

 public:
  template <bool x = kAllowCopyConstructor,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(ResultValue<ValueType> const& other);

  template <bool x = kAllowCopyAssignment,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue& operator=(ResultValue<ValueType> const& other);

  template <bool x = kAllowMoveConstructor,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(ResultValue<ValueType>&& other);

  template <bool x = kAllowMoveAssignment,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue& operator=(ResultValue<ValueType>&& other);

  template <bool x = kAllowDefaultConstruction,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue() noexcept(
      std::is_nothrow_default_constructible<ValueType>::value);

  template <bool x = kAllowDefaultConstruction,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(Result const& result);

  template <bool x = kAllowDefaultConstruction,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(Result&& result) noexcept(
      std::is_nothrow_default_constructible<ValueType>::value);

  template <bool x = kAllowConstructionFromBaseNonConstRef,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType& value);

  template <bool x = kAllowConstructionFromBaseNonConstRef,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType& value, Result const& result);

  template <bool x = kAllowConstructionFromBaseNonConstRef,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType& value, Result&& result);

  template <bool x = kAllowConstructionFromBaseConstRef,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType const& value);

  template <bool x = kAllowConstructionFromBaseConstRef,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType const& value, Result const& result);

  template <bool x = kAllowConstructionFromBaseConstRef,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType const& value, Result&& result);

  template <bool x = kAllowConstructionFromBaseTemporary,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType&& value);

  template <bool x = kAllowConstructionFromBaseTemporary,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType&& value, Result const& result);

  template <bool x = kAllowConstructionFromBaseTemporary,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType&& value, Result&& result);

  // TODO add more constructors

  // TODO define assignment operators

  // forward to matching Result methods

  int errorNumber() const;
  std::string errorMessage() const&;
  std::string errorMessage() &&;

  bool ok() const;
  bool fail() const;

  bool is(int errorNumber) const;
  bool isNot(int errorNumber) const;

  // doesn't touch value, just forwards arguments to Result::reset
  template <typename... Args>
  ResultValue& reset(Args&&... args);

  // some functions to directly retrieve the internal result
  Result const& getResult() const&;
  Result copyResult() const&;
  Result takeResult();

 private:
  Result _result;
};

////////////////////////////////////////////////////////////////////////////////
// IMPLEMENTATION BELOW
////////////////////////////////////////////////////////////////////////////////

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(ResultValue<ValueType> const& other)
    : value{other.value}, _result{other._result} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>& ResultValue<ValueType>::operator=(
    ResultValue<ValueType> const& other) {
  value = other.value;
  _result = other._result;
  return *this;
}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(ResultValue<ValueType>&& other)
    : value{std::move(other.value)}, _result{std::move(other._result)} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>& ResultValue<ValueType>::operator=(
    ResultValue<ValueType>&& other) {
  value = std::move(other.value);
  _result = std::move(other._result);
  return *this;
}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue() noexcept(
    std::is_nothrow_default_constructible<ValueType>::value)
    : value{}, _result{} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(Result const& result)
    : value{}, _result{result} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(Result&& result) noexcept(
    std::is_nothrow_default_constructible<ValueType>::value)
    : value{}, _result{std::move(result)} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(BaseValueType& v) : value{v}, _result{} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(BaseValueType& v, Result const& result)
    : value{v}, _result{result} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(BaseValueType& v, Result&& result)
    : value{v}, _result{std::move(result)} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(BaseValueType const& v)
    : value{v}, _result{} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(BaseValueType const& v,
                                    Result const& result)
    : value{v}, _result{result} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(BaseValueType const& v, Result&& result)
    : value{v}, _result{std::move(result)} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(BaseValueType&& v)
    : value{std::move(v)}, _result{} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(BaseValueType&& v, Result const& result)
    : value{std::move(v)}, _result{result} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(BaseValueType&& v, Result&& result)
    : value{std::move(v)}, _result{std::move(result)} {}

template <typename ValueType>
int ResultValue<ValueType>::errorNumber() const {
  return _result.errorNumber();
}

template <typename ValueType>
std::string ResultValue<ValueType>::errorMessage() const& {
  return _result.errorMessage();
}

template <typename ValueType>
std::string ResultValue<ValueType>::errorMessage() && {
  return std::move(_result.errorMessage());
}

template <typename ValueType>
bool ResultValue<ValueType>::ok() const {
  return _result.ok();
}

template <typename ValueType>
bool ResultValue<ValueType>::fail() const {
  return _result.fail();
}

template <typename ValueType>
bool ResultValue<ValueType>::is(int errorNumber) const {
  return _result.is(errorNumber);
}

template <typename ValueType>
bool ResultValue<ValueType>::isNot(int errorNumber) const {
  return _result.isNot(errorNumber);
}

template <typename ValueType>
template <typename... Args>
ResultValue<ValueType>& ResultValue<ValueType>::reset(Args&&... args) {
  _result.reset(std::forward<Args>(args)...);
  return *this;
}

template <typename ValueType>
Result const& ResultValue<ValueType>::getResult() const& {
  return _result;
}

template <typename ValueType>
Result ResultValue<ValueType>::copyResult() const& {
  return _result;
}

template <typename ValueType>
Result ResultValue<ValueType>::takeResult() {
  return std::move(_result);
}

}  // namespace basics
}  // namespace arangodb
#endif
