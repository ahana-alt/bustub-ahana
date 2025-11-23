//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nlj_as_hash_join.cpp
//
// Identification: src/optimizer/nlj_as_hash_join.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <vector>
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

/**
 * Helper function to extract equi-join conditions from a predicate.
 * Returns a vector of pairs (left_expr, right_expr) for each equi-condition.
 */
auto ExtractEquiJoinConditions(const AbstractExpressionRef &predicate, const Schema &left_schema,
                               const Schema &right_schema)
    -> std::vector<std::pair<AbstractExpressionRef, AbstractExpressionRef>> {
  std::vector<std::pair<AbstractExpressionRef, AbstractExpressionRef>> conditions;

  // Base case: if this is an equality comparison
  if (const auto *cmp_expr = dynamic_cast<const ComparisonExpression *>(predicate.get())) {
    if (cmp_expr->comp_type_ == ComparisonType::Equal) {
      // Check if both children are column value expressions
      const auto *left_col = dynamic_cast<const ColumnValueExpression *>(cmp_expr->GetChildAt(0).get());
      const auto *right_col = dynamic_cast<const ColumnValueExpression *>(cmp_expr->GetChildAt(1).get());

      if (left_col != nullptr && right_col != nullptr) {
        // Determine which column belongs to which table
        auto left_tuple_idx = left_col->GetTupleIdx();
        auto right_tuple_idx = right_col->GetTupleIdx();

        // Check if one is from left (tuple_idx 0) and one is from right (tuple_idx 1)
        if (left_tuple_idx == 0 && right_tuple_idx == 1) {
          conditions.emplace_back(cmp_expr->GetChildAt(0), cmp_expr->GetChildAt(1));
        } else if (left_tuple_idx == 1 && right_tuple_idx == 0) {
          // Swap to maintain left-right order
          conditions.emplace_back(cmp_expr->GetChildAt(1), cmp_expr->GetChildAt(0));
        }
      }
    }
  } else if (const auto *logic_expr = dynamic_cast<const LogicExpression *>(predicate.get())) {
    if (logic_expr->logic_type_ == LogicType::And) {
      // Recursively extract from both children
      auto left_conditions = ExtractEquiJoinConditions(logic_expr->GetChildAt(0), left_schema, right_schema);
      auto right_conditions = ExtractEquiJoinConditions(logic_expr->GetChildAt(1), left_schema, right_schema);

      conditions.insert(conditions.end(), left_conditions.begin(), left_conditions.end());
      conditions.insert(conditions.end(), right_conditions.begin(), right_conditions.end());
    }
  }

  return conditions;
}

/**
 * @brief optimize nested loop join into hash join.
 * Supports optimizing joins with multiple equi-conditions joined by AND.
 */
auto Optimizer::OptimizeNLJAsHashJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeNLJAsHashJoin(child));
  }

  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  // Check if this is a NestedLoopJoin node
  if (optimized_plan->GetType() == PlanType::NestedLoopJoin) {
    const auto &nlj_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan);

    // Check if we have a predicate
    if (nlj_plan.Predicate() != nullptr) {
      // Extract equi-join conditions
      auto equi_conditions = ExtractEquiJoinConditions(nlj_plan.Predicate(), nlj_plan.GetLeftPlan()->OutputSchema(),
                                                       nlj_plan.GetRightPlan()->OutputSchema());

      // If we found at least one equi-join condition, convert to hash join
      if (!equi_conditions.empty()) {
        // Separate left and right key expressions
        std::vector<AbstractExpressionRef> left_key_exprs;
        std::vector<AbstractExpressionRef> right_key_exprs;

        for (const auto &[left_expr, right_expr] : equi_conditions) {
          left_key_exprs.push_back(left_expr);
          right_key_exprs.push_back(right_expr);
        }

        // Create a hash join plan node (supports INNER and LEFT joins)
        return std::make_shared<HashJoinPlanNode>(nlj_plan.output_schema_, nlj_plan.GetLeftPlan(),
                                                  nlj_plan.GetRightPlan(), std::move(left_key_exprs),
                                                  std::move(right_key_exprs), nlj_plan.GetJoinType());
      }
    }
  }

  return optimized_plan;
}

}  // namespace bustub
