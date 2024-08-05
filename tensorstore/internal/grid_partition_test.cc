// Copyright 2020 The TensorStore Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tensorstore/internal/grid_partition.h"

#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "absl/status/status.h"
#include "tensorstore/array.h"
#include "tensorstore/box.h"
#include "tensorstore/index.h"
#include "tensorstore/index_interval.h"
#include "tensorstore/index_space/index_transform.h"
#include "tensorstore/index_space/index_transform_builder.h"
#include "tensorstore/internal/grid_partition_impl.h"
#include "tensorstore/internal/irregular_grid.h"
#include "tensorstore/internal/regular_grid.h"
#include "tensorstore/util/result.h"
#include "tensorstore/util/span.h"
#include "tensorstore/util/status.h"

namespace {
using ::tensorstore::Box;
using ::tensorstore::BoxView;
using ::tensorstore::DimensionIndex;
using ::tensorstore::Index;
using ::tensorstore::IndexInterval;
using ::tensorstore::IndexTransform;
using ::tensorstore::IndexTransformBuilder;
using ::tensorstore::IndexTransformView;
using ::tensorstore::MakeArray;
using ::tensorstore::Result;
using ::tensorstore::internal::GetGridCellRanges;
using ::tensorstore::internal::IrregularGrid;
using ::tensorstore::internal_grid_partition::IndexTransformGridPartition;
using ::tensorstore::internal_grid_partition::OutputToGridCellFn;
using ::tensorstore::internal_grid_partition::
    PrePartitionIndexTransformOverGrid;
using ::tensorstore::internal_grid_partition::RegularGridRef;
using ::testing::ElementsAre;

namespace partition_tests {
/// Representation of a partition, specifically the arguments supplied to the
/// callback passed to `PartitionIndexTransformOverRegularGrid`.  This is a
/// pair of:
///
/// 0. Grid cell index vector
/// 1. cell_transform transform
using R = std::pair<std::vector<Index>, IndexTransform<>>;

/// Returns the list of partitions generated by
/// `PartitionIndexTransformOverRegularGrid` when called with the specified
/// arguments.
///
/// \param grid_output_dimensions The sequence of output dimensions of the index
///     space "output" corresponding to the grid.
/// \param grid_cell_shape Array of length `grid_output_dimensions.size()`
///     specifying the cell of a grid cell along each grid dimension.
/// \param transform A transform from the "full" input space to the "output"
///     index space.
/// \returns The list of partitions.
std::vector<R> GetPartitions(
    const std::vector<DimensionIndex>& grid_output_dimensions,
    const std::vector<Index>& grid_cell_shape, IndexTransformView<> transform) {
  std::vector<R> results;

  IndexTransformGridPartition info;
  RegularGridRef grid{grid_cell_shape};
  TENSORSTORE_CHECK_OK(PrePartitionIndexTransformOverGrid(
      transform, grid_output_dimensions, grid, info));
  TENSORSTORE_CHECK_OK(
      tensorstore::internal::PartitionIndexTransformOverRegularGrid(
          grid_output_dimensions, grid_cell_shape, transform,
          [&](tensorstore::span<const Index> grid_cell_indices,
              IndexTransformView<> cell_transform) {
            auto cell_transform_direct = info.GetCellTransform(
                transform, grid_cell_indices, grid_output_dimensions,
                [&](DimensionIndex dim, Index cell_index) {
                  return grid.GetCellOutputInterval(dim, cell_index);
                });
            EXPECT_EQ(cell_transform_direct, cell_transform);
            results.emplace_back(std::vector<Index>(grid_cell_indices.begin(),
                                                    grid_cell_indices.end()),
                                 IndexTransform<>(cell_transform));
            return absl::OkStatus();
          }));
  return results;
}

// Tests that a one-dimensional transform with a constant output map is
// partitioned into 1 part.
TEST(PartitionIndexTransformOverRegularGrid, ConstantOneDimensional) {
  const auto results = GetPartitions({0}, {2},
                                     IndexTransformBuilder<>(1, 1)
                                         .input_origin({2})
                                         .input_shape({4})
                                         .output_constant(0, 3)
                                         .Finalize()
                                         .value());
  // Input index:    2   3   4   5
  // Output index:   3
  // Grid index:     1
  //  = Output index / 2
  EXPECT_THAT(      //
      results,      //
      ElementsAre(  //
          R{{1},
            IndexTransformBuilder<>(1, 1)
                .input_origin({2})
                .input_shape({4})
                .output_single_input_dimension(0, 0)
                .Finalize()
                .value()}));
}

// Tests that a two-dimensional transform with constant output maps is
// partitioned into 1 part.
TEST(PartitionIndexTransformOverRegularGrid, ConstantTwoDimensional) {
  const auto results = GetPartitions({0, 1}, {2, 3},
                                     IndexTransformBuilder<>(2, 2)
                                         .input_origin({2, 3})
                                         .input_shape({4, 5})
                                         .output_constant(0, 3)
                                         .output_constant(1, 7)
                                         .Finalize()
                                         .value());
  // Input index 0:  2   3   4   5
  // Input index 1:  3   4   5   6   7

  // Output index 0: 3
  // Grid index 0:   1
  //  = Output index / 2
  //
  // Output index 1: 7
  // Grid index 0:   2
  //  = Output index / 3

  EXPECT_THAT(      //
      results,      //
      ElementsAre(  //
          R{{1, 2},
            IndexTransformBuilder<>(2, 2)
                .input_origin({2, 3})
                .input_shape({4, 5})
                .output_identity_transform()
                .Finalize()
                .value()}));
}

// Tests that a one-dimensional identity transform over the domain `[-4,1]` with
// a cell size of `2` is partitioned into 3 parts, with the domains: `[-4,-3]`,
// `[-2,-1]`, and `[0,0]`.
TEST(PartitionIndexTransformOverRegularGrid, OneDimensionalUnitStride) {
  const auto results = GetPartitions({0}, {2},
                                     IndexTransformBuilder<>(1, 1)
                                         .input_origin({-4})
                                         .input_shape({5})
                                         .output_identity_transform()
                                         .Finalize()
                                         .value());
  // Input index:   -4  -3  -2  -1   0
  // Output index:  -4  -3  -2  -1   0
  //  = Input index
  // Grid index:    -2  -2  -1  -1   0
  //  = Output index / 2
  EXPECT_THAT(      //
      results,      //
      ElementsAre(  //
          R{{-2},
            IndexTransformBuilder<>(1, 1)
                .input_origin({-4})
                .input_shape({2})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{-1},
            IndexTransformBuilder<>(1, 1)
                .input_origin({-2})
                .input_shape({2})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{0},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({1})
                .output_identity_transform()
                .Finalize()
                .value()}));
}

// Tests that a 2-d identity-mapped input domain over `[0,30)*[0,30)` with a
// grid size of `{20,10}` is correctly partitioned in 6 parts, with domains:
// `[0,20)*[0,10)`, `[0,20)*[10,20)`, `[0,20)*[20,30)`, `[20,30)*[0,10)`,
// `[20,30)*[10,20)`, `[20,30)*[20,30)`,
TEST(PartitionIndexTransformOverRegularGrid, TwoDimensionalIdentity) {
  const auto results = GetPartitions({0, 1}, {20, 10},
                                     IndexTransformBuilder<>(2, 2)
                                         .input_origin({0, 0})
                                         .input_shape({30, 30})
                                         .output_identity_transform()
                                         .Finalize()
                                         .value());
  EXPECT_THAT(      //
      results,      //
      ElementsAre(  //
          R{{0, 0},
            IndexTransformBuilder<>(2, 2)
                .input_origin({0, 0})
                .input_shape({20, 10})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{0, 1},
            IndexTransformBuilder<>(2, 2)
                .input_origin({0, 10})
                .input_shape({20, 10})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{0, 2},
            IndexTransformBuilder<>(2, 2)
                .input_origin({0, 20})
                .input_shape({20, 10})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{1, 0},
            IndexTransformBuilder<>(2, 2)
                .input_origin({20, 0})
                .input_shape({10, 10})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{1, 1},
            IndexTransformBuilder<>(2, 2)
                .input_origin({20, 10})
                .input_shape({10, 10})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{1, 2},
            IndexTransformBuilder<>(2, 2)
                .input_origin({20, 20})
                .input_shape({10, 10})
                .output_identity_transform()
                .Finalize()
                .value()}));
}

// Same as previous test, but with non-unit stride and a cell size of 10.  The
// input domain `[-4,1]` is partitioned into 2 parts, with the domains `[-4,-2]`
// and `[-1,1]`.
TEST(PartitionIndexTransformOverRegularGrid, SingleStridedDimension) {
  const auto results =
      GetPartitions({0}, {10},
                    IndexTransformBuilder<>(1, 1)
                        .input_origin({-4})
                        .input_shape({6})
                        .output_single_input_dimension(0, 5, 3, 0)
                        .Finalize()
                        .value());
  // Input index:   -4  -3  -2  -1   0   1
  // Output index:  -7  -4  -1   2   5   8
  //  = 5 + 3 * Input index
  // Grid index:    -1  -1  -1   0   0   0
  //  = Output index / 10
  EXPECT_THAT(      //
      results,      //
      ElementsAre(  //
          R{{-1},
            IndexTransformBuilder<>(1, 1)
                .input_origin({-4})
                .input_shape({3})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{0},
            IndexTransformBuilder<>(1, 1)
                .input_origin({-1})
                .input_shape({3})
                .output_identity_transform()
                .Finalize()
                .value()}));
}

// Tests that a diagonal transform that maps two different gridded output
// dimension to a single input dimension, where a different cell size is used
// for the two grid dimensions, is partitioned into 3 parts, with domains
// `[-4,-2]`, `[-1,-1]`, and `[0,1]`.
TEST(PartitionIndexTransformOverRegularGrid, DiagonalStridedDimensions) {
  const auto results =
      GetPartitions({0, 1}, {10, 8},
                    IndexTransformBuilder<>(1, 2)
                        .input_origin({-4})
                        .input_shape({6})
                        .output_single_input_dimension(0, 5, 3, 0)
                        .output_single_input_dimension(1, 7, -2, 0)
                        .Finalize()
                        .value());
  // Input index:     -4  -3  -2  -1   0   1
  //
  // Output index 0:  -7  -4  -1   2   5   8
  //  = 5 + 3 * Input index 0
  // Grid index 0:    -1  -1  -1   0   0   0
  //  = Output index 0 / 10
  //
  // Output index 1:  15  13  11   9   7   5
  //  = 7 - 2 * Input index 1
  // Grid index 0:     1   1   1   1   0   0
  //  = Output index 1 / 8
  EXPECT_THAT(      //
      results,      //
      ElementsAre(  //
          R{{-1, 1},
            IndexTransformBuilder<>(1, 1)
                .input_origin({-4})
                .input_shape({3})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{0, 1},
            IndexTransformBuilder<>(1, 1)
                .input_origin({-1})
                .input_shape({1})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{0, 0},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({2})
                .output_identity_transform()
                .Finalize()
                .value()}));
}

// Tests that a transform that maps via an index array the domain `[100,107]` ->
// `[1,8]`, when partitioned using a grid cell size of 3, results in 3 parts
// with domains: {100, 101}, {102, 103, 104}, and {105, 106, 107}.
TEST(PartitionIndexTransformOverRegularGrid, SingleIndexArrayDimension) {
  const auto results =
      GetPartitions({0}, {3},
                    IndexTransformBuilder<>(1, 1)
                        .input_origin({100})
                        .input_shape({8})
                        .output_index_array(
                            0, 0, 1, MakeArray<Index>({1, 2, 3, 4, 5, 6, 7, 8}))
                        .Finalize()
                        .value());
  // Input index:  100 101 102 103 104 105 106 107
  // Index array :   1   2   3   4   5   6   7   8
  // Output index:   1   2   3   4   5   6   7   8
  // Grid index:     0   0   1   1   1   2   2   2
  EXPECT_THAT(  //
      results,  //
      ElementsAre(
          R{{0},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({2})
                .output_index_array(0, 0, 1, MakeArray<Index>({100, 101}))
                .Finalize()
                .value()},
          R{{1},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({3})
                .output_index_array(0, 0, 1, MakeArray<Index>({102, 103, 104}))
                .Finalize()
                .value()},
          R{{2},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({3})
                .output_index_array(0, 0, 1, MakeArray<Index>({105, 106, 107}))
                .Finalize()
                .value()}));
}

// Tests that a transform with a single gridded output dimension with an `array`
// map from a single input dimension with non-unit stride is correctly
// partitioned.
TEST(PartitionIndexTransformOverRegularGrid, SingleIndexArrayDimensionStrided) {
  const auto results = GetPartitions(
      {0}, {10},
      IndexTransformBuilder<>(1, 1)
          .input_origin({100})
          .input_shape({6})
          .output_index_array(0, 5, 3, MakeArray<Index>({10, 3, 4, -5, -6, 11}))
          .Finalize()
          .value());
  // Input index:  100 101 102 103 104 105
  // Index array:   10   3   4  -5  -6  11
  // Output index:  35  14  17 -10 -13  38
  //   = 5 + 3 * Index array
  // Grid index:     3   1   1  -1  -2   3
  //   = Output index / 3
  EXPECT_THAT(      //
      results,      //
      ElementsAre(  //
          R{{-2},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({1})
                .output_index_array(0, 0, 1, MakeArray<Index>({104}))
                .Finalize()
                .value()},
          R{{-1},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({1})
                .output_index_array(0, 0, 1, MakeArray<Index>({103}))
                .Finalize()
                .value()},
          R{{1},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({2})
                .output_index_array(0, 0, 1, MakeArray<Index>({101, 102}))
                .Finalize()
                .value()},
          R{{3},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({2})
                .output_index_array(0, 0, 1, MakeArray<Index>({100, 105}))
                .Finalize()
                .value()}));
}

// Tests that an index transform with two gridded output dimensions that are
// mapped using an `array` output index map from a single input dimension, which
// leads to a single connected set, is correctly handled.
TEST(PartitionIndexTransformOverRegularGrid, TwoIndexArrayDimensions) {
  const auto results = GetPartitions(
      {0, 1}, {10, 8},
      IndexTransformBuilder<>(1, 2)
          .input_origin({100})
          .input_shape({6})
          .output_index_array(0, 5, 3, MakeArray<Index>({10, 3, 4, -5, -6, 11}))
          .output_index_array(1, 4, -2, MakeArray<Index>({5, 1, 7, -3, -2, 5}))
          .Finalize()
          .value());
  // Input index:    100 101 102 103 104 105
  //
  // Index array 0:   10   3   4  -5  -6  11
  // Output index 0:  35  14  17 -10 -13  38
  //  = 5 + 3 * Index array 0
  // Grid index 0:     3   1   1  -1  -2   3
  //  = Output index 0 / 10
  //
  // Index array 1:    5   1   7  -3  -2   5
  // Output index 1:  -6   2 -10  10   8  -6
  //  = 4 - 2 * Index array 1
  // Grid index 1:    -1   0  -2   2   1  -1
  //  = Output index 1 / 8
  EXPECT_THAT(
      results,
      ElementsAre(
          R{{-2, 1},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({1})
                .output_index_array(0, 0, 1, MakeArray<Index>({104}))
                .Finalize()
                .value()},
          R{{-1, 1},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({1})
                .output_index_array(0, 0, 1, MakeArray<Index>({103}))
                .Finalize()
                .value()},
          R{{1, -2},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({1})
                .output_index_array(0, 0, 1, MakeArray<Index>({102}))
                .Finalize()
                .value()},
          R{{1, 0},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({1})
                .output_index_array(0, 0, 1, MakeArray<Index>({101}))
                .Finalize()
                .value()},
          R{{3, -1},
            IndexTransformBuilder<>(1, 1)
                .input_origin({0})
                .input_shape({2})
                .output_index_array(0, 0, 1, MakeArray<Index>({100, 105}))
                .Finalize()
                .value()}));
}

// Tests that a index transform with a gridded `array` output dimension that
// depends on one input dimension, and a gridded `single_input_dimension` output
// dimension that depends on the other input dimension, which leads to two
// connected sets, is handled correctly.
TEST(PartitionIndexTransformOverRegularGrid, IndexArrayAndStridedDimensions) {
  const auto results = GetPartitions(
      {0, 1}, {10, 8},
      IndexTransformBuilder<>(2, 2)
          .input_origin({-4, 100})
          .input_shape({6, 3})
          .output_index_array(0, 5, 3, MakeArray<Index>({{10, 3, 4}}))
          .output_single_input_dimension(1, 4, -2, 0)
          .Finalize()
          .value());

  // Input index 1:  100 101 102
  // Index array 0:   10   3   4
  // Output index 0:  35  14  17
  //  = 5 + 3 * Index array 0
  // Grid index 0:     3   1   1
  //  = Output index 0 / 10
  //
  // Input index 0:   -4  -3  -2  -1   0   1
  // Output index 1:  12  10   8   6   4   2
  //  = 4 - 2 * Input index 0
  // Grid index 1:     1   1   1   0   0   0
  //  = Output index 1 / 8
  EXPECT_THAT(
      results,
      ElementsAre(
          R{{1, 1},
            IndexTransformBuilder<>(2, 2)
                .input_origin({0, -4})
                .input_shape({2, 3})
                .output_single_input_dimension(0, 1)
                .output_index_array(1, 0, 1, MakeArray<Index>({{101}, {102}}))
                .Finalize()
                .value()},
          R{{1, 0},
            IndexTransformBuilder<>(2, 2)
                .input_origin({0, -1})
                .input_shape({2, 3})
                .output_single_input_dimension(0, 1)
                .output_index_array(1, 0, 1, MakeArray<Index>({{101}, {102}}))
                .Finalize()
                .value()},
          R{{3, 1},
            IndexTransformBuilder<>(2, 2)
                .input_origin({0, -4})
                .input_shape({1, 3})
                .output_single_input_dimension(0, 1)
                .output_index_array(1, 0, 1, MakeArray<Index>({{100}}))
                .Finalize()
                .value()},
          R{{3, 0},
            IndexTransformBuilder<>(2, 2)
                .input_origin({0, -1})
                .input_shape({1, 3})
                .output_single_input_dimension(0, 1)
                .output_index_array(1, 0, 1, MakeArray<Index>({{100}}))
                .Finalize()
                .value()}));
}

/// Returns the list of partitions generated by
/// `PartitionIndexTransformOverRegularGrid` when called with the specified
/// arguments.
///
/// \param grid_output_dimensions The sequence of output dimensions of the index
///     space "output" corresponding to the grid.
/// \param grid_cell_shape Array of length `grid_output_dimensions.size()`
///     specifying the cell of a grid cell along each grid dimension.
/// \param transform A transform from the "full" input space to the "output"
///     index space.
/// \returns The list of partitions.
std::vector<R> GetIrregularPartitions(
    const std::vector<DimensionIndex>& grid_output_dimensions,
    const IrregularGrid& grid, IndexTransformView<> transform) {
  std::vector<R> results;
  TENSORSTORE_CHECK_OK(tensorstore::internal::PartitionIndexTransformOverGrid(
      grid_output_dimensions, grid, transform,
      [&](tensorstore::span<const Index> grid_cell_indices,
          IndexTransformView<> cell_transform) {
        results.emplace_back(std::vector<Index>(grid_cell_indices.begin(),
                                                grid_cell_indices.end()),
                             IndexTransform<>(cell_transform));
        return absl::OkStatus();
      }));
  return results;
}

// Tests that a 2-d identity-mapped input domain over `[0,30)*[0,30)`
TEST(PartitionIndexTransformOverIrregularGrid, TwoDimensionalIdentity) {
  const std::vector<DimensionIndex> grid_output_dimensions{0, 1};
  std::vector<Index> dimension0{15};            // single split point
  std::vector<Index> dimension1{-10, 10, 100};  // multiple split points
  IrregularGrid grid({dimension0, dimension1});

  std::vector<R> results =
      GetIrregularPartitions(grid_output_dimensions, grid,
                             IndexTransformBuilder<>(2, 2)
                                 .input_origin({0, 0})
                                 .input_shape({30, 30})
                                 .output_identity_transform()
                                 .Finalize()
                                 .value());

  // According to SimpleIrregularGrid, indices < 0 are below the minimum bound
  // and in  real code could be clipped.
  EXPECT_THAT(      //
      results,      //
      ElementsAre(  //
          R{{-1, 0},
            IndexTransformBuilder<>(2, 2)
                .input_origin({0, 0})
                .input_shape({15, 10})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{-1, 1},
            IndexTransformBuilder<>(2, 2)
                .input_origin({0, 10})
                .input_shape({15, 20})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{0, 0},
            IndexTransformBuilder<>(2, 2)
                .input_origin({15, 0})
                .input_shape({15, 10})
                .output_identity_transform()
                .Finalize()
                .value()},
          R{{0, 1},
            IndexTransformBuilder<>(2, 2)
                .input_origin({15, 10})
                .input_shape({15, 20})
                .output_identity_transform()
                .Finalize()
                .value()}  //
          ));
}

TEST(PartitionIndexTransformOverIrregularGrid, IndexArrayAndStridedDimensions) {
  std::vector<Index> dimension0{10, 15, 20, 30, 50};
  std::vector<Index> dimension1{0, 1, 5, 10, 13};
  IrregularGrid grid({dimension0, dimension1});

  std::vector<R> results = GetIrregularPartitions(
      {0, 1}, grid,
      IndexTransformBuilder<>(2, 2)
          .input_origin({-4, 100})
          .input_shape({6, 3})
          .output_index_array(0, 5, 3, MakeArray<Index>({{10, 3, 4}}))
          .output_single_input_dimension(1, 4, -2, 0)
          .Finalize()
          .value());

  // Input index 1:  100 101 102
  // Index array 0:   10   3   4
  // Output index 0:  35  14  17
  //  = 5 + 3 * Index array 0
  // Grid index 0:     3   0   1
  //
  // Input index 0:   -4  -3  -2  -1   0   1
  // Output index 1:  12  10   8   6   4   2
  //  = 4 - 2 * Input index 0
  // Grid index 1:     3   3   2   2   1   1
  EXPECT_THAT(
      results,
      ElementsAre(R{{0, 3},
                    IndexTransformBuilder<>(2, 2)
                        .input_origin({0, -4})
                        .input_shape({1, 2})
                        .output_single_input_dimension(0, 1)
                        .output_index_array(1, 0, 1, MakeArray<Index>({{101}}))
                        .Finalize()
                        .value()},
                  R{{0, 2},
                    IndexTransformBuilder<>(2, 2)
                        .input_origin({0, -2})
                        .input_shape({1, 2})
                        .output_single_input_dimension(0, 1)
                        .output_index_array(1, 0, 1, MakeArray<Index>({{101}}))
                        .Finalize()
                        .value()},
                  R{{0, 1},
                    IndexTransformBuilder<>(2, 2)
                        .input_origin({0, 0})
                        .input_shape({1, 2})
                        .output_single_input_dimension(0, 1)
                        .output_index_array(1, 0, 1, MakeArray<Index>({{101}}))
                        .Finalize()
                        .value()},

                  R{{1, 3},
                    IndexTransformBuilder<>(2, 2)
                        .input_origin({0, -4})
                        .input_shape({1, 2})
                        .output_single_input_dimension(0, 1)
                        .output_index_array(1, 0, 1, MakeArray<Index>({{102}}))
                        .Finalize()
                        .value()},
                  R{{1, 2},
                    IndexTransformBuilder<>(2, 2)
                        .input_origin({0, -2})
                        .input_shape({1, 2})
                        .output_single_input_dimension(0, 1)
                        .output_index_array(1, 0, 1, MakeArray<Index>({{102}}))
                        .Finalize()
                        .value()},
                  R{{1, 1},
                    IndexTransformBuilder<>(2, 2)
                        .input_origin({0, 0})
                        .input_shape({1, 2})
                        .output_single_input_dimension(0, 1)
                        .output_index_array(1, 0, 1, MakeArray<Index>({{102}}))
                        .Finalize()
                        .value()},

                  R{{3, 3},
                    IndexTransformBuilder<>(2, 2)
                        .input_origin({0, -4})
                        .input_shape({1, 2})
                        .output_single_input_dimension(0, 1)
                        .output_index_array(1, 0, 1, MakeArray<Index>({{100}}))
                        .Finalize()
                        .value()},
                  R{{3, 2},
                    IndexTransformBuilder<>(2, 2)
                        .input_origin({0, -2})
                        .input_shape({1, 2})
                        .output_single_input_dimension(0, 1)
                        .output_index_array(1, 0, 1, MakeArray<Index>({{100}}))
                        .Finalize()
                        .value()},
                  R{{3, 1},
                    IndexTransformBuilder<>(2, 2)
                        .input_origin({0, 0})
                        .input_shape({1, 2})
                        .output_single_input_dimension(0, 1)
                        .output_index_array(1, 0, 1, MakeArray<Index>({{100}}))
                        .Finalize()
                        .value()}  //
                  ));
}

}  // namespace partition_tests

namespace get_grid_cell_ranges_tests {

using R = Box<>;
Result<std::vector<R>> GetRanges(
    tensorstore::span<const DimensionIndex> grid_output_dimensions,
    BoxView<> grid_bounds, OutputToGridCellFn output_to_grid_cell,
    IndexTransformView<> transform) {
  std::vector<R> results;
  IndexTransformGridPartition grid_partition;
  TENSORSTORE_RETURN_IF_ERROR(PrePartitionIndexTransformOverGrid(
      transform, grid_output_dimensions, output_to_grid_cell, grid_partition));
  TENSORSTORE_RETURN_IF_ERROR(GetGridCellRanges(
      grid_output_dimensions, grid_bounds, output_to_grid_cell, transform,
      [&](BoxView<> bounds) -> absl::Status {
        results.emplace_back(bounds);
        return absl::OkStatus();
      }));
  return results;
}

TEST(GetGridCellRangesTest, Rank0) {
  EXPECT_THAT(GetRanges(/*grid_output_dimensions=*/{}, /*grid_bounds=*/{},
                        /*output_to_grid_cell=*/RegularGridRef{{}},
                        IndexTransformBuilder(0, 0).Finalize().value()),
              ::testing::Optional(ElementsAre(R{})));
}

TEST(GetGridCellRangesTest, Rank1Unconstrained) {
  EXPECT_THAT(GetRanges(/*grid_output_dimensions=*/{{0}},
                        /*grid_bounds=*/Box<>{{0}, {10}},
                        /*output_to_grid_cell=*/RegularGridRef{{{5}}},
                        IndexTransformBuilder(1, 1)
                            .input_shape({50})
                            .output_identity_transform()
                            .Finalize()
                            .value()),
              ::testing::Optional(ElementsAre(R{{0}, {10}})));
}

TEST(GetGridCellRangesTest, Rank1Constrained) {
  // Grid dimension 0:
  //   Output range: [7, 36]
  //   Grid range: [1, 7]
  EXPECT_THAT(GetRanges(/*grid_output_dimensions=*/{{0}},
                        /*grid_bounds=*/Box<>{{0}, {10}},
                        /*output_to_grid_cell=*/RegularGridRef{{{5}}},
                        IndexTransformBuilder(1, 1)
                            .input_origin({7})
                            .input_shape({30})
                            .output_identity_transform()
                            .Finalize()
                            .value()),
              ::testing::Optional(ElementsAre(R({1}, {7}))));
}

TEST(GetGridCellRangesTest, Rank2ConstrainedBothDims) {
  // Grid dimension 0:
  //   Output range: [6, 13]
  //   Grid range: [1, 2]
  // Grid dimension 1:
  //   Output range: [7, 37)
  //   Grid range: [0, 3]
  EXPECT_THAT(GetRanges(/*grid_output_dimensions=*/{{0, 1}},
                        /*grid_bounds=*/Box<>{{0, 0}, {5, 10}},
                        /*output_to_grid_cell=*/RegularGridRef{{{5, 10}}},
                        IndexTransformBuilder(2, 2)
                            .input_origin({6, 7})
                            .input_shape({8, 30})
                            .output_identity_transform()
                            .Finalize()
                            .value()),
              ::testing::Optional(ElementsAre(  //
                  R{{1, 0}, {1, 4}},            //
                  R{{2, 0}, {1, 4}}             //
                  )));
}

TEST(GetGridCellRangesTest, Rank2ConstrainedFirstDimOnly) {
  // Grid dimension 0:
  //   Output range: [6, 13]
  //   Grid range: [1, 2]
  // Grid dimension 1:
  //   Output range: [0, 49]
  //   Grid range: [0, 9] (unconstrained)
  EXPECT_THAT(GetRanges(/*grid_output_dimensions=*/{{0, 1}},
                        /*grid_bounds=*/Box<>{{0, 0}, {5, 10}},
                        /*output_to_grid_cell=*/RegularGridRef{{{5, 5}}},
                        IndexTransformBuilder(2, 2)
                            .input_origin({6, 0})
                            .input_shape({8, 50})
                            .output_identity_transform()
                            .Finalize()
                            .value()),
              ::testing::Optional(ElementsAre(R{{1, 0}, {2, 10}})));
}

TEST(GetGridCellRangesTest, Rank2ConstrainedSecondDimOnly) {
  // Grid dimension 0:
  //   Output range: [0, 24]
  //   Grid range: [0, 4] (unconstrained)
  // Grid dimension 1:
  //   Output range: [7, 36]
  //   Grid range: [1, 7]
  EXPECT_THAT(GetRanges(/*grid_output_dimensions=*/{{0, 1}},
                        /*grid_bounds=*/Box<>{{0, 0}, {5, 10}},
                        /*output_to_grid_cell=*/RegularGridRef{{{5, 5}}},
                        IndexTransformBuilder(2, 2)
                            .input_origin({0, 7})
                            .input_shape({25, 30})
                            .output_identity_transform()
                            .Finalize()
                            .value()),
              ::testing::Optional(ElementsAre(  //
                  R{{0, 1}, {1, 7}},            //
                  R{{1, 1}, {1, 7}},            //
                  R{{2, 1}, {1, 7}},            //
                  R{{3, 1}, {1, 7}},            //
                  R{{4, 1}, {1, 7}}             //
                  )));
}

TEST(GetGridCellRangesTest, Rank2IndexArrayFirstDimUnconstrainedSecondDim) {
  // Grid dimension 0:
  //   Output range: {6, 15, 20}
  //   Grid range: {1, 3, 4}
  // Grid dimension 1:
  //   Output range: [0, 49]
  //   Grid range: [0, 9] (unconstrained)
  EXPECT_THAT(
      GetRanges(
          /*grid_output_dimensions=*/{{0, 1}},
          /*grid_bounds=*/Box<>{{0, 0}, {5, 10}},
          /*output_to_grid_cell=*/RegularGridRef{{{5, 5}}},
          IndexTransformBuilder(2, 2)
              .input_origin({0, 0})
              .input_shape({3, 50})
              .output_index_array(0, 0, 1, MakeArray<Index>({{6}, {15}, {20}}))
              .output_single_input_dimension(1, 1)
              .Finalize()
              .value()),
      ::testing::Optional(ElementsAre(  //
          R{{1, 0}, {1, 10}},           //
          R{{3, 0}, {2, 10}}            //
          )));
}

TEST(GetGridCellRangesTest, Rank2IndexArrayFirstDimConstrainedSecondDim) {
  // Grid dimension 0:
  //   Output range: {6, 15, 20}
  //   Grid range: {1, 3, 4}
  // Grid dimension 1:
  //   Output range: [7, 36]
  //   Grid range: [1, 7]
  EXPECT_THAT(
      GetRanges(
          /*grid_output_dimensions=*/{{0, 1}},
          /*grid_bounds=*/Box<>{{0, 0}, {5, 10}},
          /*output_to_grid_cell=*/RegularGridRef{{{5, 5}}},
          IndexTransformBuilder(2, 2)
              .input_origin({0, 7})
              .input_shape({3, 30})
              .output_index_array(0, 0, 1, MakeArray<Index>({{6}, {15}, {20}}))
              .output_single_input_dimension(1, 1)
              .Finalize()
              .value()),
      // Since grid dimension 1 is constrained, a separate range is required for
      // each grid dimension 0 index.
      ::testing::Optional(ElementsAre(  //
          R{{1, 1}, {1, 7}},            //
          R{{3, 1}, {1, 7}},            //
          R{{4, 1}, {1, 7}}             //
          )));
}

TEST(GetGridCellRangesTest, Rank2Diagonal) {
  // Grid dimension 0:
  //   Output range: [6, 13]
  //   Grid range: [1, 2]
  // Grid dimension 1:
  //   Output range: [6, 13]
  //   Grid range: [0, 1]
  EXPECT_THAT(GetRanges(/*grid_output_dimensions=*/{{0, 1}},
                        /*grid_bounds=*/Box<>{{0, 0}, {5, 10}},
                        /*output_to_grid_cell=*/RegularGridRef{{{5, 10}}},
                        IndexTransformBuilder(1, 2)
                            .input_origin({6})
                            .input_shape({8})
                            .output_single_input_dimension(0, 0)
                            .output_single_input_dimension(1, 0)
                            .Finalize()
                            .value()),
              ::testing::Optional(ElementsAre(  //
                  R{{1, 0}, {1, 1}},            //
                  R{{2, 1}, {1, 1}}             //
                  )));
}

}  // namespace get_grid_cell_ranges_tests

}  // namespace
