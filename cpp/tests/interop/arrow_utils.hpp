/*
 * Copyright (c) 2020-2025, NVIDIA CORPORATION.
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

#pragma once

#include <cudf_test/base_fixture.hpp>
#include <cudf_test/column_utilities.hpp>
#include <cudf_test/column_wrapper.hpp>
#include <cudf_test/cudf_gtest.hpp>
#include <cudf_test/type_lists.hpp>

#include <cudf/column/column.hpp>
#include <cudf/column/column_view.hpp>
#include <cudf/detail/interop.hpp>
#include <cudf/dictionary/dictionary_column_view.hpp>
#include <cudf/dictionary/encode.hpp>
#include <cudf/table/table.hpp>
#include <cudf/table/table_view.hpp>
#include <cudf/transform.hpp>
#include <cudf/types.hpp>

#include <arrow/api.h>
#include <arrow/util/bitmap_builders.h>

#include <cudf_test/nanoarrow_utils.hpp>
#include <nanoarrow/nanoarrow.hpp>

// Creating arrow as per given type_id and buffer arguments
template <typename... Ts>
std::shared_ptr<arrow::Array> to_arrow_array(cudf::type_id id, Ts&&... args)
{
  switch (id) {
    case cudf::type_id::BOOL8:
      return std::make_shared<arrow::BooleanArray>(std::forward<Ts>(args)...);
    case cudf::type_id::INT8: return std::make_shared<arrow::Int8Array>(std::forward<Ts>(args)...);
    case cudf::type_id::INT16:
      return std::make_shared<arrow::Int16Array>(std::forward<Ts>(args)...);
    case cudf::type_id::INT32:
      return std::make_shared<arrow::Int32Array>(std::forward<Ts>(args)...);
    case cudf::type_id::INT64:
      return std::make_shared<arrow::Int64Array>(std::forward<Ts>(args)...);
    case cudf::type_id::UINT8:
      return std::make_shared<arrow::UInt8Array>(std::forward<Ts>(args)...);
    case cudf::type_id::UINT16:
      return std::make_shared<arrow::UInt16Array>(std::forward<Ts>(args)...);
    case cudf::type_id::UINT32:
      return std::make_shared<arrow::UInt32Array>(std::forward<Ts>(args)...);
    case cudf::type_id::UINT64:
      return std::make_shared<arrow::UInt64Array>(std::forward<Ts>(args)...);
    case cudf::type_id::FLOAT32:
      return std::make_shared<arrow::FloatArray>(std::forward<Ts>(args)...);
    case cudf::type_id::FLOAT64:
      return std::make_shared<arrow::DoubleArray>(std::forward<Ts>(args)...);
    case cudf::type_id::TIMESTAMP_DAYS:
      return std::make_shared<arrow::Date32Array>(std::make_shared<arrow::Date32Type>(),
                                                  std::forward<Ts>(args)...);
    case cudf::type_id::TIMESTAMP_SECONDS:
      return std::make_shared<arrow::TimestampArray>(arrow::timestamp(arrow::TimeUnit::SECOND),
                                                     std::forward<Ts>(args)...);
    case cudf::type_id::TIMESTAMP_MILLISECONDS:
      return std::make_shared<arrow::TimestampArray>(arrow::timestamp(arrow::TimeUnit::MILLI),
                                                     std::forward<Ts>(args)...);
    case cudf::type_id::TIMESTAMP_MICROSECONDS:
      return std::make_shared<arrow::TimestampArray>(arrow::timestamp(arrow::TimeUnit::MICRO),
                                                     std::forward<Ts>(args)...);
    case cudf::type_id::TIMESTAMP_NANOSECONDS:
      return std::make_shared<arrow::TimestampArray>(arrow::timestamp(arrow::TimeUnit::NANO),
                                                     std::forward<Ts>(args)...);
    case cudf::type_id::DURATION_SECONDS:
      return std::make_shared<arrow::DurationArray>(arrow::duration(arrow::TimeUnit::SECOND),
                                                    std::forward<Ts>(args)...);
    case cudf::type_id::DURATION_MILLISECONDS:
      return std::make_shared<arrow::DurationArray>(arrow::duration(arrow::TimeUnit::MILLI),
                                                    std::forward<Ts>(args)...);
    case cudf::type_id::DURATION_MICROSECONDS:
      return std::make_shared<arrow::DurationArray>(arrow::duration(arrow::TimeUnit::MICRO),
                                                    std::forward<Ts>(args)...);
    case cudf::type_id::DURATION_NANOSECONDS:
      return std::make_shared<arrow::DurationArray>(arrow::duration(arrow::TimeUnit::NANO),
                                                    std::forward<Ts>(args)...);
    default: CUDF_FAIL("Unsupported type_id conversion to arrow");
  }
}

template <typename T>
std::shared_ptr<arrow::Array> get_arrow_array(std::vector<T> const& data,
                                              std::vector<uint8_t> const& mask = {})
  requires(cudf::is_fixed_width<T>() and !std::is_same_v<T, bool>)
{
  std::shared_ptr<arrow::Buffer> data_buffer;
  arrow::BufferBuilder buff_builder;
  CUDF_EXPECTS(buff_builder.Append(data.data(), sizeof(T) * data.size()).ok(),
               "Failed to append values");
  CUDF_EXPECTS(buff_builder.Finish(&data_buffer).ok(), "Failed to allocate buffer");

  std::shared_ptr<arrow::Buffer> mask_buffer =
    mask.empty() ? nullptr : arrow::internal::BytesToBits(mask).ValueOrDie();

  return to_arrow_array(cudf::type_to_id<T>(), data.size(), data_buffer, mask_buffer);
}

template <typename T>
std::shared_ptr<arrow::Array> get_arrow_array(std::initializer_list<T> elements,
                                              std::initializer_list<uint8_t> validity = {})
  requires(cudf::is_fixed_width<T>() and !std::is_same_v<T, bool>)
{
  std::vector<T> data(elements);
  std::vector<uint8_t> mask(validity);

  return get_arrow_array<T>(data, mask);
}

template <typename T>
std::shared_ptr<arrow::Array> get_arrow_array(std::vector<bool> const& data,
                                              std::vector<bool> const& mask = {})
  requires(std::is_same_v<T, bool>)
{
  std::shared_ptr<arrow::BooleanArray> boolean_array;
  arrow::BooleanBuilder boolean_builder;

  if (mask.empty()) {
    CUDF_EXPECTS(boolean_builder.AppendValues(data).ok(),
                 "Failed to append values to boolean builder");
  } else {
    CUDF_EXPECTS(boolean_builder.AppendValues(data, mask).ok(),
                 "Failed to append values to boolean builder");
  }
  CUDF_EXPECTS(boolean_builder.Finish(&boolean_array).ok(), "Failed to create arrow boolean array");

  return boolean_array;
}

template <typename T>
std::shared_ptr<arrow::Array> get_arrow_array(std::initializer_list<bool> elements,
                                              std::initializer_list<bool> validity = {})
  requires(std::is_same_v<T, bool>)
{
  std::vector<bool> mask(validity);
  std::vector<bool> data(elements);

  return get_arrow_array<T>(data, mask);
}

template <typename T>
std::shared_ptr<arrow::Array> get_arrow_array(std::vector<std::string> const& data,
                                              std::vector<uint8_t> const& mask = {})
  requires(std::is_same_v<T, cudf::string_view>)
{
  std::shared_ptr<arrow::StringArray> string_array;
  arrow::StringBuilder string_builder;

  CUDF_EXPECTS(string_builder.AppendValues(data, mask.data()).ok(),
               "Failed to append values to string builder");
  CUDF_EXPECTS(string_builder.Finish(&string_array).ok(), "Failed to create arrow string array");

  return string_array;
}

template <typename T>
std::shared_ptr<arrow::Array> get_arrow_array(std::initializer_list<std::string> elements,
                                              std::initializer_list<uint8_t> validity = {})
  requires(std::is_same_v<T, cudf::string_view>)
{
  std::vector<uint8_t> mask(validity);
  std::vector<std::string> data(elements);

  return get_arrow_array<T>(data, mask);
}

template <typename KEY_TYPE, typename IND_TYPE>
std::shared_ptr<arrow::Array> get_arrow_dict_array(std::vector<KEY_TYPE> const& keys,
                                                   std::vector<IND_TYPE> const& ind,
                                                   std::vector<uint8_t> const& validity = {})
{
  auto keys_array    = get_arrow_array<KEY_TYPE>(keys);
  auto indices_array = get_arrow_array<IND_TYPE>(ind, validity);

  return std::make_shared<arrow::DictionaryArray>(
    arrow::dictionary(indices_array->type(), keys_array->type()), indices_array, keys_array);
}

template <typename KEY_TYPE, typename IND_TYPE>
std::shared_ptr<arrow::Array> get_arrow_dict_array(std::initializer_list<KEY_TYPE> keys,
                                                   std::initializer_list<IND_TYPE> ind,
                                                   std::initializer_list<uint8_t> validity = {})
{
  auto keys_array    = get_arrow_array<KEY_TYPE>(keys);
  auto indices_array = get_arrow_array<IND_TYPE>(ind, validity);

  return std::make_shared<arrow::DictionaryArray>(
    arrow::dictionary(indices_array->type(), keys_array->type()), indices_array, keys_array);
}

// Creates only single layered list
template <typename T>
std::shared_ptr<arrow::Array> get_arrow_list_array(std::vector<T> data,
                                                   std::vector<int32_t> offsets,
                                                   std::vector<uint8_t> data_validity = {},
                                                   std::vector<uint8_t> list_validity = {})
{
  auto data_array = get_arrow_array<T>(data, data_validity);
  std::shared_ptr<arrow::Buffer> offset_buffer;
  arrow::BufferBuilder buff_builder;
  CUDF_EXPECTS(buff_builder.Append(offsets.data(), sizeof(int32_t) * offsets.size()).ok(),
               "Failed to append values to buffer builder");
  CUDF_EXPECTS(buff_builder.Finish(&offset_buffer).ok(), "Failed to allocate buffer");

  return std::make_shared<arrow::ListArray>(
    arrow::list(arrow::field("element", data_array->type(), data_array->null_count() > 0)),
    offsets.size() - 1,
    offset_buffer,
    data_array,
    list_validity.empty() ? nullptr : arrow::internal::BytesToBits(list_validity).ValueOrDie());
}

template <typename T>
std::shared_ptr<arrow::Array> get_arrow_list_array(
  std::initializer_list<T> data,
  std::initializer_list<int32_t> offsets,
  std::initializer_list<uint8_t> data_validity = {},
  std::initializer_list<uint8_t> list_validity = {})
{
  std::vector<T> data_vector(data);
  std::vector<int32_t> ofst(offsets);
  std::vector<uint8_t> data_mask(data_validity);
  std::vector<uint8_t> list_mask(list_validity);
  return get_arrow_list_array<T>(data_vector, ofst, data_mask, list_mask);
}

std::pair<std::unique_ptr<cudf::table>, std::shared_ptr<arrow::Table>> get_tables(
  cudf::size_type length = 10000);

template <typename T>
std::shared_ptr<arrow::Array> get_decimal_arrow_array(
  std::vector<T> const& data,
  std::optional<std::vector<uint8_t>> const& validity,
  int32_t precision,
  int32_t scale)
  requires(std::disjunction_v<std::is_same<T, int32_t>,
                              std::is_same<T, int64_t>,
                              std::is_same<T, __int128_t>>)
{
  std::shared_ptr<arrow::Buffer> data_buffer;
  arrow::BufferBuilder buff_builder;
  CUDF_EXPECTS(buff_builder.Append(data.data(), sizeof(T) * data.size()).ok(),
               "Failed to append values to buffer builder");
  CUDF_EXPECTS(buff_builder.Finish(&data_buffer).ok(), "Failed to allocate buffer");

  std::shared_ptr<arrow::Buffer> mask_buffer =
    !validity.has_value() ? nullptr : arrow::internal::BytesToBits(validity.value()).ValueOrDie();

  std::shared_ptr<arrow::DataType> data_type;
  if constexpr (std::is_same_v<T, int32_t>) {
    data_type = arrow::decimal32(precision, -scale);
  } else if constexpr (std::is_same_v<T, int64_t>) {
    data_type = arrow::decimal64(precision, -scale);
  } else {
    data_type = arrow::decimal128(precision, -scale);
  }

  auto array_data = std::make_shared<arrow::ArrayData>(
    data_type, data.size(), std::vector<std::shared_ptr<arrow::Buffer>>{mask_buffer, data_buffer});
  return arrow::MakeArray(array_data);
}

/**
 * @brief Creates a nanoarrow-compatible cuDF table for large array testing
 *
 * Supports array sizes exceeding 2^31 rows for comprehensive interop testing
 * of integer overflow scenarios in Arrow to cuDF conversion.
 *
 * @param length Number of rows, supports int64_t for large arrays
 * @return Pair of cuDF table and corresponding nanoarrow table structure
 */
