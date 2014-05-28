/*
  Copyright (c) 2014 DataStax

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

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

#include <arpa/inet.h>

#include "cassandra.h"

void print_error(CassFuture* future) {
  CassString message = cass_future_error_message(future);
  fprintf(stderr, "Error: %.*s\n", (int)message.length, message.data);
}

CassError connect_session(const char* contact_points[], CassSession** output) {
  CassError rc = 0;
  const char** cp = NULL;
  CassFuture* future = NULL;
  CassSession* session = cass_session_new();

  *output = NULL;

  for(cp = contact_points; *cp; cp++) {
    cass_session_setopt(session, CASS_OPTION_CONTACT_POINT_ADD, *cp, strlen(*cp));
  }

  future = cass_session_connect(session);
  cass_future_wait(future);
  rc = cass_future_error_code(future);
  if(rc != CASS_OK) {
    print_error(future);
    cass_session_free(session);
  } else {
    *output = session;
  }
  cass_future_free(future);

  return rc;
}

CassError execute_query(CassSession* session, const char* query) {
  CassError rc = 0;
  CassFuture* future = NULL;
  CassStatement* statement = cass_statement_new(cass_string_init(query), 0, CASS_CONSISTENCY_ONE);

  cass_session_execute(session, statement, &future);
  cass_future_wait(future);

  rc = cass_future_error_code(future);
  if(rc != CASS_OK) {
    print_error(future);
  }

  cass_future_free(future);
  cass_statement_free(statement);

  return rc;
}

CassError insert_into_collections(CassSession* session, const char* key, const char* items[]) {
  CassError rc = 0;
  CassStatement* statement = NULL;
  CassFuture* future = NULL;
  CassCollection* collection = NULL;
  const char** item = NULL;
  CassString query = cass_string_init("INSERT INTO examples.collections (key, items) VALUES (?, ?);");

  statement = cass_statement_new(query, 2, CASS_CONSISTENCY_ONE);

  cass_statement_bind_string(statement, 0, cass_string_init(key));

  collection = cass_collection_new(2);
  for(item = items; *item; item++) {
    cass_collection_append_string(collection, cass_string_init(*item));
  }
  cass_statement_bind_collection(statement, 1, collection, cass_false);
  cass_collection_free(collection);

  cass_session_execute(session, statement, &future);

  cass_future_wait(future);

  rc = cass_future_error_code(future);
  if(rc != CASS_OK) {
    print_error(future);
  }

  cass_future_free(future);
  cass_statement_free(statement);

  return rc;
}

CassError select_from_collections(CassSession* session, const char* key) {
  CassError rc = 0;
  CassStatement* statement = NULL;
  CassFuture* future = NULL;
  CassString query = cass_string_init("SELECT items FROM examples.collections WHERE key = ?");

  statement = cass_statement_new(query, 1, CASS_CONSISTENCY_ONE);

  cass_statement_bind_string(statement, 0, cass_string_init(key));

  cass_session_execute(session, statement, &future);

  cass_future_wait(future);

  rc = cass_future_error_code(future);
  if(rc != CASS_OK) {
    print_error(future);
  } else {
    const CassResult* result = cass_future_get_result(future);
    CassIterator* iterator = cass_iterator_from_result(result);

    if(cass_iterator_next(iterator)) {
      const CassValue* value = NULL;
      const CassRow* row = cass_iterator_get_row(iterator);
      CassIterator* items_iterator = NULL;

      value = cass_row_get_column(row, 0);
      items_iterator = cass_iterator_from_collection(value);
      while(cass_iterator_next(items_iterator)) {
        CassString item_string;
        cass_value_get_string(cass_iterator_get_value(items_iterator), &item_string);
        printf("item: %.*s\n", (int)item_string.length, item_string.data);
      }
      cass_iterator_free(items_iterator);
    }

    cass_result_free(result);
    cass_iterator_free(iterator);
  }

  cass_future_free(future);
  cass_statement_free(statement);

  return rc;
}

int main() {
  CassError rc = 0;
  CassSession* session = NULL;
  CassFuture* shutdown_future = NULL;
  const char* contact_points[] = { "127.0.0.1", "127.0.0.2", "127.0.0.3", NULL };
  const char* items[] = { "apple", "orange", "banana", "mango", NULL };

  rc = connect_session(contact_points, &session);
  if(rc != CASS_OK) {
    return -1;
  }

  execute_query(session,
                "CREATE KEYSPACE examples WITH replication = { \
                           'class': 'SimpleStrategy', 'replication_factor': '1' };");

  execute_query(session,
                "CREATE TABLE examples.collections (key text, \
                                                    items set<text>, \
                                                    PRIMARY KEY (key))");


  insert_into_collections(session, "test", items);
  select_from_collections(session, "test");

  shutdown_future = cass_session_shutdown(session);
  cass_future_wait(shutdown_future);
  cass_session_free(session);

  return 0;
}