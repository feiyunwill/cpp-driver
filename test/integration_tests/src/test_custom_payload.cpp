/*
  Copyright (c) 2014-2015 DataStax

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifdef STAND_ALONE
#   define BOOST_TEST_MODULE cassandra
#endif

#include <algorithm>

#if !defined(WIN32) && !defined(_WIN32)
#include <signal.h>
#endif

#include "cassandra.h"
#include "testing.hpp"
#include "test_utils.hpp"
#include "cql_ccm_bridge.hpp"

#include <boost/test/unit_test.hpp>
#include <boost/test/debug.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/cstdint.hpp>
#include <boost/format.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/thread/future.hpp>
#include <boost/bind.hpp>
#include <boost/chrono.hpp>
#include <boost/cstdint.hpp>

#include <map>
#include <set>
#include <string>

BOOST_AUTO_TEST_SUITE(custom_payload)

BOOST_AUTO_TEST_CASE(simple)
{
  test_utils::CassClusterPtr cluster(cass_cluster_new());

  const cql::cql_ccm_bridge_configuration_t& conf = cql::get_ccm_bridge_configuration();
  boost::shared_ptr<cql::cql_ccm_bridge_t> ccm = cql::cql_ccm_bridge_t::create(conf, "test");

  ccm->add_node(1);
  ccm->start(1, "-Dcassandra.custom_query_handler_class=org.apache.cassandra.cql3.CustomPayloadMirroringQueryHandler");

  CassVersion version = ccm->version();
  BOOST_REQUIRE((version.major >= 2 && version.minor >= 2) || version.major >= 3);

  test_utils::initialize_contact_points(cluster.get(), conf.ip_prefix(), 1, 0);

  test_utils::CassSessionPtr session(test_utils::create_session(cluster.get()));

  test_utils::CassStatementPtr statement(cass_statement_new("SELECT * FROM system.local", 0));

  test_utils::CassCustomPayloadPtr custom_payload(cass_custom_payload_new());

  typedef std::map<std::string, std::string> PayloadItemMap;

  PayloadItemMap items;
  items["key1"] = "value1";
  items["key2"] = "value2";
  items["key3"] = "value3";

  for (PayloadItemMap::const_iterator i = items.begin(); i != items.end(); ++i) {
    cass_custom_payload_set(custom_payload.get(),
                            i->first.c_str(),
                            reinterpret_cast<const cass_byte_t*>(i->second.data()), i->second.size());
  }

  cass_statement_set_custom_payload(statement.get(), custom_payload.get());

  test_utils::CassFuturePtr future(cass_session_execute(session.get(), statement.get()));

  size_t item_count = cass_future_custom_payload_item_count(future.get());

  BOOST_REQUIRE(item_count == items.size());

  for (size_t i = 0; i < item_count; ++i) {
    const char* name;
    size_t name_length;
    const cass_byte_t* value;
    size_t value_size;
    BOOST_REQUIRE(cass_future_custom_payload_item(future.get(), i,
                                                  &name, &name_length,
                                                  &value, &value_size) == CASS_OK);

    PayloadItemMap::const_iterator it = items.find(std::string(name, name_length));
    BOOST_REQUIRE(it != items.end());
    BOOST_CHECK_EQUAL(it->second, std::string(reinterpret_cast<const char*>(value), value_size));
  }
}

BOOST_AUTO_TEST_SUITE_END()