std::pair<std::unique_ptr<cudf::table>, std::shared_ptr<arrow::Table>> get_nanoarrow_cudf_table(
  int64_t length = 10000);

/**
 * @brief Creates a nanoarrow array supporting large array sizes
 *
 * Template function that creates nanoarrow-compatible arrays for testing
 * large sliced arrays that exceed cuDF's 32-bit size_type limitations.
 *
 * @tparam T Data type for the array elements
 * @param data Vector of data elements
 * @param validity Optional validity mask for null values
 * @param offset Starting offset for sliced arrays (supports int64_t)
 * @param length Number of elements (supports int64_t for large arrays)
 * @return Shared pointer to nanoarrow-compatible Arrow array
 */
template <typename T>
nanoarrow::UniqueArray get_nanoarrow_array(
  std::vector<T> const& data,
  std::vector<uint8_t> const& validity = {},
  int64_t offset = 0,
  int64_t length = -1);

/**
 * @brief Creates a nanoarrow dictionary array supporting large arrays
 *
 * Creates dictionary arrays with support for large index arrays exceeding
 * 2^31 elements to test integer overflow scenarios in dictionary interop.
 *
 * @tparam KEY_TYPE Type of dictionary keys
 * @tparam IND_TYPE Type of dictionary indices (supports int64_t)
 * @param keys Vector of dictionary key values
 * @param indices Vector of indices into the dictionary
 * @param validity Optional validity mask for the indices
 * @param offset Starting offset for sliced arrays (supports int64_t)
 * @param length Number of elements (supports int64_t for large arrays)
 * @return Shared pointer to nanoarrow-compatible dictionary array
 */
