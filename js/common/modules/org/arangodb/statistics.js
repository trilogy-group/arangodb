/*jslint indent: 2, nomen: true, maxlen: 120, white: true, plusplus: true, eqeq: true, vars: true */
/*global require, exports, ArangoServerState */

////////////////////////////////////////////////////////////////////////////////
/// @brief statistics handler
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 triagens GmbH, Cologne, Germany
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
/// @author Dr. Frank Celler, Lucas Dohmen
/// @author Copyright 2014, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var internal = require("internal");
var cluster = require("org/arangodb/cluster");

var db = internal.db;

// -----------------------------------------------------------------------------
// --SECTION--                                  module "org/arangodb/statistics"
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief cluster id or undefined for standalone
////////////////////////////////////////////////////////////////////////////////

var clusterId;

if (cluster.isCluster()) {
  clusterId = ArangoServerState.id();
}
  
// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief lastEntry
////////////////////////////////////////////////////////////////////////////////

function lastEntry (collection, start) {
  'use strict';

  var filter = "";

  if (clusterId !== undefined) {
    filter = " FILTER s.clusterId == @clusterId ";
  }

  var values = db._query(
      "FOR s in @@collection "
    + "  FILTER s.time >= @start "
    +    filter
    + "  SORT s.time desc "
    + "  RETURN s",
    { start: start, '@collection': collection, clusterId: clusterId });

  if (values.hasNext()) {
    return values.next();
  }

  return null;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief avgPercentDistributon
////////////////////////////////////////////////////////////////////////////////

function avgPercentDistributon (now, last, cuts) {
  'use strict';

  var n = cuts.length + 1;
  var result = new Array(n);
  var count = 0;
  var i;

  if (last === null) {
    count = now.count;
  }
  else {
    count = now.count - last.count;
  }

  if (count === 0) {
    for (i = 0;  i < n;  ++i) {
      result[i] = 0;
    }
  }

  else if (last === null) {
    for (i = 0;  i < n;  ++i) {
      result[i] = now.counts[i] / count;
    }
  }

  else  {
    for (i = 0;  i < n;  ++i) {
      result[i] = (now.counts[i] - last.counts[i]) / count;
    }
  }

  return { values: result, cuts: cuts };
}

////////////////////////////////////////////////////////////////////////////////
/// @brief computes the figures on a per second basis
////////////////////////////////////////////////////////////////////////////////

function computePerSeconds (current, prev) {
  'use strict';
  
  // sanity check if we have restarted the server
  if (prev.time + exports.STATISTICS_INTERVALL * 1.5 < current.time) {
    return null;
  }

  if (prev.server.uptime > current.server.uptime) {
    return null;
  }

  // compute differences and average per second
  var dt = current.time - prev.time;

  if (dt <= 0) {
    return null;
  }

  var result = { time: current.time };

  // system part
  result.system = {};

  result.system.minorPageFaultsPerSecond = (current.system.minorPageFaults - prev.system.minorPageFaults) / dt;
  result.system.majorPageFaultsPerSecond = (current.system.majorPageFaults - prev.system.majorPageFaults) / dt;
  result.system.userTimePerSecond = (current.system.userTime - prev.system.userTime) / dt;
  result.system.systemTimePerSecond = (current.system.systemTime - prev.system.systemTime) / dt;
  result.system.residentSize = current.system.residentSize;
  result.system.residentSizePercent = current.system.residentSizePercent;
  result.system.virtualSize = current.system.virtualSize;
  result.system.numberOfThreads = current.system.numberOfThreads;

  // http part
  result.http = {};

  result.http.requestsTotalPerSecond = (current.http.requestsTotal - prev.http.requestsTotal) / dt;
  result.http.requestsAsyncPerSecond = (current.http.requestsAsync - prev.http.requestsAsync) / dt;
  result.http.requestsGetPerSecond = (current.http.requestsGet - prev.http.requestsGet) / dt;
  result.http.requestsHeadPerSecond = (current.http.requestsHead - prev.http.requestsHead) / dt;
  result.http.requestsPostPerSecond = (current.http.requestsPost - prev.http.requestsPost) / dt;
  result.http.requestsPutPerSecond = (current.http.requestsPut - prev.http.requestsPut) / dt;
  result.http.requestsPatchPerSecond = (current.http.requestsPatch - prev.http.requestsPatch) / dt;
  result.http.requestsDeletePerSecond = (current.http.requestsDelete - prev.http.requestsDelete) / dt;
  result.http.requestsOptionsPerSecond = (current.http.requestsOptions - prev.http.requestsOptions) / dt;
  result.http.requestsOtherPerSecond = (current.http.requestsOther - prev.http.requestsOther) / dt;

  // client part
  result.client = {};

  result.client.httpConnections = current.client.httpConnections;

  // bytes send
  result.client.bytesSentPerSecond = (current.client.bytesSent.sum - prev.client.bytesSent.sum) / dt;

  result.client.bytesSentPercent = avgPercentDistributon(
    current.client.bytesSent,
    prev.client.bytesSent,
    internal.bytesSentDistribution);

  // bytes received
  result.client.bytesReceivedPerSecond = (current.client.bytesReceived.sum - prev.client.bytesReceived.sum) / dt;

  result.client.bytesReceivedPercent = avgPercentDistributon(
    current.client.bytesReceived,
    prev.client.bytesReceived,
    internal.bytesReceivedDistribution);

  // total time
  var d1 = current.client.totalTime.count - prev.client.totalTime.count;

  if (d1 === 0) {
    result.client.avgTotalTime = 0;
  }
  else {
    result.client.avgTotalTime = (current.client.totalTime.sum - prev.client.totalTime.sum) / d1;
  }

  result.client.totalTimePercent = avgPercentDistributon(
    current.client.totalTime,
    prev.client.totalTime,
    internal.requestTimeDistribution);

  // request time
  d1 = current.client.requestTime.count - prev.client.requestTime.count;

  if (d1 === 0) {
    result.client.avgRequestTime = 0;
  }
  else {
    result.client.avgRequestTime = (current.client.requestTime.sum - prev.client.requestTime.sum) / d1;
  }

  result.client.requestTimePercent = avgPercentDistributon(
    current.client.requestTime,
    prev.client.requestTime,
    internal.requestTimeDistribution);

  // queue time
  d1 = current.client.queueTime.count - prev.client.queueTime.count;

  if (d1 === 0) {
    result.client.avgQueueTime = 0;
  }
  else {
    result.client.avgQueueTime = (current.client.queueTime.sum - prev.client.queueTime.sum) / d1;
  }

  result.client.queueTimePercent = avgPercentDistributon(
    current.client.queueTime,
    prev.client.queueTime,
    internal.requestTimeDistribution);

  // io time
  result.client.avgIoTime
    = result.client.avgTotalTime - result.client.avgRequestTime - result.client.avgQueueTime;

  return result;
}
  
////////////////////////////////////////////////////////////////////////////////
/// @brief computes the 15 minute averages
////////////////////////////////////////////////////////////////////////////////

function compute15Minute (start) {
  'use strict';

  var filter = "";

  if (clusterId !== undefined) {
    filter = " FILTER s.clusterId == @clusterId ";
  }

  var values = db._query(
      "FOR s in _statistics "
    + "  FILTER s.time >= @start "
    +    filter
    + "  SORT s.time "
    + "  RETURN s",
    { start: start, clusterId: clusterId });

  var result = {};
  var count = 0;

  result.system = {
    minorPageFaultsPerSecond: 0,
    majorPageFaultsPerSecond: 0,
    userTimePerSecond: 0,
    systemTimePerSecond: 0,
    residentSize: 0,
    virtualSize: 0,
    numberOfThreads: 0
  };

  result.http = {
    requestsTotalPerSecond: 0,
    requestsAsyncPerSecond: 0,
    requestsGetPerSecond: 0,
    requestsHeadPerSecond: 0,
    requestsPostPerSecond: 0,
    requestsPutPerSecond: 0,
    requestsPatchPerSecond: 0,
    requestsDeletePerSecond: 0,
    requestsOptionsPerSecond: 0,
    requestsOtherPerSecond: 0
  };

  result.client = {
    httpConnections: 0,
    bytesSentPerSecond: 0, 
    bytesReceivedPerSecond: 0, 
    avgTotalTime: 0, 
    avgRequestTime: 0, 
    avgQueueTime: 0, 
    avgIoTime: 0 
  };

  while (values.hasNext()) {
    var raw = values.next();

    result.time = raw.time;

    result.system.minorPageFaultsPerSecond += raw.system.minorPageFaultsPerSecond;
    result.system.majorPageFaultsPerSecond += raw.system.majorPageFaultsPerSecond;
    result.system.userTimePerSecond += raw.system.userTimePerSecond;
    result.system.systemTimePerSecond += raw.system.systemTimePerSecond;
    result.system.residentSize += raw.system.residentSize;
    result.system.virtualSize += raw.system.virtualSize;
    result.system.numberOfThreads += raw.system.numberOfThreads;

    result.http.requestsTotalPerSecond += raw.http.requestsTotalPerSecond;
    result.http.requestsAsyncPerSecond += raw.http.requestsAsyncPerSecond;
    result.http.requestsGetPerSecond += raw.http.requestsGetPerSecond;
    result.http.requestsHeadPerSecond += raw.http.requestsHeadPerSecond;
    result.http.requestsPostPerSecond += raw.http.requestsPostPerSecond;
    result.http.requestsPutPerSecond += raw.http.requestsPutPerSecond;
    result.http.requestsPatchPerSecond += raw.http.requestsPatchPerSecond;
    result.http.requestsDeletePerSecond += raw.http.requestsDeletePerSecond;
    result.http.requestsOptionsPerSecond += raw.http.requestsOptionsPerSecond;
    result.http.requestsOtherPerSecond += raw.http.requestsOtherPerSecond;

    result.client.httpConnections += raw.client.httpConnections;
    result.client.bytesSentPerSecond += raw.client.bytesSentPerSecond;
    result.client.bytesReceivedPerSecond += raw.client.bytesReceivedPerSecond;
    result.client.avgTotalTime += raw.client.avgTotalTime;
    result.client.avgRequestTime += raw.client.avgRequestTime;
    result.client.avgQueueTime += raw.client.avgQueueTime;
    result.client.avgIoTime += raw.client.avgIoTime;

    count++;
  }

  if (count !== 0) {
    result.system.minorPageFaultsPerSecond /= count;
    result.system.majorPageFaultsPerSecond /= count;
    result.system.userTimePerSecond /= count;
    result.system.systemTimePerSecond /= count;
    result.system.residentSize /= count;
    result.system.virtualSize /= count;
    result.system.numberOfThreads /= count;

    result.http.requestsTotalPerSecond /= count;
    result.http.requestsAsyncPerSecond /= count;
    result.http.requestsGetPerSecond /= count;
    result.http.requestsHeadPerSecond /= count;
    result.http.requestsPostPerSecond /= count;
    result.http.requestsPutPerSecond /= count;
    result.http.requestsPatchPerSecond /= count;
    result.http.requestsDeletePerSecond /= count;
    result.http.requestsOptionsPerSecond /= count;
    result.http.requestsOtherPerSecond /= count;

    result.client.httpConnections /= count;
  }

  return result;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                  public variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief statistics intervall
////////////////////////////////////////////////////////////////////////////////

exports.STATISTICS_INTERVALL = 10;
  
////////////////////////////////////////////////////////////////////////////////
/// @brief statistics intervall for history
////////////////////////////////////////////////////////////////////////////////

exports.STATISTICS_HISTORY_INTERVALL = 15 * 60;
  
// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a statistics entry
////////////////////////////////////////////////////////////////////////////////
  
exports.historian = function () {
  "use strict";

  var statsRaw = db._statisticsRaw;
  var statsCol = db._statistics;

  try {
    var now = internal.time();
    var prevRaw = lastEntry('_statisticsRaw', now - 2 * exports.STATISTICS_INTERVALL);

    // create new raw statistics
    var raw = {};

    raw.time = now;
    raw.system = internal.processStatistics();
    raw.client = internal.clientStatistics();
    raw.http = internal.httpStatistics();
    raw.server = internal.serverStatistics();

    if (clusterId !== undefined) {
      raw.clusterId = clusterId;
    }

    statsRaw.save(raw);

    // create the per-seconds statistics
    if (prevRaw !== null) {
      var perSecs = computePerSeconds(raw, prevRaw);

      if (perSecs !== null) {
        if (clusterId !== undefined) {
          statsCol.clusterId = clusterId;
        }

        statsCol.save(perSecs);
      }
    }
  }
  catch (err) {
    // require("console").warn("catch error in historian: %s", err);
  }
};

////////////////////////////////////////////////////////////////////////////////
/// @brief creates an average entry
////////////////////////////////////////////////////////////////////////////////
  
exports.historianAverage = function () {
  "use strict";

  var stats15m = db._statistics15;

  try {
    var now = internal.time();

    // check if need to create a new 15 min intervall
    var prev15 = lastEntry('_statistics15', now - 2 * exports.STATISTICS_HISTORY_INTERVALL);
    var stat15;

    if (prev15 === null) {
      stat15 = compute15Minute(now - exports.STATISTICS_HISTORY_INTERVALL);
    }
    else {
      stat15 = compute15Minute(prev15.time);
    }

    if (stat15 !== undefined) {
      if (clusterId !== undefined) {
        stat15.clusterId = clusterId;
      }

      stats15m.save(stat15);
    }
  }
  catch (err) {
    // require("console").warn("catch error in historianAverage: %s", err);
  }
};

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\|/\\*jslint"
// End:
