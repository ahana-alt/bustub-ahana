//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// sort_limit_as_topn.cpp
//
// Identification: src/optimizer/sort_limit_as_topn.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/plans/limit_plan.h"
#include "execution/plans/sort_plan.h"
#include "execution/plans/topn_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

/**
 * @brief optimize sort + limit as top N
 */
auto Optimizer::OptimizeSortLimitAsTopN(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // First, recursively optimize all children
  std::vector<AbstractPlanNodeRef> optimized_children;
  for (const auto &child : plan->GetChildren()) {
    optimized_children.emplace_back(OptimizeSortLimitAsTopN(child));
  }
  // Clone the current plan with optimized children
  auto optimized_plan = plan->CloneWithChildren(std::move(optimized_children));
  // Check if the current plan is a Limit node
  if (optimized_plan->GetType() == PlanType::Limit) {
    const auto &limit_plan = dynamic_cast<const LimitPlanNode &>(*optimized_plan);
    // Check if the child is a Sort node
    BUSTUB_ENSURE(limit_plan.GetChildren().size() == 1, "Limit should have exactly one child");
    const auto &child = limit_plan.GetChildAt(0);
    if (child->GetType() == PlanType::Sort) {
      const auto &sort_plan = dynamic_cast<const SortPlanNode &>(*child);
      // Pattern matched: Limit -> Sort
      // Replace with TopN
      return std::make_shared<TopNPlanNode>(optimized_plan->output_schema_,  // Use the output schema from limit
                                            sort_plan.GetChildAt(0),         // The child of the sort node
                                            sort_plan.GetOrderBy(),          // Order by expressions from sort
                                            limit_plan.GetLimit());  // N from limit - closing paren on same line
    }
  }
  // No optimization possible, return the plan as-is
  return optimized_plan;
}

}  // namespace bustub