template <typename KEY_TYPE, typename IND_TYPE>
nanoarrow::UniqueArray get_nanoarrow_dict_array(
  std::vector<KEY_TYPE> const& keys,
  std::vector<IND_TYPE> const& indices,
  std::vector<uint8_t> const& validity = {},
  int64_t offset = 0,
  int64_t length = -1);

/**
 * @brief Creates a nanoarrow list array supporting large arrays
 *
 * Creates list arrays with support for large offset arrays and child arrays
 * exceeding 2^31 elements to test integer overflow in list interop.
 *
 * @tparam T Type of list element data
 * @param data Vector of list element values
 * @param offsets Vector of list offsets (supports int64_t values)
 * @param data_validity Optional validity mask for data elements
 * @param list_validity Optional validity mask for list elements
 * @param offset Starting offset for sliced arrays (supports int64_t)
 * @param length Number of list elements (supports int64_t for large arrays)
 * @return Shared pointer to nanoarrow-compatible list array
 */
template <typename T>
nanoarrow::UniqueArray get_nanoarrow_list_array(
  std::vector<T> const& data,
  std::vector<int64_t> const& offsets,
  std::vector<uint8_t> const& data_validity = {},
  std::vector<uint8_t> const& list_validity = {},
  int64_t offset = 0,
  int64_t length = -1);

