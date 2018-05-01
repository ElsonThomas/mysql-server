/* Copyright (c) 2016, 2018, Oracle and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 2.0, as published by the
Free Software Foundation.

This program is also distributed with certain software (including but not
limited to OpenSSL) that is licensed under separate terms, as designated in a
particular file or component or in included license documentation. The authors
of MySQL hereby grant you an additional permission to link the program and
your derivative works with the separately licensed software that they have
included with MySQL.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License, version 2.0,
for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/** @file storage/temptable/include/temptable/table.h
TempTable Table declarations. */

#ifndef TEMPTABLE_TABLE_H
#define TEMPTABLE_TABLE_H

#include <cstddef>       /* size_t */
#include <functional>    /* std::hash, std::equal_to */
#include <string>        /* std::string */
#include <unordered_map> /* std::unordered_map */
#include <utility>       /* std::pair, std::move */
#include <vector>        /* std::vector */

#include "sql/table.h" /* TABLE, TABLE_SHARE */
#include "storage/temptable/include/temptable/allocator.h" /* temptable::Allocator */
#include "storage/temptable/include/temptable/column.h"  /* temptable::Column */
#include "storage/temptable/include/temptable/cursor.h"  /* temptable::Cursor */
#include "storage/temptable/include/temptable/index.h"   /* temptable::Index */
#include "storage/temptable/include/temptable/result.h"  /* temptable::Result */
#include "storage/temptable/include/temptable/row.h"     /* temptable::Row */
#include "storage/temptable/include/temptable/storage.h" /* temptable::Storage */

namespace temptable {

class Table {
 public:
  Table(TABLE *mysql_table, bool all_columns_are_fixed_size);

  /* The `m_rows` member is too expensive to copy around. */
  Table(const Table &) = delete;
  Table &operator=(const Table &) = delete;

  Table(Table &&other);
  Table &operator=(Table &&rhs);

  ~Table();

  const TABLE_SHARE *mysql_table_share() const;

  size_t mysql_row_length() const;

  size_t number_of_indexes() const;

  size_t number_of_columns() const;

  const Columns &columns() const;

  size_t number_of_rows() const;

  const Index &index(size_t i) const;

  const Column &column(size_t i) const;

  const Storage &rows() const;

  void row(const Storage::Iterator &pos, unsigned char *mysql_row) const;

  /** Insert a new row in the table. If something else than Result::OK is
   * returned, then the state of the table and its indexes is unchanged by the
   * call of this method (ie the method takes care to clean up any half
   * completed work in case of an error and restore the table to its state
   * before this method was called).
   * @return result code */
  Result insert(
      /** [in] The contents of the new row to be inserted. */
      const unsigned char *mysql_row);

  /** Update a row in the table. The semantics of this are enforced by the
   * handler API. If something else than Result::OK is returned, then the state
   * of the table and its indexes is unchanged by the call of this method (ie
   * the method takes care to clean up any half completed work in case of an
   * error and restore the table to its state before this method was called).
   * @return result code */
  Result update(
      /** [in] The contents of the old row to be updated. The row pointed to by
       * `target_row` must equal this. */
      const unsigned char *mysql_row_old,
      /** [in] The contents of the new row. */
      const unsigned char *mysql_row_new,
      /** [in,out] Position in the list of rows to update. The row pointed to
       * by this must equal `mysql_row_old`. */
      Storage::Element *target_row,
      /** [in] Indicate if this is reversing an incomplete update operation.
       * When a normal update(reversal = false) fails at some index for example
       * because the index is unique and it resulted in a duplicate, then we
       * need to undo all the actions performed on the table and its indexes in
       * order to restore to the state before the update(reversal = false) call.
       * Then we call update(reversal = true) and set `max_index` to the index
       * at which the failure occured. */
      bool reversal = false,
      /** [in] In case of reversal, the index which we failed to update. */
      size_t max_index = 0);

  Result remove(const unsigned char *mysql_row_must_be,
                const Storage::Iterator &victim_position);

  void truncate();

  Result disable_indexes();

  Result enable_indexes();

 private:
  /** Index entry for storing index pointer as well
   * as allocated memory size. */
  struct Index_entry {
    Index *m_index;

    size_t m_alloc_size;
  };

  /** Create index for given key and append it to indexes table. */
  template <class T>
  void append_new_index(const KEY &mysql_index);

  /** Create the indexes in `m_index_entries`
   * from `m_mysql_table->key_info[]`. */
  void indexes_create();

  /** Destroy the indexes in `m_index_entries`. */
  void indexes_destroy();

  /** Allocator for all members that need dynamic memory allocation. */
  Allocator<uint8_t> m_allocator;

  /** Rows of the table. */
  Storage m_rows;

  bool m_all_columns_are_fixed_size;

