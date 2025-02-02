/*
 * Copyright (C) 2020-2022 Dremio Corporation
 *
 * See "LICENSE" for license information.
 */

#include <flight_sql/flight_sql_driver.h>
#include <odbcabstraction/platform.h>
#include <odbcabstraction/types.h>

#include "flight_sql_connection.h"
#include "flight_sql_result_set.h"
#include "flight_sql_result_set_metadata.h"
#include "flight_sql_statement.h"

#include <arrow/flight/api.h>
#include <iostream>

#include "spdlog/pattern_formatter-inl.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <getopt.h>
#endif


using arrow::Status;
using arrow::flight::FlightClient;
using arrow::flight::Location;
using arrow::flight::sql::FlightSqlClient;

using driver::flight_sql::FlightSqlConnection;
using driver::flight_sql::FlightSqlDriver;
using driver::odbcabstraction::Connection;
using driver::odbcabstraction::ResultSet;
using driver::odbcabstraction::ResultSetMetadata;
using driver::odbcabstraction::Statement;

Connection::ConnPropertyMap parse_connection_properties(const int argc, char *argv[]) {
  // Default values
  Connection::ConnPropertyMap properties = {
          {FlightSqlConnection::HOST, std::string("localhost")},
          {FlightSqlConnection::PORT, std::string("443")},
          {FlightSqlConnection::USER, std::string("")},
          {FlightSqlConnection::PASSWORD, std::string("")},
          {FlightSqlConnection::USE_ENCRYPTION, std::string("true")},
          {FlightSqlConnection::DISABLE_CERTIFICATE_VERIFICATION, std::string("false")},
          {"data_plane", "spark-resources"},
          {"cluster", "arrow"}};

#ifdef _WIN32
  // Simple argument parsing for Windows
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "--host" || arg == "-h") {
      if (i + 1 < argc)
        properties[FlightSqlConnection::HOST] = argv[++i];
    } else if (arg == "--port" || arg == "-p") {
      if (i + 1 < argc)
        properties[FlightSqlConnection::PORT] = argv[++i];
    } else if (arg == "--user" || arg == "-u") {
      if (i + 1 < argc)
        properties[FlightSqlConnection::USER] = argv[++i];
    } else if (arg == "--password" || arg == "-w") {
      if (i + 1 < argc)
        properties[FlightSqlConnection::PASSWORD] = argv[++i];
    } else if (arg == "--data-plane" || arg == "-d") {
      if (i + 1 < argc)
        properties["data_plane"] = argv[++i];
    } else if (arg == "--cluster" || arg == "-c") {
      if (i + 1 < argc)
        properties["cluster"] = argv[++i];
    } else if (arg == "--no-encryption" || arg == "-n") {
      properties[FlightSqlConnection::USE_ENCRYPTION] = "false";
    } else if (arg == "--disable-cert-verify" || arg == "-k") {
      properties[FlightSqlConnection::DISABLE_CERTIFICATE_VERIFICATION] = "true";
    } else {
      std::cerr << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --host, -h <host>           Flight SQL server host\n"
                << "  --port, -p <port>           Flight SQL server port\n"
                << "  --user, -u <username>       Username\n"
                << "  --password, -w <password>   Password\n"
                << "  --data-plane, -d <name>     Data plane name\n"
                << "  --cluster, -c <name>        Cluster name\n"
                << "  --no-encryption, -n         Disable encryption\n"
                << "  --disable-cert-verify, -k   Disable certificate verification\n";
      exit(1);
    }
  }
#else
  // Original getopt code for non-Windows platforms
  static struct option long_options[] = {{"host", required_argument, 0, 'h'},
                                         {"port", required_argument, 0, 'p'},
                                         {"user", required_argument, 0, 'u'},
                                         {"password", required_argument, 0, 'w'},
                                         {"data-plane", required_argument, 0, 'd'},
                                         {"cluster", required_argument, 0, 'c'},
                                         {"no-encryption", no_argument, 0, 'n'},
                                         {"disable-cert-verify", no_argument, 0, 'k'},
                                         {0, 0, 0, 0}};

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "h:p:u:w:d:c:nk", long_options, &option_index)) != -1) {
    switch (opt) {
      case 'h':
        properties[FlightSqlConnection::HOST] = std::string(optarg);
        break;
      case 'p':
        properties[FlightSqlConnection::PORT] = std::string(optarg);
        break;
      case 'u':
        properties[FlightSqlConnection::USER] = std::string(optarg);
        break;
      case 'w':
        properties[FlightSqlConnection::PASSWORD] = std::string(optarg);
        break;
      case 'd':
        properties["data_plane"] = std::string(optarg);
        break;
      case 'c':
        properties["cluster"] = std::string(optarg);
        break;
      case 'n':
        properties[FlightSqlConnection::USE_ENCRYPTION] = std::string("false");
        break;
      case 'k':
        properties[FlightSqlConnection::DISABLE_CERTIFICATE_VERIFICATION] = std::string("true");
        break;
      default:
        std::cerr << "Usage: " << argv[0] << " [options]\n"
                  << "Options:\n"
                  << "  --host, -h <host>           Flight SQL server host\n"
                  << "  --port, -p <port>           Flight SQL server port\n"
                  << "  --user, -u <username>       Username\n"
                  << "  --password, -w <password>   Password\n"
                  << "  --data-plane, -d <name>     Data plane name\n"
                  << "  --cluster, -c <name>        Cluster name\n"
                  << "  --no-encryption, -n         Disable encryption\n"
                  << "  --disable-cert-verify, -k   Disable certificate verification\n";
        exit(1);
    }
  }