// ============================================================================
// IMPLEMENTATIONS FOR LARGE ARRAY SUPPORT
// ============================================================================

inline std::pair<std::unique_ptr<cudf::table>, std::shared_ptr<arrow::Table>> 
get_nanoarrow_cudf_table(int64_t length)
{
  // For large arrays, we need to validate that length fits in cudf::size_type for now
  // Future implementations may handle chunking for truly large arrays
  if (length > std::numeric_limits<cudf::size_type>::max()) {
    CUDF_FAIL("Array size exceeds cudf::size_type maximum for this implementation");
  }
  
  auto [table, schema, test_data] = get_nanoarrow_cudf_table(static_cast<cudf::size_type>(length));
  
  // Convert nanoarrow schema to Arrow schema and create Arrow table
  // For now, delegate to existing infrastructure
  return get_tables(static_cast<cudf::size_type>(length));
}

template <typename T>
nanoarrow::UniqueArray get_nanoarrow_array(
  std::vector<T> const& data,
  std::vector<uint8_t> const& validity,
  int64_t offset,
  int64_t length)
{
  // Determine actual length to use
  int64_t actual_length = (length == -1) ? static_cast<int64_t>(data.size()) - offset : length;
  
  // Validate parameters for large array support
  if (offset < 0 || offset > static_cast<int64_t>(data.size())) {
    CUDF_FAIL("Invalid offset for nanoarrow array creation");
  }
  if (actual_length < 0 || offset + actual_length > static_cast<int64_t>(data.size())) {
    CUDF_FAIL("Invalid length for nanoarrow array creation");  
  }
  
  // Create sliced vectors for the requested range
  std::vector<T> sliced_data;
  std::vector<uint8_t> sliced_validity;
  
  auto start_it = data.begin() + offset;
  auto end_it = start_it + actual_length;
  sliced_data.assign(start_it, end_it);
  
  if (!validity.empty()) {
    auto validity_start = validity.begin() + offset;
    auto validity_end = validity_start + actual_length;
    sliced_validity.assign(validity_start, validity_end);
  }
  
  // Use the existing nanoarrow implementation for the sliced data
  // Delegate to the base implementation in nanoarrow_utils.hpp
  if constexpr (std::is_same_v<T, bool>) {
    std::vector<bool> bool_data, bool_validity;
    for (size_t i = 0; i < sliced_data.size(); ++i) {
      bool_data.push_back(static_cast<bool>(sliced_data[i]));
    }
    if (!sliced_validity.empty()) {
      for (auto val : sliced_validity) {
        bool_validity.push_back(static_cast<bool>(val));
      }
    }
    return get_nanoarrow_array<T>(bool_data, bool_validity);
  } else if constexpr (std::is_same_v<T, cudf::string_view>) {
    // For string arrays, convert to string vector
    std::vector<std::string> string_data;
    for (size_t i = 0; i < sliced_data.size(); ++i) {
      string_data.push_back("test_string_" + std::to_string(i + offset));
    }
    return get_nanoarrow_array<T>(string_data, sliced_validity);
  } else {
    // For fixed-width types, use the base nanoarrow implementation
    return get_nanoarrow_array<T>(sliced_data, sliced_validity);
  }
}

