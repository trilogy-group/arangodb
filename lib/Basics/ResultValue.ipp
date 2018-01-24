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

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue() noexcept(
    std::is_nothrow_default_constructible<ValueType>::value)
    : value{}, _result{} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(Result const& result) noexcept(false)
    : value{}, _result{result} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(Result&& result) noexcept(
    std::is_nothrow_default_constructible<ValueType>::value)
    : value{}, _result{std::move(result)} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(ValueType v) : value{v}, _result{} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(ValueType v, Result result)
    : value{v}, _result{result} {}

template <typename ValueType>
template <bool x, typename std::enable_if<x>::type*>
ResultValue<ValueType>::ResultValue(ValueType&& v, Result result)
    : value{std::move(v)}, _result{result} {}

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
