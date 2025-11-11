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

auto GenerateSortKey(const Tuple &tuple, const std::vector<std::pair<OrderByType, AbstractExpressionRef>> &order_bys,
                     const Schema &schema) -> SortKey {
  SortKey key;
  key.reserve(order_bys.size());

  for (const auto &[order_type, expr] : order_bys) {
    key.push_back(expr->Evaluate(&tuple, schema));
  }

  return key;
}

TupleComparator::TupleComparator(std::vector<std::pair<OrderByType, AbstractExpressionRef>> order_bys)
    : order_bys_(std::move(order_bys)) {}

auto TupleComparator::operator()(const SortEntry &a, const SortEntry &b) const -> bool {
  const auto &key_a = a.first;
  const auto &key_b = b.first;

  for (size_t i = 0; i < order_bys_.size(); i++) {
    const auto &order_type = order_bys_[i].first;
    const auto &val_a = key_a[i];
    const auto &val_b = key_b[i];

    // Handle NULL comparison according to NULLS FIRST/LAST
    bool a_is_null = val_a.IsNull();
    bool b_is_null = val_b.IsNull();

    if (a_is_null && b_is_null) {
      continue;  // Both NULL, consider equal, check next key
    }

    if (a_is_null) {
      // a is NULL, b is not
      // For ASC: NULLS FIRST (default) means NULL < non-NULL, return true
      // For DESC: NULLS LAST (default) means NULL > non-NULL, return false
      return order_type == OrderByType::ASC || order_type == OrderByType::DEFAULT;
    }

    if (b_is_null) {
      // b is NULL, a is not
      // For ASC: NULLS FIRST means non-NULL > NULL, return false
      // For DESC: NULLS LAST means non-NULL < NULL, return true
      return order_type == OrderByType::DESC;
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