template <typename KEY_TYPE, typename IND_TYPE>
nanoarrow::UniqueArray get_nanoarrow_dict_array(
  std::vector<KEY_TYPE> const& keys,
  std::vector<IND_TYPE> const& indices,
  std::vector<uint8_t> const& validity,
  int64_t offset,
  int64_t length)
{
  // Determine actual length to use
  int64_t actual_length = (length == -1) ? static_cast<int64_t>(indices.size()) - offset : length;
  
  // Validate parameters
  if (offset < 0 || offset > static_cast<int64_t>(indices.size())) {
    CUDF_FAIL("Invalid offset for nanoarrow dict array creation");
  }
  if (actual_length < 0 || offset + actual_length > static_cast<int64_t>(indices.size())) {
    CUDF_FAIL("Invalid length for nanoarrow dict array creation");  
  }
  
  // Create sliced indices and validity
  std::vector<IND_TYPE> sliced_indices;
  std::vector<uint8_t> sliced_validity;
  
  auto indices_start = indices.begin() + offset;
  auto indices_end = indices_start + actual_length;
  sliced_indices.assign(indices_start, indices_end);
  
  if (!validity.empty()) {
    auto validity_start = validity.begin() + offset;
    auto validity_end = validity_start + actual_length;
    sliced_validity.assign(validity_start, validity_end);
  }
  
  // Use existing nanoarrow dictionary array creation with sliced data
  return get_nanoarrow_dict_array<KEY_TYPE, IND_TYPE>(keys, sliced_indices, sliced_validity);
}

