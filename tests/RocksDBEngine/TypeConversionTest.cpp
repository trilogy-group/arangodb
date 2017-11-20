////////////////////////////////////////////////////////////////////////////////
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triagens GmbH, Cologne, Germany
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
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Jan Christoph Uhde
/// @author Copyright 2017, ArangoDB GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////


#include "catch.hpp"
#include "RocksDBEngine/RocksDBCommon.h"

#include <vector>
#include <limits>

using namespace arangodb;

// -----------------------------------------------------------------------------
// --SECTION--                                                        test suite
// -----------------------------------------------------------------------------

using namespace arangodb::rocksutils;
// @brief setup

void doFromToTest(double num){
  CHECK( (num == intToDouble(doubleToInt(num))) );
}

template <typename T>
void doFromToTest(T num){
  T x = num , y;
  char s[sizeof(x)] = {0};
  char* p1 = &s[0];
  char* p2 = p1;
  toPersistent(x,p1);
  y = fromPersistent<T>(p2);
  CAPTURE(x);
  CAPTURE(y);
  CHECK((x == y));
}

template <typename T>
void doFromToTestBigEndian(T num){
  std::string s;
  uintToPersistentBigEndian<T>(s,num);
  T y = uintFromPersistentBigEndian<T>(s.data());
  CAPTURE(num);
  CAPTURE(y);
  CHECK((num == y));
}

TEST_CASE("TypeConversion", "[type_conv]") {

// @brief Test fixme

SECTION("test_from_to_persist_uint64"){
  doFromToTest(std::numeric_limits<uint64_t>::min());
  doFromToTest(std::numeric_limits<uint64_t>::max()/2);
  doFromToTest(std::numeric_limits<uint64_t>::max());
}

SECTION("test_from_to_persist_uint32"){
  doFromToTest(std::numeric_limits<uint32_t>::min());
  doFromToTest(std::numeric_limits<uint32_t>::max()/2);
  doFromToTest(std::numeric_limits<uint32_t>::max());
}

SECTION("test_from_to_persist_uint16"){
  doFromToTest(std::numeric_limits<uint16_t>::min());
  doFromToTest(std::numeric_limits<uint16_t>::max()/2);
  doFromToTest(std::numeric_limits<uint16_t>::max());
}

SECTION("test_from_to_persist_int64"){
  doFromToTest(std::numeric_limits<int64_t>::min());
  doFromToTest(std::numeric_limits<int64_t>::lowest());
  doFromToTest(std::numeric_limits<int64_t>::max()/2);
  doFromToTest(std::numeric_limits<int64_t>::max());
}

SECTION("test_from_to_persist_int32"){
  doFromToTest(std::numeric_limits<int32_t>::min());
  doFromToTest(std::numeric_limits<int32_t>::lowest());
  doFromToTest(std::numeric_limits<int32_t>::max()/2);
  doFromToTest(std::numeric_limits<int32_t>::max());
}

SECTION("test_from_to_persist_int32"){
  doFromToTest(std::numeric_limits<int16_t>::min());
  doFromToTest(std::numeric_limits<int16_t>::lowest());
  doFromToTest(std::numeric_limits<int16_t>::max()/2);
  doFromToTest(std::numeric_limits<int16_t>::max());
}

SECTION("test_from_to_persist_big_endian_uint64"){
  doFromToTestBigEndian(static_cast<uint64_t>(0x0000000000000000llu));
  doFromToTestBigEndian(static_cast<uint64_t>(0x000000000000FF00llu));
  doFromToTestBigEndian(static_cast<uint64_t>(0x0000000000FF0000llu));
  doFromToTestBigEndian(static_cast<uint64_t>(0x00000000FF000000llu));
  doFromToTestBigEndian(static_cast<uint64_t>(0x000000FF00000000llu));
  doFromToTestBigEndian(static_cast<uint64_t>(0x0000FF0000000000llu));
  doFromToTestBigEndian(static_cast<uint64_t>(0x00FF000000000000llu));
  doFromToTestBigEndian(static_cast<uint64_t>(0xFF00000000000000llu));
}

// @brief generate tests
SECTION("test_from_to_double"){
  doFromToTest(std::numeric_limits<double>::min());
  doFromToTest(std::numeric_limits<double>::lowest());
  doFromToTest(std::numeric_limits<double>::max()/2);
  doFromToTest(std::numeric_limits<double>::max());
}
}
