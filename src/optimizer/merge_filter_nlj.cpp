//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// merge_filter_nlj.cpp
//
// Identification: src/optimizer/merge_filter_nlj.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
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
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

/**
 * Helper function to split AND-connected predicates into a vector
 */
static auto SplitConjunction(const AbstractExpressionRef &expr) -> std::vector<AbstractExpressionRef> {
  if (const auto *logic = dynamic_cast<const LogicExpression *>(expr.get())) {
    if (logic->logic_type_ == LogicType::And) {
      auto left_preds = SplitConjunction(logic->GetChildAt(0));
      auto right_preds = SplitConjunction(logic->GetChildAt(1));
      left_preds.insert(left_preds.end(), right_preds.begin(), right_preds.end());
      return left_preds;
    }
  }
  return {expr};
}

/**
 * Helper function to combine predicates with AND
 */
static auto CombinePredicates(const std::vector<AbstractExpressionRef> &preds) -> AbstractExpressionRef {
  if (preds.empty()) {
    return std::make_shared<ConstantValueExpression>(ValueFactory::GetBooleanValue(true));
  }
  if (preds.size() == 1) {
    return preds[0];
  }

  auto result = preds[0];
  for (size_t i = 1; i < preds.size(); i++) {
    result = std::make_shared<LogicExpression>(result, preds[i], LogicType::And);
  }
  return result;
}

/**
 * Check if an expression references the left child (tuple_idx == 0)
 */
static auto ReferencesLeftChild(const AbstractExpressionRef &expr) -> bool {
  if (const auto *col = dynamic_cast<const ColumnValueExpression *>(expr.get())) {
    return col->GetTupleIdx() == 0;
  }
  for (const auto &child : expr->GetChildren()) {
    if (ReferencesLeftChild(child)) {
      return true;
    }
  }
  return false;
}

/**
 * Check if an expression references the right child (tuple_idx == 1)
 */
static auto ReferencesRightChild(const AbstractExpressionRef &expr) -> bool {
  if (const auto *col = dynamic_cast<const ColumnValueExpression *>(expr.get())) {
    return col->GetTupleIdx() == 1;
  }
  for (const auto &child : expr->GetChildren()) {
    if (ReferencesRightChild(child)) {
      return true;
    }
  }
  return false;
}

/**
 * @brief rewrite expression to be used in nested loop joins. e.g., if we have `SELECT * FROM a, b WHERE a.x = b.y`,
 * we will have `#0.x = #0.y` in the filter plan node. We will need to figure out where does `0.x` and `0.y` belong
 * in NLJ (left table or right table?), and rewrite it as `#0.x = #1.y`.
 *
 * @param expr the filter expression
 * @param left_column_cnt number of columns in the left size of the NLJ
 * @param right_column_cnt number of columns in the left size of the NLJ
 */
auto Optimizer::RewriteExpressionForJoin(const AbstractExpressionRef &expr, size_t left_column_cnt,
                                         size_t right_column_cnt) -> AbstractExpressionRef {
  std::vector<AbstractExpressionRef> children;
  for (const auto &child : expr->GetChildren()) {
    children.emplace_back(RewriteExpressionForJoin(child, left_column_cnt, right_column_cnt));
  }
  if (const auto *column_value_expr = dynamic_cast<const ColumnValueExpression *>(expr.get());
      column_value_expr != nullptr) {
    BUSTUB_ENSURE(column_value_expr->GetTupleIdx() == 0, "tuple_idx cannot be value other than 0 before this stage.")
    auto col_idx = column_value_expr->GetColIdx();
    if (col_idx < left_column_cnt) {
      return std::make_shared<ColumnValueExpression>(0, col_idx, column_value_expr->GetReturnType());
    }
    if (col_idx >= left_column_cnt && col_idx < left_column_cnt + right_column_cnt) {
      return std::make_shared<ColumnValueExpression>(1, col_idx - left_column_cnt, column_value_expr->GetReturnType());
    }
    throw bustub::Exception("col_idx not in range");
  }
  return expr->CloneWithChildren(children);
}

