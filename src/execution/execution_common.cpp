//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// execution_common.cpp
//
// Identification: src/execution/execution_common.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/execution_common.h"
#include "catalog/catalog.h"
#include "common/config.h"
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "fmt/core.h"
#include "storage/table/table_heap.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

auto GenerateSortKey(const Tuple &tuple, const std::vector<OrderBy> &order_bys, const Schema &schema) -> SortKey {
  SortKey key;
  key.reserve(order_bys.size());
  for (const auto &order_by : order_bys) {
    const auto &expr = std::get<2>(order_by);  // Get the expression from the tuple
    key.push_back(expr->Evaluate(&tuple, schema));
  }
  return key;
}

TupleComparator::TupleComparator(std::vector<OrderBy> order_bys) : order_bys_(std::move(order_bys)) {}

auto TupleComparator::operator()(const SortEntry &a, const SortEntry &b) const -> bool {
  const auto &key_a = a.first;
  const auto &key_b = b.first;

  for (size_t i = 0; i < order_bys_.size(); i++) {
    const auto &order_type = std::get<0>(order_bys_[i]);  // OrderByType
    const auto &null_type = std::get<1>(order_bys_[i]);   // OrderByNullType
    const auto &val_a = key_a[i];
    const auto &val_b = key_b[i];

    // Handle NULL comparison according to NULLS FIRST/LAST
    bool a_is_null = val_a.IsNull();
    bool b_is_null = val_b.IsNull();

    if (a_is_null && b_is_null) {
      continue;  // Both NULL, consider equal, check next key
    }

    // Determine effective null ordering
    bool nulls_first;
    if (null_type == OrderByNullType::NULLS_FIRST) {
      nulls_first = true;
    } else if (null_type == OrderByNullType::NULLS_LAST) {
      nulls_first = false;
    } else {
      // DEFAULT case: NULLS FIRST for ASC/DEFAULT, NULLS LAST for DESC
      nulls_first = (order_type != OrderByType::DESC);
    }

    if (a_is_null) {
      // a is NULL, b is not
      return nulls_first;  // true if NULLS FIRST (NULL < non-NULL), false if NULLS LAST (NULL > non-NULL)
    }

    if (b_is_null) {
      // b is NULL, a is not
      return !nulls_first;  // false if NULLS FIRST (non-NULL > NULL), true if NULLS LAST (non-NULL < NULL)
    }

    // Neither is NULL, do normal comparison
    CmpBool cmp = val_a.CompareLessThan(val_b);
    if (cmp == CmpBool::CmpTrue) {
      // a < b
      return order_type != OrderByType::DESC;  // true for ASC/DEFAULT, false for DESC
    }
    if (val_a.CompareGreaterThan(val_b) == CmpBool::CmpTrue) {
      // a > b
      return order_type == OrderByType::DESC;  // false for ASC/DEFAULT, true for DESC
    }
    // a == b, continue to next sort key
  }

  // All keys are equal
  return false;
}

auto ReconstructTuple(const Schema *schema, const Tuple &base_tuple, const TupleMeta &base_meta,
                      const std::vector<UndoLog> &undo_logs) -> std::optional<Tuple> {
  return std::nullopt;
}

void TxnMgrDbg(const std::string &info, TransactionManager *txn_mgr, const TableInfo *table_info,
               TableHeap *table_heap) {
  // always use stderr for printing logs...
  fmt::println(stderr, "debug_hook: {}", info);
}

}  // namespace bustub
