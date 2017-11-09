/*jshint globalstrict:false, strict:false, maxlen: 500 */
/*global assertEqual, assertFalse, assertTrue, assertNotEqual, AQL_EXPLAIN, AQL_EXECUTE, print */

// execute with:
// ./scripts/unittest shell_server --test js/server/tests/shell/shell-cluster-shard-merge.js

////////////////////////////////////////////////////////////////////////////////
/// @brief tests that tests if collecting results from multiple
///        shards works correctly
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2012 triagens GmbH, Cologne, Germany
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
/// @author Jan Christoph Uhde
/// @author Copyright 2017, ArangoDB GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

const expect = require('chai').expect;
var internal = require("internal");
var jsunity = require("jsunity");
var helper = require("@arangodb/aql-helper");
var isEqual = helper.isEqual;
var findExecutionNodes = helper.findExecutionNodes;
var findReferencedNodes = helper.findReferencedNodes;
var getQueryMultiplePlansAndExecutions = helper.getQueryMultiplePlansAndExecutions;
var removeAlwaysOnClusterRules = helper.removeAlwaysOnClusterRules;
var db = require('internal').db;
var debug = false;

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite
////////////////////////////////////////////////////////////////////////////////

function optimizerRuleTestSuite() {
  // quickly disable tests here
  var enabled = {
    basics : true,
  };

  var ruleName = "shardmerge";
  var colName1 = "UnitTestsShellServer" + ruleName.replace(/-/g, "_");
  var colName2 = "UnitTestsShellServer" + ruleName.replace(/-/g, "_") + "2";


  var collection1;
  var collection2;

        //distances.forEach(d => { assertTrue( d >= old); old = d; });

  return {

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief set up
    ////////////////////////////////////////////////////////////////////////////////

    setUp : function () {
      internal.db._drop(colName1);
      internal.db._drop(colName2);
      collection1 = internal.db._create(colName1, { numberOfShards : 3 });
      collection2 = internal.db._create(colName2, { numberOfShards : 9 });
      assertEqual(collection1.properties()['numberOfShards'], 3);
      assertEqual(collection2.properties()['numberOfShards'], 9);

      let batchSize=1000;
      for (var i = 0; i < 4; i++) {
          var insertArray = [];
          for (var j = 0; j < batchSize; j++) {
              let value = i * batchSize + j;
              insertArray.push({ "value" : value });
          }
          collection1.insert(insertArray);
          collection2.insert(insertArray);
      }
    },

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief tear down
    ////////////////////////////////////////////////////////////////////////////////

    tearDown : function () {
      internal.db._drop(colName1);
      internal.db._drop(colName2);
    },

    testRuleBasics : function () {
      if(enabled.basics){
        let query1 = 'for doc in @@col FILTER doc.value >= 2000 return doc';
        let query2 = `LET queryOne = ( FOR doc in @@col
                                          FILTER doc.value >= 2000
                                          COLLECT WITH COUNT INTO length return length
                                     )
                      RETURN { ac: queryOne }
                     `;
        let query3 = 'for doc in @@col SORT doc.value return doc';

        //q(int) query 
        //s(int) shards
        //i(bool) index
        let tests = [
          //{ test : "q1s3if", query: query1, collection: colName1, gathernodes: 1, sortmode: 'minelement' , count: 2000 , sorted: false },
          //{ test : "q1s9if", query: query1, collection: colName2, gathernodes: 1, sortmode: 'heap'       , count: 2000 , sorted: false },
          //{ test : "q2s3if", query: query2, collection: colName1, gathernodes: 1, sortmode: 'minelement' , count: 1    , sorted: false },
          //{ test : "q2s9if", query: query2, collection: colName2, gathernodes: 1, sortmode: 'heap'       , count: 1    , sorted: false },
          { test : "q3s3if", query: query3, collection: colName1, gathernodes: 1, sortmode: 'minelement' , count: 4000 , sorted: true },
          { test : "q3s9if", query: query3, collection: colName2, gathernodes: 1, sortmode: 'heap'       , count: 4000 , sorted: true }
        ];

        let loop = 10;
        tests.forEach(t => {
            if(debug) {
              print(t);
              db._explain(t.query , { '@col' : t.collection });
            }
            let plan = AQL_EXPLAIN(t.query , { '@col' : t.collection });
            let gatherNodes = findExecutionNodes(plan, "GatherNode");
            if(debug) {
              print(gatherNodes[0]);
            }
            var assertMessage;
            assertMessage = "\n##### " + "did not find gatherNode" + " #####\n";
            assertEqual(gatherNodes.length,1, assertMessage);
            assertMessage = "\n##### " + "sort mode does not match" + " #####\n";
            //assertEqual(gatherNodes[0]["sortmode"], t.sortmode, assertMessage);

            var rv;
            var time = 0;
            for(var i=0; i < loop; i++){
              let start = Date.now();
              rv = db._query(t.query , { '@col' : t.collection });
              time += (Date.now() - start);
            };

            assertEqual(rv.toArray().length, t.count);

            print(rv.toArray().map( doc => { return doc.value }));
            if(t.sorted) {
              var old = -1;
              rv.toArray().forEach(doc => {
                let d = doc.value;
                assertMessage = "\n##### " + old + " -> " + d + " #####\n";
                assertTrue( d >= old, assertMessage);
                old = d;
              });
            }

            t.result = rv.toArray();
            t.result_len = t.result.length;
            t.time = time / loop;
        });
        tests.forEach(t => { print(t.test,t.time); });
      }

    }, // testRuleBasics


  }; // test dictionary (return)
} // optimizerRuleTestSuite

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suite
////////////////////////////////////////////////////////////////////////////////

jsunity.run(optimizerRuleTestSuite);

return jsunity.done();