/** @brief check if the predicate is true::boolean */
auto Optimizer::IsPredicateTrue(const AbstractExpressionRef &expr) -> bool {
  if (const auto *const_expr = dynamic_cast<const ConstantValueExpression *>(expr.get()); const_expr != nullptr) {
    return const_expr->val_.CastAs(TypeId::BOOLEAN).GetAs<bool>();
  }
  return false;
}

/**
 * @brief merge filter condition into nested loop join with predicate pushdown.
 * In planner, we plan cross join + filter with cross product (done with nested loop join) and a filter plan node. We
 * can merge the filter condition into nested loop join to achieve better efficiency.
 *
 * Enhanced version: also pushes down predicates that only reference one side of the join.
 */
auto Optimizer::OptimizeMergeFilterNLJ(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeMergeFilterNLJ(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() == PlanType::Filter) {
    const auto &filter_plan = dynamic_cast<const FilterPlanNode &>(*optimized_plan);
    BUSTUB_ENSURE(optimized_plan->children_.size() == 1, "Filter with multiple children?? Impossible!");
    const auto &child_plan = optimized_plan->children_[0];

    if (child_plan->GetType() == PlanType::NestedLoopJoin) {
      const auto &nlj_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*child_plan);
      BUSTUB_ENSURE(child_plan->GetChildren().size() == 2, "NLJ should have exactly 2 children.");

      if (IsPredicateTrue(nlj_plan.Predicate())) {
        // Support INNER and LEFT joins
        bool is_left_join = (nlj_plan.GetJoinType() == JoinType::LEFT);

        if (nlj_plan.GetJoinType() != JoinType::INNER && nlj_plan.GetJoinType() != JoinType::LEFT) {
          return optimized_plan;
        }

        // Rewrite the filter predicate for join
        auto rewritten_predicate = RewriteExpressionForJoin(filter_plan.GetPredicate(),
                                                            nlj_plan.GetLeftPlan()->OutputSchema().GetColumnCount(),
                                                            nlj_plan.GetRightPlan()->OutputSchema().GetColumnCount());

        // Split predicate into individual conditions
        auto predicates = SplitConjunction(rewritten_predicate);

        // Categorize predicates
        std::vector<AbstractExpressionRef> join_conditions;
        std::vector<AbstractExpressionRef> left_filters;
        std::vector<AbstractExpressionRef> right_filters;

        for (const auto &pred : predicates) {
          bool refs_left = ReferencesLeftChild(pred);
          bool refs_right = ReferencesRightChild(pred);

          if (refs_left && refs_right) {
            join_conditions.push_back(pred);
          } else if (refs_left) {
            left_filters.push_back(pred);
          } else if (refs_right) {
            // For LEFT JOIN, cannot push filters to right side (would filter out NULLs)
            if (is_left_join) {
              join_conditions.push_back(pred);
            } else {
              right_filters.push_back(pred);
            }
          } else {
            join_conditions.push_back(pred);
          }
        }

        // Build new plan with pushed filters
        auto new_left = nlj_plan.GetLeftPlan();
        auto new_right = nlj_plan.GetRightPlan();

        if (!left_filters.empty()) {
          new_left = std::make_shared<FilterPlanNode>(nlj_plan.GetLeftPlan()->output_schema_,
                                                      CombinePredicates(left_filters), nlj_plan.GetLeftPlan());
        }

        if (!right_filters.empty()) {
          new_right = std::make_shared<FilterPlanNode>(nlj_plan.GetRightPlan()->output_schema_,
                                                       CombinePredicates(right_filters), nlj_plan.GetRightPlan());
        }

        // Recursively optimize children BEFORE creating NLJ
        new_left = OptimizeMergeFilterNLJ(new_left);
        new_right = OptimizeMergeFilterNLJ(new_right);

        // Create NLJ with join conditions
        return std::make_shared<NestedLoopJoinPlanNode>(filter_plan.output_schema_, new_left, new_right,
                                                        CombinePredicates(join_conditions), nlj_plan.GetJoinType());
      }
    }
  }
  return optimized_plan;
}

}  // namespace bustub
