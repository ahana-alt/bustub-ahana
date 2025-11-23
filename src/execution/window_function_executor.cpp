//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// window_function_executor.cpp
//
// Identification: src/execution/window_function_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/window_function_executor.h"
#include <algorithm>
#include <vector>
#include "execution/execution_common.h"
#include "execution/plans/window_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

WindowFunctionExecutor::WindowFunctionExecutor(ExecutorContext *exec_ctx, const WindowFunctionPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void WindowFunctionExecutor::Init() {
  child_executor_->Init();
  input_tuples_.clear();
  output_tuples_.clear();
  output_index_ = 0;

  // Read all tuples from child
  std::vector<Tuple> tuple_batch;
  std::vector<RID> rid_batch;
  while (child_executor_->Next(&tuple_batch, &rid_batch, BUSTUB_BATCH_SIZE)) {
    for (const auto &tuple : tuple_batch) {
      input_tuples_.push_back(tuple);
    }
  }

  // Compute window functions
  ComputeWindowFunctions();
}

void WindowFunctionExecutor::ComputeWindowFunctions() {
  if (input_tuples_.empty()) {
    return;
  }

  auto &child_schema = child_executor_->GetOutputSchema();
  auto &output_schema = plan_->OutputSchema();

  // Check if any window function has ORDER BY
  bool has_order_by = false;
  std::vector<OrderBy> common_order_by;
  for (const auto &entry : plan_->window_functions_) {
    [[maybe_unused]] const auto &col_idx = entry.first;
    const auto &window_func = entry.second;
    if (!window_func.order_by_.empty()) {
      has_order_by = true;
      common_order_by = window_func.order_by_;
      break;
    }
  }

  // Initialize output tuples
  std::vector<Tuple> temp_output_tuples;
  for (const auto &input_tuple : input_tuples_) {
    std::vector<Value> values;
    values.reserve(output_schema.GetColumnCount());

    // Evaluate each column in plan_->columns_
    for (size_t col_idx = 0; col_idx < plan_->columns_.size(); col_idx++) {
      // Check if this column is a window function (placeholder)
      if (plan_->window_functions_.find(col_idx) != plan_->window_functions_.end()) {
        // This is a window function placeholder - add NULL for now
        values.emplace_back(TypeId::INTEGER);
      } else {
        // This is a regular column - evaluate it
        values.push_back(plan_->columns_[col_idx]->Evaluate(&input_tuple, child_schema));
      }
    }

    temp_output_tuples.emplace_back(values, &output_schema);
  }

  // Process each window function
  for (const auto &entry : plan_->window_functions_) {
    // Create indices for sorting
    const auto &col_idx = entry.first;
    const auto &window_func = entry.second;
    std::vector<size_t> indices(input_tuples_.size());
    for (size_t i = 0; i < indices.size(); i++) {
      indices[i] = i;
    }

    // Sort if ORDER BY or PARTITION BY is specified
    if (!window_func.order_by_.empty() || !window_func.partition_by_.empty()) {
      std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        const auto &tuple_a = input_tuples_[a];
        const auto &tuple_b = input_tuples_[b];

        // Compare partition by keys first
        for (const auto &partition_expr : window_func.partition_by_) {
          auto val_a = partition_expr->Evaluate(&tuple_a, child_schema);
          auto val_b = partition_expr->Evaluate(&tuple_b, child_schema);

          if (val_a.CompareLessThan(val_b) == CmpBool::CmpTrue) {
            return true;
          }
          if (val_a.CompareGreaterThan(val_b) == CmpBool::CmpTrue) {
            return false;
          }
        }

        // Then compare order by keys
        for (const auto &order_by : window_func.order_by_) {
          const auto &order_type = std::get<0>(order_by);
          const auto &order_expr = std::get<2>(order_by);
          auto val_a = order_expr->Evaluate(&tuple_a, child_schema);
          auto val_b = order_expr->Evaluate(&tuple_b, child_schema);

          if (val_a.CompareLessThan(val_b) == CmpBool::CmpTrue) {
            return order_type != OrderByType::DESC;
          }
          if (val_a.CompareGreaterThan(val_b) == CmpBool::CmpTrue) {
            return order_type == OrderByType::DESC;
          }
        }

        return false;
      });
    }

    // Group into partitions and compute window function
    size_t start = 0;
    while (start < indices.size()) {
      size_t end = start + 1;

      // Find partition boundary
      while (end < indices.size()) {
        bool same_partition = true;
        for (const auto &partition_expr : window_func.partition_by_) {
          auto val_start = partition_expr->Evaluate(&input_tuples_[indices[start]], child_schema);
          auto val_end = partition_expr->Evaluate(&input_tuples_[indices[end]], child_schema);

          if (val_start.CompareEquals(val_end) != CmpBool::CmpTrue) {
            same_partition = false;
            break;
          }
        }

        if (!same_partition) {
          break;
        }
        end++;
      }

      // Compute window function for this partition
      if (window_func.type_ == WindowFunctionType::Rank) {
        // RANK function
        int32_t rank = 1;
        for (size_t i = start; i < end; i++) {
          if (i > start) {
            // Check if order by values are the same as previous
            bool same_order = true;
            for (const auto &order_by : window_func.order_by_) {
              const auto &order_expr = std::get<2>(order_by);
              auto val_prev = order_expr->Evaluate(&input_tuples_[indices[i - 1]], child_schema);
              auto val_curr = order_expr->Evaluate(&input_tuples_[indices[i]], child_schema);

              if (val_prev.CompareEquals(val_curr) != CmpBool::CmpTrue) {
                same_order = false;
                break;
              }
            }

            if (!same_order) {
              rank = i - start + 1;
            }
          }

          // Update output tuple with rank value
          std::vector<Value> values;
          values.reserve(output_schema.GetColumnCount());
          for (uint32_t j = 0; j < output_schema.GetColumnCount(); j++) {
            if (j == col_idx) {
              values.emplace_back(TypeId::INTEGER, rank);
            } else {
              values.push_back(temp_output_tuples[indices[i]].GetValue(&output_schema, j));
            }
          }
          temp_output_tuples[indices[i]] = Tuple(values, &output_schema);
        }
      } else {
        // Aggregation functions
        for (size_t i = start; i < end; i++) {
          size_t frame_start = start;
          size_t frame_end = window_func.order_by_.empty() ? end : i + 1;

          // Compute aggregate for frame [frame_start, frame_end)
          Value result;
          if (window_func.type_ == WindowFunctionType::CountStarAggregate) {
            result = Value(TypeId::INTEGER, static_cast<int32_t>(frame_end - frame_start));
          } else {
            // Initialize aggregate
            bool first = true;
            int32_t count = 0;

            for (size_t j = frame_start; j < frame_end; j++) {
              auto val = window_func.function_->Evaluate(&input_tuples_[indices[j]], child_schema);

              if (first) {
                if (window_func.type_ == WindowFunctionType::CountAggregate) {
                  result = val.IsNull() ? Value(TypeId::INTEGER, 0) : Value(TypeId::INTEGER, 1);
                  count = val.IsNull() ? 0 : 1;
                } else {
                  result = val;
                }
                first = false;
              } else {
                switch (window_func.type_) {
                  case WindowFunctionType::SumAggregate:
                    if (!val.IsNull()) {
                      if (result.IsNull()) {
                        result = val;
                      } else {
                        result = result.Add(val);
                      }
                    }
                    break;
                  case WindowFunctionType::MinAggregate:
                    if (!val.IsNull() && (result.IsNull() || val.CompareLessThan(result) == CmpBool::CmpTrue)) {
                      result = val;
                    }
                    break;
                  case WindowFunctionType::MaxAggregate:
                    if (!val.IsNull() && (result.IsNull() || val.CompareGreaterThan(result) == CmpBool::CmpTrue)) {
                      result = val;
                    }
                    break;
                  case WindowFunctionType::CountAggregate:
                    if (!val.IsNull()) {
                      count++;
                      result = Value(TypeId::INTEGER, count);
                    }
                    break;
                  default:
                    break;
                }
              }
            }
          }

          // Update output tuple
          std::vector<Value> values;
          values.reserve(output_schema.GetColumnCount());
          for (uint32_t j = 0; j < output_schema.GetColumnCount(); j++) {
            if (j == col_idx) {
              values.push_back(result);
            } else {
              values.push_back(temp_output_tuples[indices[i]].GetValue(&output_schema, j));
            }
          }
          temp_output_tuples[indices[i]] = Tuple(values, &output_schema);
        }
      }

      start = end;
    }
  }

  // If there's an ORDER BY, output tuples in sorted order
  // Otherwise, output in original order
  if (has_order_by) {
    // Create indices and sort them
    std::vector<size_t> output_indices(input_tuples_.size());
    for (size_t i = 0; i < output_indices.size(); i++) {
      output_indices[i] = i;
    }

    std::sort(output_indices.begin(), output_indices.end(), [&](size_t a, size_t b) {
      const auto &tuple_a = input_tuples_[a];
      const auto &tuple_b = input_tuples_[b];

      for (const auto &order_by : common_order_by) {
        const auto &order_type = std::get<0>(order_by);
        const auto &order_expr = std::get<2>(order_by);
        auto val_a = order_expr->Evaluate(&tuple_a, child_schema);
        auto val_b = order_expr->Evaluate(&tuple_b, child_schema);

        if (val_a.CompareLessThan(val_b) == CmpBool::CmpTrue) {
          return order_type != OrderByType::DESC;
        }
        if (val_a.CompareGreaterThan(val_b) == CmpBool::CmpTrue) {
          return order_type == OrderByType::DESC;
        }
      }

      return false;
    });

    // Reorder output tuples based on sorted indices
    for (size_t idx : output_indices) {
      output_tuples_.push_back(temp_output_tuples[idx]);
    }
  } else {
    // No ORDER BY, keep original order
    output_tuples_ = std::move(temp_output_tuples);
  }
}

/**
 * Yield the next tuple batch from the window aggregation.
 * @param[out] tuple_batch The next tuple batch produced by the window aggregation
 * @param[out] rid_batch The next tuple RID batch produced by the window aggregation
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto WindowFunctionExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                  size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  // Return batches from the pre-computed output_tuples_
  while (output_index_ < output_tuples_.size() && tuple_batch->size() < batch_size) {
    tuple_batch->push_back(output_tuples_[output_index_]);
    rid_batch->emplace_back();  // Empty RID
    output_index_++;
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
