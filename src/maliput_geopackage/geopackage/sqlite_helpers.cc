// BSD 3-Clause License
//
// Copyright (c) 2026, Woven by Toyota.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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

void SqliteDeleter::operator()(sqlite3* db) const {
  if (db) {
    sqlite3_close(db);
  }
}

sqlite3* SqliteDatabase::get() const { return db_.get(); }

SqliteStatement::SqliteStatement(sqlite3* db, const std::string& query) {
  sqlite3_stmt* raw_stmt = nullptr;
  const int rc = sqlite3_prepare_v2(db, query.c_str(), -1, &raw_stmt, nullptr);
  if (rc != SQLITE_OK) {
    const std::string err_msg = sqlite3_errmsg(db);
    MALIPUT_THROW_MESSAGE("Failed to prepare query '" + query + "': " + err_msg);
  }
  stmt_ = std::unique_ptr<sqlite3_stmt, SqliteStatementDeleter>(raw_stmt);
}

void SqliteStatementDeleter::operator()(sqlite3_stmt* stmt) const {
  if (stmt) {
    sqlite3_finalize(stmt);
  }
}

bool SqliteStatement::Step() {
  const int rc = sqlite3_step(stmt_.get());
  if (rc == SQLITE_ROW) {
    return true;
  } else if (rc == SQLITE_DONE) {
    return false;
  } else {
    const std::string err_msg = sqlite3_errmsg(sqlite3_db_handle(stmt_.get()));
    MALIPUT_THROW_MESSAGE("Failed to step query: " + err_msg);
  }
}

std::string SqliteStatement::GetColumnText(int col) {
  const unsigned char* text = sqlite3_column_text(stmt_.get(), col);
  if (text) {
    int len = sqlite3_column_bytes(stmt_.get(), col);
    return std::string(reinterpret_cast<const char*>(text), len);
  }
  return "";
}

int SqliteStatement::GetColumnInt(int col) { return sqlite3_column_int(stmt_.get(), col); }

const void* SqliteStatement::GetColumnBlob(int col) { return sqlite3_column_blob(stmt_.get(), col); }

int SqliteStatement::GetColumnBytes(int col) { return sqlite3_column_bytes(stmt_.get(), col); }