#endif

  return properties;
}


void TestBindColumn(const std::shared_ptr<Connection> &connection) {
  const std::shared_ptr<Statement> &statement = connection->CreateStatement();
  statement->Execute("SELECT IncidntNum, Category FROM \"@dremio\".Test LIMIT 10");

  const std::shared_ptr<ResultSet> &result_set = statement->GetResultSet();

  const int batch_size = 100;
  const int max_strlen = 1000;

  char IncidntNum[batch_size][max_strlen];
  ssize_t IncidntNum_length[batch_size];

  char Category[batch_size][max_strlen];
  ssize_t Category_length[batch_size];

  result_set->BindColumn(1, driver::odbcabstraction::CDataType_CHAR, 0, 0, IncidntNum, max_strlen, IncidntNum_length);
  result_set->BindColumn(2, driver::odbcabstraction::CDataType_CHAR, 0, 0, Category, max_strlen, Category_length);

  size_t total = 0;
  while (true) {
    size_t fetched_rows = result_set->Move(batch_size, 0, 0, nullptr);
    std::cout << "Fetched " << fetched_rows << " rows." << std::endl;

    total += fetched_rows;
    std::cout << "Total:" << total << std::endl;

    for (int i = 0; i < fetched_rows; ++i) {
      std::cout << "Row[" << i << "] IncidntNum: '" << IncidntNum[i] << "', Category: '" << Category[i] << "'"
                << std::endl;
    }

    if (fetched_rows < batch_size)
      break;
  }
}

void TestGetData(const std::shared_ptr<Connection> &connection) {
  const std::shared_ptr<Statement> &statement = connection->CreateStatement();
  statement->Execute(
          "SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5 UNION ALL SELECT 6");

  const std::shared_ptr<ResultSet> &result_set = statement->GetResultSet();
  const std::shared_ptr<ResultSetMetadata> &metadata = result_set->GetMetadata();

  while (result_set->Move(1, 0, 0, nullptr) == 1) {
    char result[128];
    ssize_t result_length;
    result_set->GetData(1, driver::odbcabstraction::CDataType_CHAR, 0, 0, &result, sizeof(result), &result_length);
    std::cout << result << std::endl;
  }
}

void TestBindColumnBigInt(const std::shared_ptr<Connection> &connection) {
  const std::shared_ptr<Statement> &statement = connection->CreateStatement();
  statement->Execute("SELECT IncidntNum, CAST(\"IncidntNum\" AS DOUBLE) / 100 AS "
                     "double_field, Category\n"
                     "FROM (\n"
                     "  SELECT CONVERT_TO_INTEGER(IncidntNum, 1, 1, 0) AS IncidntNum, "
                     "Category\n"
                     "  FROM (\n"
                     "    SELECT IncidntNum, Category FROM \"@dremio\".Test LIMIT 10\n"
                     "  ) nested_0\n"
                     ") nested_0");

  const std::shared_ptr<ResultSet> &result_set = statement->GetResultSet();

  const int batch_size = 100;
  const int max_strlen = 1000;

  char IncidntNum[batch_size][max_strlen];
  ssize_t IncidntNum_length[batch_size];

  double double_field[batch_size];
  ssize_t double_field_length[batch_size];

  char Category[batch_size][max_strlen];
  ssize_t Category_length[batch_size];

  result_set->BindColumn(1, driver::odbcabstraction::CDataType_CHAR, 0, 0, IncidntNum, max_strlen, IncidntNum_length);
  result_set->BindColumn(2, driver::odbcabstraction::CDataType_DOUBLE, 0, 0, double_field, max_strlen,
                         double_field_length);
  result_set->BindColumn(3, driver::odbcabstraction::CDataType_CHAR, 0, 0, Category, max_strlen, Category_length);

  size_t total = 0;
  while (true) {
    size_t fetched_rows = result_set->Move(batch_size, 0, 0, nullptr);
    std::cout << "Fetched " << fetched_rows << " rows." << std::endl;

    total += fetched_rows;
    std::cout << "Total:" << total << std::endl;

    for (int i = 0; i < fetched_rows; ++i) {
      std::cout << "Row[" << i << "] IncidntNum: '" << IncidntNum[i] << "', "
                << "double_field: '" << double_field[i] << "', "
                << "Category: '" << Category[i] << "'" << std::endl;
    }

    if (fetched_rows < batch_size)
      break;
  }
}

