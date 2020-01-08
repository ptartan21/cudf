/*
 * Copyright (c) 2019, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cudf/cudf.h>
#include <cudf/utilities/error.hpp>
#include <cudf/column/column_view.hpp>
#include <cudf/copying.hpp>
#include <cudf/detail/copy.hpp>
#include <cudf/detail/null_mask.hpp>

#include <algorithm>

namespace cudf {
namespace experimental {

namespace detail {

std::vector<column_view> slice(column_view const& input,
                               std::vector<size_type> const& indices,
                               cudaStream_t stream){
  CUDF_EXPECTS(indices.size() % 2 == 0, "indices size must be even");

  std::vector<column_view> result{};

  if(indices.size() == 0 or input.size() == 0) {
    return result;
  }

  auto null_counts = cudf::detail::segmented_count_unset_bits(input.null_mask(),
                                                              indices, stream);

  for (size_t i = 0; i < indices.size() / 2; i++) {
    std::vector<column_view> children{};
    for (size_type j = 0; j < input.num_children(); j++) {
        children.push_back(input.child(j));
    }
    auto begin = indices[2 * i];
    auto end = indices[2 * i + 1];
    CUDF_EXPECTS(begin >= 0, "Invalid beginning of range.");
    CUDF_EXPECTS(end >= begin, "Invalid end of range.");
    CUDF_EXPECTS(end <= input.size(), "Slice range out of bounds.");
    result.emplace_back(input.type(), end - begin, input.head(),
                        input.null_mask(), null_counts[i],
                        input.offset() + begin, std::move(children));
  }

  return result;
}

}  // detail

std::vector<cudf::column_view> slice(cudf::column_view const& input,
                                     std::vector<size_type> const& indices){
  return detail::slice(input, indices, 0);
}

std::vector<cudf::table_view> slice(cudf::table_view const& input,
                                                std::vector<size_type> const& indices){

    CUDF_EXPECTS(indices.size()%2 == 0, "indices size must be even");
    std::vector<cudf::table_view> result{};

    if(indices.size() == 0 or input.num_columns() == 0) {
        return result;
    }

    // 2d arrangement of column_views that represent the outgoing table_views    
    // sliced_table[i][j]
    // where i is the i'th column of the j'th table_view
    std::vector<std::vector<cudf::column_view>> sliced_table;
    sliced_table.reserve(indices.size() + 1);
    std::transform(input.begin(), input.end(), std::back_inserter(sliced_table), [&indices](cudf::column_view const& c){
       return slice(c, indices);
    });

    // distribute columns into outgoing table_views
    size_t num_output_tables = indices.size()/2;
    for(size_t i = 0; i < num_output_tables; i++){        
        std::vector<cudf::column_view> table_columns;
        for(size_t j = 0; j<input.num_columns(); j++){
            table_columns.emplace_back(sliced_table[j][i]);
        }
        result.emplace_back(table_view{table_columns});
    }

    return result;
};

} // experimental
} // cudf