  bool m_indexes_are_enabled;

  uint32_t m_mysql_row_length;

  std::vector<Index_entry, Allocator<Index_entry>> m_index_entries;

  std::vector<Cursor, Allocator<Cursor>> m_insert_undo;

  Columns m_columns;

  TABLE_SHARE *m_mysql_table_share;
};

/** A container for the list of the tables. Don't allocate memory for it from
 * the Allocator because the Allocator keeps one block for reuse and it is
 * only marked for reuse after all elements from it have been removed. This
 * container, being a global variable may allocate some memory and never free
 * it before its destructor is called at thread termination time. */
typedef std::unordered_map<std::string, Table> Tables;

/** A list of the tables that currently exist for this thread. */
extern thread_local Tables tables;

/* Implementation of inlined methods. */

inline Table::Table(Table &&other) : m_rows(nullptr) {
  *this = std::move(other);
}

inline Table &Table::operator=(Table &&rhs) {
  m_rows = std::move(rhs.m_rows);

  m_all_columns_are_fixed_size = rhs.m_all_columns_are_fixed_size;
  rhs.m_all_columns_are_fixed_size = false;

  m_indexes_are_enabled = rhs.m_indexes_are_enabled;
  rhs.m_indexes_are_enabled = false;

  m_mysql_row_length = rhs.m_mysql_row_length;
  rhs.m_mysql_row_length = 0;

  indexes_destroy();

  m_index_entries = std::move(rhs.m_index_entries);

  /* No need to move `m_insert_undo[]`. It does not bring a state - it is only
   * used by the `insert()` method as a scratch-pad. */
  m_insert_undo.reserve(m_index_entries.size());

  m_columns = std::move(rhs.m_columns);

  m_mysql_table_share = rhs.m_mysql_table_share;
  rhs.m_mysql_table_share = nullptr;

  return *this;
}

inline const TABLE_SHARE *Table::mysql_table_share() const {
  return m_mysql_table_share;
}

inline size_t Table::mysql_row_length() const { return m_mysql_row_length; }

inline size_t Table::number_of_indexes() const {
  return m_index_entries.size();
}

inline size_t Table::number_of_columns() const { return m_columns.size(); }

inline const Columns &Table::columns() const { return m_columns; }

inline size_t Table::number_of_rows() const { return m_rows.size(); }

inline const Index &Table::index(size_t i) const {
  return *m_index_entries[i].m_index;
}

inline const Column &Table::column(size_t i) const {
  DBUG_ASSERT(i < m_columns.size());
  return m_columns[i];
}

inline const Storage &Table::rows() const { return m_rows; }

inline void Table::row(const Storage::Iterator &pos,
                       unsigned char *mysql_row) const {
  DBUG_ASSERT(m_mysql_row_length == m_mysql_table_share->rec_buff_length);

  const Storage::Element *storage_element = *pos;

  if (m_all_columns_are_fixed_size) {
    DBUG_ASSERT(m_rows.element_size() == m_mysql_row_length);

    memcpy(mysql_row, storage_element, m_mysql_row_length);
  } else {
    DBUG_ASSERT(m_rows.element_size() == sizeof(Row));

    const Row *row = static_cast<const Row *>(storage_element);
    row->copy_to_mysql_row(m_columns, mysql_row, m_mysql_row_length);
  }
}

inline void Table::truncate() {
  if (!m_all_columns_are_fixed_size) {
    for (auto element : m_rows) {
      Row *row = static_cast<Row *>(element);
      row->~Row();
    }
  }
  m_rows.clear();

  /* Truncate indexes even if `m_indexes_are_enabled` is false. Somebody may
   * use truncate() before enabling indexes and we don't want an empty m_rows
   * with some stale data inside the indexes. */
  for (auto &entry : m_index_entries) {
    entry.m_index->truncate();
  }
}

inline Result Table::disable_indexes() {
  m_indexes_are_enabled = false;
  return Result::OK;
}

inline Result Table::enable_indexes() {
  if (m_rows.size() == 0 || m_indexes_are_enabled) {
    m_indexes_are_enabled = true;
    return Result::OK;
  }

  return Result::WRONG_COMMAND;
}

/** Create index for given key and appends it to indexes table. */
template <class T>
inline void Table::append_new_index(const KEY &mysql_index) {
  Index_entry entry;

  entry.m_alloc_size = sizeof(T);

  auto mem_ptr = m_allocator.allocate(entry.m_alloc_size);
  try {
    entry.m_index = new (mem_ptr) T(*this, mysql_index, m_allocator);

    m_index_entries.push_back(entry);
  } catch (...) {
    m_allocator.deallocate(mem_ptr, entry.m_alloc_size);
    throw;
  }
}

} /* namespace temptable */

#endif /* TEMPTABLE_TABLE_H */