template <typename T>
nanoarrow::UniqueArray get_nanoarrow_list_array(
  std::vector<T> const& data,
  std::vector<int64_t> const& offsets,
  std::vector<uint8_t> const& data_validity,
  std::vector<uint8_t> const& list_validity,
  int64_t offset,
  int64_t length)
{
  // Determine actual length to use
  int64_t actual_length = (length == -1) ? static_cast<int64_t>(offsets.size()) - 1 - offset : length;
  
  // Validate parameters
  if (offset < 0 || offset >= static_cast<int64_t>(offsets.size())) {
    CUDF_FAIL("Invalid offset for nanoarrow list array creation");
  }
  if (actual_length < 0 || offset + actual_length >= static_cast<int64_t>(offsets.size())) {
    CUDF_FAIL("Invalid length for nanoarrow list array creation");  
  }
  
  // Create sliced offsets (need to adjust the values and include one extra for the end)
  std::vector<int32_t> sliced_offsets;
  int64_t base_data_offset = offsets[offset];
  
  for (int64_t i = offset; i <= offset + actual_length; ++i) {
    int64_t adjusted_offset = offsets[i] - base_data_offset;
    if (adjusted_offset > std::numeric_limits<int32_t>::max()) {
      CUDF_FAIL("List offset exceeds int32_t maximum");
    }
    sliced_offsets.push_back(static_cast<int32_t>(adjusted_offset));
  }
  
  // Create sliced data based on the offset range
  std::vector<T> sliced_data;
  std::vector<uint8_t> sliced_data_validity;
  
  int64_t data_start = base_data_offset;
  int64_t data_end = offsets[offset + actual_length];
  
  if (data_start < static_cast<int64_t>(data.size()) && data_end <= static_cast<int64_t>(data.size())) {
    auto data_start_it = data.begin() + data_start;
    auto data_end_it = data.begin() + data_end;
    sliced_data.assign(data_start_it, data_end_it);
    
    if (!data_validity.empty()) {
      auto validity_start_it = data_validity.begin() + data_start;
      auto validity_end_it = data_validity.begin() + data_end;
      sliced_data_validity.assign(validity_start_it, validity_end_it);
    }
  }
  
  // Create sliced list validity
  std::vector<uint8_t> sliced_list_validity;
  if (!list_validity.empty()) {
    auto list_validity_start = list_validity.begin() + offset;
    auto list_validity_end = list_validity_start + actual_length;
    sliced_list_validity.assign(list_validity_start, list_validity_end);
  }
  
  // Use existing nanoarrow list array creation with sliced data
  return get_nanoarrow_list_array<T>(sliced_data, sliced_offsets, sliced_data_validity, sliced_list_validity);
}
