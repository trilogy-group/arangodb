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
/// @author Dr. Frank Celler
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
  using BaseValueType = typename std::remove_const<
      typename std::remove_reference<ValueType>::type>::type;
  ValueType value;

 public:
  template <bool x = std::is_default_constructible<ValueType>::value,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue() noexcept(
      std::is_nothrow_default_constructible<ValueType>::value);

  template <bool x = std::is_default_constructible<ValueType>::value,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(Result const& result) noexcept(false);

  template <bool x = std::is_default_constructible<ValueType>::value,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(Result&& result) noexcept(
      std::is_nothrow_default_constructible<ValueType>::value);

  template <bool x = std::is_lvalue_reference<ValueType>::value ||
                     std::is_copy_constructible<BaseValueType>::value,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType& value);

  template <bool x = std::is_lvalue_reference<ValueType>::value ||
                     std::is_copy_constructible<BaseValueType>::value,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType& value, Result result);

  template <bool x = (std::is_lvalue_reference<ValueType>::value &&
                      std::is_const<typename std::remove_reference<
                          ValueType>::type>::value) ||
                     (!std::is_reference<ValueType>::value &&
                      std::is_copy_constructible<BaseValueType>::value),
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType const& value);

  template <bool x = (std::is_lvalue_reference<ValueType>::value &&
                      std::is_const<typename std::remove_reference<
                          ValueType>::type>::value) ||
                     (!std::is_reference<ValueType>::value &&
                      std::is_copy_constructible<BaseValueType>::value),
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType const& value, Result result);

  template <bool x = !std::is_reference<ValueType>::value &&
                     std::is_move_constructible<BaseValueType>::value,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType&& value);

  template <bool x = !std::is_reference<ValueType>::value &&
                     std::is_move_constructible<BaseValueType>::value,
            typename std::enable_if<x>::type* = nullptr>
  ResultValue(BaseValueType&& value, Result result);

  // TODO add more constructors

  // forward to result's functions
  int errorNumber() const;
  std::string errorMessage() const&;
  std::string errorMessage() &&;

  bool ok() const;
  bool fail() const;

  bool is(int errorNumber) const;
  bool isNot(int errorNumber) const;

  // this function does not modify the value - it behaves exactly as it
  // does for the standalone result
  template <typename... Args>
  ResultValue& reset(Args&&... args);

  // some functions to directly retrieve the internal result
  Result const& getResult() const&;
  Result copyResult() const&;
  Result takeResult();

 private:
  Result _result;
};
#include "ResultValue.ipp"

}  // namespace basics
}  // namespace arangodb
#endif
