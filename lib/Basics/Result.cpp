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

#include "Result.h"
#include "Basics/Common.h"

using namespace arangodb;

Result::Result() noexcept : _errorNumber(TRI_ERROR_NO_ERROR) {}

Result::Result(int errorNumber) noexcept : _errorNumber(errorNumber) {}

Result::Result(int errorNumber, std::string const& errorMessage)
    : _errorNumber(errorNumber),
      _errorMessage(std::make_unique<std::string>(errorMessage)) {}

Result::Result(int errorNumber, std::string&& errorMessage)
    : _errorNumber(errorNumber),
      _errorMessage(std::make_unique<std::string>(std::move(errorMessage))) {}

// copy
Result::Result(Result const& other)
    : _errorNumber(other._errorNumber),
      _errorMessage(other._errorMessage
                        ? std::make_unique<std::string>(*other._errorMessage)
                        : nullptr) {}

Result& Result::operator=(Result const& other) {
  _errorNumber = other._errorNumber;
  _errorMessage = other._errorMessage
                      ? std::make_unique<std::string>(*other._errorMessage)
                      : nullptr;
  return *this;
}

// move
Result::Result(Result&& other) noexcept
    : _errorNumber(other._errorNumber),
      _errorMessage(std::move(other._errorMessage)) {}

Result& Result::operator=(Result&& other) noexcept {
  _errorNumber = other._errorNumber;
  _errorMessage = std::move(other._errorMessage);
  return *this;
}

int Result::errorNumber() const { return _errorNumber; }

std::string Result::errorMessage() const& {
  if (!_errorMessage) {
    if (_errorNumber != TRI_ERROR_NO_ERROR) {
      _errorMessage =
          std::make_unique<std::string>(TRI_errno_string(_errorNumber));
    } else {
      _errorMessage = std::make_unique<std::string>();
    }
  }
  return *_errorMessage;
}

std::string Result::errorMessage() && {
  if (!_errorMessage) {
    if (_errorNumber != TRI_ERROR_NO_ERROR) {
      _errorMessage =
          std::make_unique<std::string>(TRI_errno_string(_errorNumber));
    } else {
      _errorMessage = std::make_unique<std::string>();
    }
  }
  std::string* tmp = _errorMessage.get();
  _errorMessage.release();
  std::string msg{std::move(*tmp)};
  return msg;
}

bool Result::ok() const { return _errorNumber == TRI_ERROR_NO_ERROR; }
bool Result::fail() const { return !ok(); }

bool Result::is(int errorNumber) const { return _errorNumber == errorNumber; }
bool Result::isNot(int errorNumber) const { return !is(errorNumber); }

Result& Result::reset() {
  return reset(TRI_ERROR_NO_ERROR);
}

Result& Result::reset(int errorNumber) {
  _errorNumber = errorNumber;
  _errorMessage.reset();
  return *this;
}

Result& Result::reset(int errorNumber, std::string const& errorMessage) {
  _errorNumber = errorNumber;
  _errorMessage = std::make_unique<std::string>(errorMessage);
  return *this;
}

Result& Result::reset(int errorNumber, std::string&& errorMessage) noexcept {
  _errorNumber = errorNumber;
  _errorMessage = std::make_unique<std::string>(std::move(errorMessage));
  return *this;
}

Result& Result::reset(Result const& other) {
  _errorNumber = other._errorNumber;
  _errorMessage = other._errorMessage
                      ? std::make_unique<std::string>(*other._errorMessage)
                      : nullptr;
  return *this;
}

Result& Result::reset(Result&& other) noexcept {
  _errorNumber = other._errorNumber;
  _errorMessage = std::move(other._errorMessage);
  return *this;
}
