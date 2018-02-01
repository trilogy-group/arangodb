////////////////////////////////////////////////////////////////////////////////
/// @brief test suite for ResultValue class
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2018, ArangoDB GmbH, Cologne, Germany
//
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
/// @author Jan Christoph Uhde
/// @author Dan Larkin-York
////////////////////////////////////////////////////////////////////////////////

#include "Basics/Result.h"
#include "Basics/ResultValue.h"

#include <type_traits>
#include "catch.hpp"

using namespace arangodb;
using namespace arangodb::basics;
using namespace std;

extern template class ResultValue<int>;
extern template class ResultValue<int*>;
extern template class ResultValue<std::string>;

ResultValue<int> function_a(int i) { return ResultValue<int>{i}; }

Result function_b() {
  auto rv = function_a(42);  // create one result and try modify / reuse
                             // it to make copy elision happen

  if (rv.ok()) {
    CHECK(rv.value == 42);  // do something with the value
  } else {
    rv.reset(rv.errorNumber(), "error in function_b: " + rv.errorMessage());
  }

  if (false) {
    rv.reset(23, std::string("the result is not valid because some other "
                             "condition did not hold"));
  }

  return rv.takeResult();  // still move the result forward
}

struct no_copy {
  int member = 4;
  no_copy() = default;
  no_copy(no_copy const&) = delete;
  no_copy& operator=(no_copy const&) = delete;
  no_copy(no_copy&& other) : member{std::move(other.member)} {}
  no_copy& operator=(no_copy&& other) {
    member = std::move(other.member);
    return *this;
  }
};

struct no_move {
  int member = 4;
  no_move() = default;
  no_move(no_move const& other) : member(other.member) {}
  no_move& operator=(no_move const& other) {
    member = other.member;
    return *this;
  }
  no_move(no_move&&) = delete;
  no_move& operator=(no_move&&) = default;
};

TEST_CASE("ResultTest", "[string]") {
  ////////////////////////////////////////////////////////////////////////////////
  /// @brief test_StringBuffer1
  ////////////////////////////////////////////////////////////////////////////////

  SECTION("test_ResultTest1") {
    int integer = 43;
    int& integer_lvalue_ref = integer;
    int const& integer_lvalue_cref = integer;
    int* integer_ptr = &integer;
    std::string str = "arangodb rocks";

    ResultValue<int> int_result{integer};
    CHECK((std::is_same<decltype(int_result.value), int>::value));
    CHECK((int_result.value == integer));

    ResultValue<int> int_result_from_ref{integer_lvalue_ref};
    CHECK((std::is_same<decltype(int_result.value), int>::value));
    CHECK((int_result.value == integer));

    ResultValue<int> int_result_from_cref{integer_lvalue_cref};
    CHECK((std::is_same<decltype(int_result.value), int>::value));
    CHECK((int_result.value == integer));

    ResultValue<int&> lvalue_ref_int_result{integer_lvalue_ref};
    CHECK((std::is_same<decltype(lvalue_ref_int_result.value), int&>::value));
    CHECK((lvalue_ref_int_result.value == integer_lvalue_ref));
    CHECK((lvalue_ref_int_result.value == integer));

    ResultValue<int const&> lvalue_cref_int_result{integer_lvalue_cref};
    CHECK((std::is_same<decltype(lvalue_cref_int_result.value),
                        int const&>::value));
    CHECK((lvalue_cref_int_result.value == integer_lvalue_cref));
    CHECK((lvalue_cref_int_result.value == integer));

    ResultValue<int> rvalue_int_result{std::move(integer)};
    CHECK((std::is_same<decltype(rvalue_int_result.value), int>::value));
    CHECK((rvalue_int_result.value == integer));

    ResultValue<int*> int_ptr_result{integer_ptr};
    CHECK((std::is_same<decltype(int_ptr_result.value), int*>::value));
    CHECK((int_ptr_result.value == integer_ptr));
    CHECK((*(int_ptr_result.value) == integer));

    ResultValue<std::string> string_result{str};
    CHECK((string_result.value == str));
    CHECK(("arangodb rocks" == str));

    ResultValue<std::string> string_move_result{std::move(str)};
    CHECK((str.empty()));
    CHECK((string_move_result.value == "arangodb rocks"));

    ResultValue<no_move> no_move_result{no_move{}};

    no_copy nc{};
    ResultValue<no_copy> no_copy_result{std::move(nc)};

    function_b();
  }
}

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|//
// --SECTION--\\|/// @\\}\\)" End:
