//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seqscan_as_indexscan.cpp
//
// Identification: src/optimizer/seqscan_as_indexscan.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/index_scan_plan.h"
#include "execution/plans/seq_scan_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

/**
 * @brief Optimizes seq scan as index scan if there's an index on a table
 */
auto Optimizer::OptimizeSeqScanAsIndexScan(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // First, recursively optimize children (bottom-up approach)
  std::vector<AbstractPlanNodeRef> optimized_children;
  for (const auto &child : plan->GetChildren()) {
    optimized_children.push_back(OptimizeSeqScanAsIndexScan(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(optimized_children));

  // Check if this is a SeqScan node
  if (optimized_plan->GetType() != PlanType::SeqScan) {
    return optimized_plan;
  }

  const auto &seq_scan = dynamic_cast<const SeqScanPlanNode &>(*optimized_plan);

  // Check if there's a filter predicate
  if (seq_scan.filter_predicate_ == nullptr) {
    return optimized_plan;
  }

  // Get table info
  auto table_info = catalog_.GetTable(seq_scan.table_name_);
  if (table_info == nullptr) {
    return optimized_plan;
  }

  // Get all indexes on this table
  auto indexes = catalog_.GetTableIndexes(table_info->name_);
  if (indexes.empty()) {
    return optimized_plan;
  }

  // Helper function to check if an expression is an equality comparison on an indexed column
  auto is_index_match = [&](const AbstractExpressionRef &expr, uint32_t &index_oid) -> bool {
    // Check if it's a comparison expression
    if (auto comp_expr = dynamic_cast<const ComparisonExpression *>(expr.get())) {
      // Must be an equality comparison
      if (comp_expr->comp_type_ != ComparisonType::Equal) {
        return false;
      }

      // Check both orders: col = val and val = col
      const AbstractExpression *col_expr = nullptr;

      // Try: left = column, right = constant
      if (dynamic_cast<const ColumnValueExpression *>(comp_expr->children_[0].get()) != nullptr &&
          dynamic_cast<const ConstantValueExpression *>(comp_expr->children_[1].get()) != nullptr) {
        col_expr = comp_expr->children_[0].get();
      } else if (dynamic_cast<const ConstantValueExpression *>(comp_expr->children_[0].get()) != nullptr &&
                 dynamic_cast<const ColumnValueExpression *>(comp_expr->children_[1].get()) != nullptr) {
        col_expr = comp_expr->children_[1].get();
      } else {
        return false;
      }

      // Get the column index
      auto col_val_expr = dynamic_cast<const ColumnValueExpression *>(col_expr);
      if (col_val_expr == nullptr) {
        return false;
      }

      uint32_t col_idx = col_val_expr->GetColIdx();

      // Check if there's an index on this column
      for (const auto &index : indexes) {
        const auto &key_attrs = index->index_->GetKeyAttrs();

        // We only support single-column indexes
        if (key_attrs.size() == 1 && key_attrs[0] == col_idx) {
          index_oid = index->index_oid_;
          return true;
        }
      }
    }
    return false;
  };

  // Helper function to recursively collect all equality comparisons from OR expressions
  std::function<bool(const AbstractExpressionRef &, uint32_t &, std::vector<AbstractExpressionRef> &)>
      collect_or_predicates = [&](const AbstractExpressionRef &expr, uint32_t &index_oid,
                                  std::vector<AbstractExpressionRef> &predicates) -> bool {
    // If it's a simple comparison, check if it matches an index
    uint32_t temp_index_oid;
    if (is_index_match(expr, temp_index_oid)) {
      if (index_oid == std::numeric_limits<uint32_t>::max()) {
        index_oid = temp_index_oid;
      } else if (index_oid != temp_index_oid) {
        return false;  // Different indexes
      }
      predicates.push_back(expr);
      return true;
    }

    // If it's an OR expression, recursively collect from children
    if (auto logic_expr = dynamic_cast<const LogicExpression *>(expr.get())) {
      if (logic_expr->logic_type_ == LogicType::Or) {
        for (const auto &child : logic_expr->children_) {
          if (!collect_or_predicates(child, index_oid, predicates)) {
            return false;
          }
        }
        return true;
      }
    }

    return false;
  };

  uint32_t matched_index_oid = std::numeric_limits<uint32_t>::max();

  // Check if the predicate is a simple equality comparison
  if (is_index_match(seq_scan.filter_predicate_, matched_index_oid)) {
    // Create an IndexScan node
    return std::make_shared<IndexScanPlanNode>(seq_scan.output_schema_, table_info->oid_, matched_index_oid,
                                               seq_scan.filter_predicate_);
  }

  // Check if it's an OR expression (possibly nested) with multiple point lookups on the same index
  std::vector<AbstractExpressionRef> or_predicates;
  if (collect_or_predicates(seq_scan.filter_predicate_, matched_index_oid, or_predicates)) {
    if (!or_predicates.empty() && matched_index_oid != std::numeric_limits<uint32_t>::max()) {
      // Create an IndexScan node with the original OR predicate
      return std::make_shared<IndexScanPlanNode>(seq_scan.output_schema_, table_info->oid_, matched_index_oid,
                                                 seq_scan.filter_predicate_);
    }
  }

  // Cannot optimize, return original plan
  return optimized_plan;
}

}  // namespace bustub
