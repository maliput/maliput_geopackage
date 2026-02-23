// BSD 3-Clause License
//
// Copyright (c) 2026, Woven by Toyota
// All rights reserved.

#include "maliput_geopackage/geopackage/sqlite_helpers.h"

#include <string>

#include "maliput/common/maliput_throw.h"

SqliteDatabase::SqliteDatabase(const std::string& db_path) {
  sqlite3* raw_db = nullptr;
  const int rc = sqlite3_open_v2(db_path.c_str(), &raw_db, SQLITE_OPEN_READONLY, nullptr);
  if (rc != SQLITE_OK) {
    const std::string err_msg = sqlite3_errmsg(raw_db);
    sqlite3_close(raw_db);
    MALIPUT_THROW_MESSAGE("Failed to open GeoPackage at " + db_path + ": " + err_msg);
  }
  db_ = std::unique_ptr<sqlite3, SqliteDeleter>(raw_db);
}

sqlite3* SqliteDatabase::get() const { return db_.get(); }

SqliteStatement::SqliteStatement(sqlite3* db, const std::string& query) {
  const int rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt_, nullptr);
  if (rc != SQLITE_OK) {
    const std::string err_msg = sqlite3_errmsg(db);
    MALIPUT_THROW_MESSAGE("Failed to prepare query '" + query + "': " + err_msg);
  }
}

SqliteStatement::~SqliteStatement() {
  if (stmt_) {
    sqlite3_finalize(stmt_);
  }
}

bool SqliteStatement::Step() {
  const int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) {
    return true;
  } else if (rc == SQLITE_DONE) {
    return false;
  } else {
    const std::string err_msg = sqlite3_errmsg(sqlite3_db_handle(stmt_));
    MALIPUT_THROW_MESSAGE("Failed to step query: " + err_msg);
  }
}

std::string SqliteStatement::GetColumnText(int col) {
  const unsigned char* text = sqlite3_column_text(stmt_, col);
  if (text) {
    // Get the exact length to avoid an O(N) strlen scan
    int len = sqlite3_column_bytes(stmt_, col);
    return std::string(reinterpret_cast<const char*>(text), len);
  }
  return "";
}

int SqliteStatement::GetColumnInt(int col) { return sqlite3_column_int(stmt_, col); }

const void* SqliteStatement::GetColumnBlob(int col) { return sqlite3_column_blob(stmt_, col); }

int SqliteStatement::GetColumnBytes(int col) { return sqlite3_column_bytes(stmt_, col); }