void TestInitialGetTablesCall(const std::shared_ptr<Connection> &connection) {
  const std::shared_ptr<Statement> &statement = connection->CreateStatement();
  const std::string catalog_name = "%";
  const std::shared_ptr<ResultSet> &result_set = statement->GetTables_V3(&catalog_name, nullptr, nullptr, nullptr);

  std::cout << "Initial call simulation - Catalog.Schema.Table" << std::endl;

  while (result_set->Move(1, 0, 0, nullptr) == 1) {
    constexpr int buffer_length = 1024;
    std::vector<char> result(buffer_length);
    ssize_t result_length;

    result_set->GetData(1, driver::odbcabstraction::CDataType_CHAR, 0, 0, result.data(), buffer_length, &result_length);
    const auto catalog = std::string(result.data());

    result_set->GetData(2, driver::odbcabstraction::CDataType_CHAR, 0, 0, result.data(), buffer_length, &result_length);
    const auto schema = std::string(result.data());

    result_set->GetData(3, driver::odbcabstraction::CDataType_CHAR, 0, 0, result.data(), buffer_length, &result_length);
    const auto table = std::string(result.data());

    if (!catalog.empty() && !schema.empty() && !table.empty()) {
      std::cout << catalog + "." + schema + "." + table << std::endl;
    }
  }
}

void TestGetTablesV3(const std::shared_ptr<Connection> &connection) {
  const std::shared_ptr<Statement> &statement = connection->CreateStatement();
  const std::string catalog_name = "%";
  const std::string schema_name = "IOMETE_USER";
  const std::string table_name = "%";
  const std::shared_ptr<ResultSet> &result_set =
          statement->GetTables_V3(&catalog_name, &schema_name, &table_name, nullptr);

  std::cout << "Schema call - Catalog.Schema.Table" << std::endl;

  while (result_set->Move(1, 0, 0, nullptr) == 1) {
    constexpr int buffer_length = 1024;
    std::vector<char> result(buffer_length);
    ssize_t result_length;

    result_set->GetData(1, driver::odbcabstraction::CDataType_CHAR, 0, 0, result.data(), buffer_length, &result_length);
    const auto catalog = std::string(result.data());

    result_set->GetData(2, driver::odbcabstraction::CDataType_CHAR, 0, 0, result.data(), buffer_length, &result_length);
    const auto schema = std::string(result.data());

    result_set->GetData(3, driver::odbcabstraction::CDataType_CHAR, 0, 0, result.data(), buffer_length, &result_length);
    const auto table = std::string(result.data());

    if (!catalog.empty() && !schema.empty() && !table.empty()) {
      std::cout << catalog + "." + schema + "." + table << std::endl;
    }
  }
}

void TestGetColumnsV3(const std::shared_ptr<Connection> &connection) {
  const std::shared_ptr<Statement> &statement = connection->CreateStatement();
  const std::string catalog_name = "%";
  const std::string schema_name = "IOMETE_USER";
  const std::string table_name = "ALTAY_TEST";
  const std::string column_name = "%";
  const std::shared_ptr<ResultSet> &result_set = statement->GetColumns_V3(&catalog_name, &schema_name, &table_name, &column_name);

  const std::shared_ptr<ResultSetMetadata> &metadata = result_set->GetMetadata();
  const size_t column_count = metadata->GetColumnCount();

  constexpr int buffer_length = 1024;
  std::vector<char> result(buffer_length);
  ssize_t result_length;

  while (result_set->Move(1, 0, 0, nullptr) == 1) {
    for (int i = 0; i < column_count; ++i) {
      result_set->GetData(1 + i, driver::odbcabstraction::CDataType_CHAR, 0, 0, result.data(), buffer_length,
                          &result_length);
      std::cout << (result_length != -1 ? result.data() : "NULL") << '\t';
    }

    std::cout << std::endl;
  }

  std::cout << column_count << std::endl;
}

int main(const int argc, char *argv[]) {
  FlightSqlDriver driver;

  const std::shared_ptr<Connection> &connection = driver.CreateConnection(driver::odbcabstraction::V_3);

  const Connection::ConnPropertyMap properties = parse_connection_properties(argc, argv);

  std::vector<std::string> missing_attr;
  connection->Connect(properties, missing_attr);

  TestInitialGetTablesCall(connection);
  TestGetTablesV3(connection);
  TestGetColumnsV3(connection);

  connection->Close();
  return 0;
}
