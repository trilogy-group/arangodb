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

#ifndef ARANGODB_BASICS_RESULT_H
#define ARANGODB_BASICS_RESULT_H 1

#include <memory>
#include <string>
#include <type_traits>

namespace arangodb {
class Result {
 public:
  Result() noexcept;
  Result(int errorNumber) noexcept;
  Result(int errorNumber, std::string const& errorMessage);
  Result(int errorNumber, std::string&& errorMessage);

  // copy
  Result(Result const& other);
  Result& operator=(Result const& other);

  // move
  Result(Result&& other) noexcept;
  Result& operator=(Result&& other) noexcept;

 public:
  int errorNumber() const;
  std::string errorMessage() const&;
  std::string errorMessage() &&;

  bool ok() const;
  bool fail() const;

  bool is(int errorNumber) const;
  bool isNot(int errorNumber) const;

  Result& reset();
  Result& reset(int errorNumber);
  Result& reset(int errorNumber, std::string const& errorMessage);
  Result& reset(int errorNumber, std::string&& errorMessage) noexcept;
  Result& reset(Result const& other);
  Result& reset(Result&& other) noexcept;

 protected:
  int _errorNumber;
  mutable std::unique_ptr<std::string> _errorMessage;
};
}  // namespace arangodb

#endif
