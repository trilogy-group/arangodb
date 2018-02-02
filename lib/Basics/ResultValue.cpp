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
////////////////////////////////////////////////////////////////////////////////

#include "Basics/ResultValue.h"

namespace arangodb {
namespace basics {

/* NOTE: All parameter types T for which we explicitly instantiate
 * ResultValue<T> must be
 *  - default constructible,
 *  - copy constructible,
 *  - move constructible,
 *  - copy assignable,
 *  - and move assignable.
 * This rules out, among others, any reference types. If any of these properties
 * do not hold, or the instantiation will not be reused, simply fall back to
 * implicit instantiation.
 */

template class ResultValue<int>;
template class ResultValue<int*>;

template class ResultValue<std::string>;

}  // namespace basics
}  // namespace arangodb
