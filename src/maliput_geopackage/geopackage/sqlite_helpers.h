// BSD 3-Clause License
//
// Copyright (c) 2026, Woven by Toyota
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
#pragma once

#include <memory>
#include <string>

#include <sqlite3.h>

#include "maliput/common/maliput_throw.h"

// Define a custom deleter for the sqlite3 smart pointer.
struct SqliteDeleter {
  void operator()(sqlite3* db) const;
};

// Define a custom deleter for the sqlite3_stmt smart pointer.
struct SqliteStatementDeleter {
  void operator()(sqlite3_stmt* stmt) const;
};

// RAII wrapper for sqlite3 database connection.
class SqliteDatabase {
 public:
  explicit SqliteDatabase(const std::string& db_path);
  ~SqliteDatabase() = default;

  sqlite3* get() const;

 private:
  std::unique_ptr<sqlite3, SqliteDeleter> db_;
};

// RAII wrapper for sqlite3 statement.
class SqliteStatement {
 public:
  SqliteStatement(sqlite3* db, const std::string& query);
  ~SqliteStatement() = default;

  /// Step to the next row of the result. Returns true if a row is available, false if the end of the result is reached.
  bool Step();

  /// Get the text value of the specified column in the current row. Returns an empty string if the column is NULL.
  std::string GetColumnText(int col);

  /// Get the integer value of the specified column in the current row. Returns 0 if the column is NULL.
  int GetColumnInt(int col);

  /// Get a pointer to the blob data of the specified column in the current row. Returns nullptr if the column is NULL.
  const void* GetColumnBlob(int col);

  /// Get the size in bytes of the blob data in the specified column. Returns 0 if the column is NULL.
  int GetColumnBytes(int col);

 private:
  /// The sqlite3_stmt is managed by a unique_ptr with a custom deleter to ensure proper cleanup.
  std::unique_ptr<sqlite3_stmt, SqliteStatementDeleter> stmt_;
};
