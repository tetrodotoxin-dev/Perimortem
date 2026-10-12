// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#include "perimortem/serialization/png.hpp"

#include "perimortem/compression/deflate.hpp"
#include "perimortem/core/algorithm/search.hpp"
#include "perimortem/core/data.hpp"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/null_terminated.hpp"
#include "perimortem/core/scalar.hpp"
#include "perimortem/core/static/bytes.hpp"
#include "perimortem/core/static/vector.hpp"
#include "perimortem/memory/matrix.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::Compression;
using namespace Perimortem::Memory;
using namespace Perimortem::Math;
using namespace Perimortem::Serialization;

// Values are the wire codes stored in IHDR. Unknown codes map to zero channels
// and are rejected before allocating pixel storage.
enum class ColorType : U8 {
  Greyscale = 0,
  Rgb = 2,
  Indexed = 3,
  GreyscaleAlpha = 4,
  Rgba = 6,
};

// Prediction applied to each row before DEFLATE. Each value matches the filter
// byte written at the start of the filtered row per the PNG specification.
enum class FilterType : U8 {
  None = 0,
  Sub = 1,
  Up = 2,
  Average = 3,
  Paeth = 4,
};

// ImageInfo stores the 13 byte IHDR payload at the beginning of its native
// record. Width and height remain in big endian storage while accessors expose
// native values. `size` excludes trailing C++ padding so chunk reads and writes
// never serialize it. A default record has zero dimensions and represents
// header admission failure.
struct ImageInfo {
 public:
  constexpr ImageInfo() = default;
  constexpr ImageInfo(const Matrix<Rgba8>& image) {
    Data::write<Data::ByteOrder::Big>(&width, image.get_column_count());
    Data::write<Data::ByteOrder::Big>(&height, image.get_row_count());
    bit_depth = 8;
    color_type = ColorType::Rgba;
  }

  static constexpr Count size = 13;

  constexpr auto get_width() const -> U32 {
    return Data::ensure_endian<Data::ByteOrder::Big, Data::ByteOrder::Native>(
        width);
  }

  constexpr auto get_height() const -> U32 {
    return Data::ensure_endian<Data::ByteOrder::Big, Data::ByteOrder::Native>(
        height);
  }

  constexpr auto get_bit_depth() const -> U8 { return bit_depth; }
  constexpr auto get_color_type() const -> ColorType { return color_type; }
  constexpr auto uses_standard_methods() const -> Bool {
    return compression_method == 0 && filter_method == FilterType::None &&
           interlace_method == 0;
  }

 private:
  U32 width = 0;
  U32 height = 0;
  U8 bit_depth = 0;
  ColorType color_type = ColorType::Rgba;
  U8 compression_method = 0;
  FilterType filter_method = FilterType::None;
  U8 interlace_method = 0;
};

// A parsed PNG chunk. PNG files are made of sequential chunks, each with a
// type tag, a payload, and a CRC32 checksum for integrity verification.
class Chunk {
 public:
  constexpr Chunk() = default;
  constexpr Chunk(View::Bytes data, Static::Bytes<4> type, U32 length)
      : data(data), type(type), length(length), valid(True) {}

  constexpr auto get_data() const -> View::Bytes { return data; }
  constexpr auto get_type() const -> View::Bytes { return type.get_view(); }
  constexpr auto get_length() const -> U32 { return length; }
  constexpr auto get_valid() const -> Bool { return valid; }

 private:
  View::Bytes data{};
  Static::Bytes<4> type;
  U32 length = 0;
  Bool valid = False;
};

// The eight byte magic signature that begins every valid PNG file.
constexpr Static::Bytes<8> png_signature = {
  {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}};

// Each chunk: 4 bytes length + 4 bytes type + data + 4 bytes CRC32.
constexpr Count chunk_metadata_size = 12;

constexpr auto number_of_color_channels(ColorType color_type) -> Count {
  switch (color_type) {
  case ColorType::Indexed:
  case ColorType::Greyscale:
    return 1;
  case ColorType::GreyscaleAlpha:
    return 2;
  case ColorType::Rgb:
    return 3;
  case ColorType::Rgba:
    return 4;
  default:
    return 0;
  }
}

constexpr auto populate_crc_table() -> Static::Vector<U32, 256> {
  // CRC32 (Cyclic Redundancy Check): a 32 bit error detection hash used by the
  // PNG specification to verify each chunk has not been corrupted in transit.
  // The IEEE 802.3 polynomial (0xEDB88320) is the standard choice for CRC32.
  constexpr U32 ieee_crc32_polynomial = 0xEDB88320;

  Static::Vector<U32, 256> table;
  for (U32 table_index = 0; table_index < 256; table_index++) {
    U32 crc_value = table_index;
    for (Count bit_index = 0; bit_index < 8; bit_index++) {
      crc_value = (crc_value & 1) ? (ieee_crc32_polynomial ^ (crc_value >> 1))
                                  : (crc_value >> 1);
    }

    table[table_index] = crc_value;
  }

  return table;
}

constexpr auto calculate_crc32(View::Bytes data) -> U32 {
  // The 256 entry CRC32 look up table is prepopulated at compile time.
  constexpr Static::Vector<U32, 256> crc_table = populate_crc_table();

  // PNG initializes CRC32 to all ones and complements it after the final byte.
  U32 running_crc = 0xFFFFFFFF;
  for (Count byte_index = 0; byte_index < data.get_size(); byte_index++) {
    running_crc =
        crc_table[(running_crc ^ data[byte_index]) & 0xFF] ^ (running_crc >> 8);
  }

  return running_crc ^ 0xFFFFFFFF;
}

constexpr auto read_chunk(View::Bytes source, Count offset) -> Chunk {
  if (offset + chunk_metadata_size > source.get_size()) [[unlikely]] {
    Diagnostics::Log::Message<96> error_message(Diagnostics::Log::Level::Error);
    error_message << "Png: Chunk at offset "_view << S64(offset)
                  << " truncated before header end"_view;
    return Chunk();
  }

  U32 length;
  Data::copy(
      Data::cast<U8>(&length), source.get_data() + offset, sizeof(length));
  length = Data::ensure_endian<Data::ByteOrder::Big, Data::ByteOrder::Native>(
      length);
  if (offset + chunk_metadata_size + length > source.get_size()) [[unlikely]] {
    Diagnostics::Log::Message<128> error_message(
        Diagnostics::Log::Level::Error);
    error_message << "Png: Chunk at offset "_view << S64(offset)
                  << " with length "_view << S64(length)
                  << " extends past end of stream"_view;
    return Chunk();
  }

  Static::Bytes<4> type(source.slice(offset + 4, 4));
  View::Bytes data = source.slice(offset + 8, length);

  // Caculating the CRC from the buffer is slow and is unnecessary for files
  // from a trusted source. The check is still used in debug builds, but in
  // release builds removing the CRC check improves throughput by as much as
  // 20% for large images.

#if PERI_DEBUG

  U32 stored_crc;
  Data::copy(
      Data::cast<U8>(&stored_crc), source.get_data() + offset + 8 + length,
      sizeof(stored_crc));
  stored_crc =
      Data::ensure_endian<Data::ByteOrder::Big, Data::ByteOrder::Native>(
          stored_crc);

  U32 actual_crc = calculate_crc32(source.slice(offset + 4, 4 + length));
  if (stored_crc != actual_crc) [[unlikely]] {
    Diagnostics::Log::Message<96> error_message(Diagnostics::Log::Level::Error);
    error_message << "Png: CRC-32 mismatch for chunk '"_view << type.get_view()
                  << "' at offset "_view << S64(offset);
    return Chunk();
  }

#endif

  return Chunk(data, type, length);
}

constexpr auto write_chunk(
    Access::Bytes buffer,
    Count& write_position,
    View::Bytes type_tag,
    View::Bytes data) -> void {
  auto output = buffer.get_data();

  Data::write<Data::ByteOrder::Big>(
      Data::cast<U32>(output + write_position), U32(data.get_size()));
  write_position += sizeof(U32);

  Data::copy(output + write_position, type_tag.get_data(), type_tag.get_size());
  write_position += type_tag.get_size();

  // An empty View can carry null storage. Avoid passing it to `Data::copy` even
  // with a zero byte count.
  if (!data.is_empty()) {
    Data::copy(output + write_position, data.get_data(), data.get_size());
  }

  write_position += data.get_size();

  // PNG checksums the type tag and payload but excludes the length field. Store
  // the resulting CRC32 in big endian order.
  Count chunk_length = type_tag.get_size() + data.get_size();
  U32 chunk_crc = calculate_crc32(
      View::Bytes(output + write_position - chunk_length, chunk_length));
  Data::write<Data::ByteOrder::Big>(
      Data::cast<U32>(output + write_position), chunk_crc);
  write_position += 4;
}

// The Paeth predictor estimates the next sample by computing a linear
// extrapolation using the left, upper, and upper left pixels and returning the
// value closest to the current pixel.
constexpr auto paeth_predictor(U8 left, U8 up, U8 upper_left) -> U8 {
  S16 signed_left = S16(left);
  S16 signed_up = S16(up);
  S16 signed_upper_left = S16(upper_left);
  S16 predictor = signed_left + signed_up - signed_upper_left;

  U16 score_left = Scalar::magnitude<U16>(S16(predictor - signed_left));
  U16 score_up = Scalar::magnitude<U16>(S16(predictor - signed_up));
  U16 score_upper_left =
      Scalar::magnitude<U16>(S16(predictor - signed_upper_left));
  if (score_left <= score_up && score_left <= score_upper_left) {
    return left;
  }

  if (score_up <= score_upper_left) {
    return up;
  }

  return upper_left;
}

// Smaller residuals _generally_ compress better under DEFLATE.
// We treat each byte as a signed value and sum the magnitudes as a cheap
// estimate for compressibility.
template <Bool first_row>
auto score_row(View::Bytes current_row, View::Bytes previous_row)
    -> FilterType {
  constexpr auto residual_magnitude = [](U8 residual) -> U8 {
    return Scalar::magnitude<U8>(__builtin_bit_cast(S8, residual));
  };
  constexpr auto filter_count = first_row ? 2 : 5;
  constexpr auto channel_count = Rgba8::get_channel_count();
  Static::Vector<U64, filter_count> scores;

  auto current_row_data = current_row.get_data();
  auto previous_row_data = previous_row.get_data();

  // The first pixel has no left neighbor. Sub therefore matches None, while
  // row dependent filters use only the sample above.
  for (Count i = 0; i < channel_count; i++) {
    scores[U8(FilterType::None)] += residual_magnitude(current_row_data[i]);
    scores[U8(FilterType::Sub)] += residual_magnitude(current_row_data[i]);
    if constexpr (!first_row) {
      U8 up = previous_row_data[i];
      scores[U8(FilterType::Up)] +=
          residual_magnitude(current_row_data[i] - up);
      scores[U8(FilterType::Average)] +=
          residual_magnitude(current_row_data[i] - up / 2);
      scores[U8(FilterType::Paeth)] +=
          residual_magnitude(current_row_data[i] - up);
    }
  }

  for (Count i = channel_count; i < current_row.get_size(); i++) {
    U8 left = current_row_data[i - channel_count];
    scores[U8(FilterType::None)] += residual_magnitude(current_row_data[i]);
    scores[U8(FilterType::Sub)] +=
        residual_magnitude(current_row_data[i] - left);
    if constexpr (!first_row) {
      U8 up = previous_row_data[i];
      U8 upper_left = previous_row_data[i - channel_count];
      scores[U8(FilterType::Up)] +=
          residual_magnitude(current_row_data[i] - up);
      scores[U8(FilterType::Average)] +=
          residual_magnitude(current_row_data[i] - (U16(left) + U16(up)) / 2);
      scores[U8(FilterType::Paeth)] += residual_magnitude(
          current_row_data[i] - paeth_predictor(left, up, upper_left));
    }
  }

  return FilterType(Algorithm::Search::min(View::Vector<U64>(scores)));
}

// Encodes one unfiltered RGBA row into PNG residual bytes. All three ranges
// have the same extent. `previous_row` contains the unfiltered row above when
// the filter uses upper samples. The first row supplies a valid placeholder.
constexpr auto apply_row_filter(
    FilterType filter_type,
    Access::Bytes output_row,
    View::Bytes current_row,
    View::Bytes previous_row) -> void {
  constexpr auto channel_count = Rgba8::get_channel_count();
  Count size = current_row.get_size();

  auto output_row_data = output_row.get_data();
  auto current_row_data = current_row.get_data();
  auto previous_row_data = previous_row.get_data();

#if PERI_DEBUG

  // All of the buffers should be appropriately sized so just work on the raw
  // data directly since this is hot path code.
  if (output_row.get_size() != size || previous_row.get_size() != size) {
    Diagnostics::Log::fatal(
        "PNG: apply_filter_row was called with one or more invalid rows."_view);
    return;
  }

#endif

  // The first pixel has no left sample. PNG substitutes zero there, while Up,
  // Average, and Paeth can still use the sample above.
  switch (filter_type) {
  case FilterType::Sub:
    Data::copy(output_row_data, current_row_data, channel_count);
    break;
  case FilterType::Up:
    for (Count i = 0; i < channel_count; i++) {
      output_row_data[i] = U8(current_row_data[i] - previous_row_data[i]);
    }

    break;
  case FilterType::Average:
    for (Count i = 0; i < channel_count; i++) {
      output_row_data[i] = current_row_data[i] - U8(previous_row_data[i]) / 2;
    }

    break;
  case FilterType::Paeth:
    for (Count i = 0; i < channel_count; i++) {
      output_row_data[i] =
          U8(current_row_data[i] - paeth_predictor(0, previous_row_data[i], 0));
    }

    break;

    // `score_row` produces only declared filters. Keep other enum
    // representations deterministic by treating them as None.
  case FilterType::None:
  default:
    Data::copy(output_row_data, current_row_data, size);
    return;
  }

  switch (filter_type) {
  case FilterType::Sub:
    for (Count i = channel_count; i < size; i++) {
      U8 left = current_row_data[i - channel_count];
      output_row_data[i] = U8(current_row_data[i] - left);
    }

    break;
  case FilterType::Up:
    for (Count i = channel_count; i < size; i++) {
      output_row_data[i] = U8(current_row_data[i] - previous_row_data[i]);
    }

    break;
  case FilterType::Average:
    for (Count i = channel_count; i < size; i++) {
      U8 left = current_row_data[i - channel_count];
      U8 up = previous_row_data[i];
      output_row_data[i] =
          U8(current_row_data[i] - U8((U32(left) + U32(up)) / 2));
    }

    break;
  case FilterType::Paeth:
    for (Count i = channel_count; i < size; i++) {
      U8 left = current_row_data[i - channel_count];
      U8 up = previous_row_data[i];
      U8 upper_left = previous_row_data[i - channel_count];
      output_row_data[i] =
          U8(current_row_data[i] - paeth_predictor(left, up, upper_left));
    }

    break;

  case FilterType::None:
  default:
    Data::copy(output_row_data, current_row_data, size);
  }
}

// Applies adaptive PNG filtering to the source pixels, scoring all five filter
// types per row and selecting the option with the lowest residual.
constexpr auto apply_adaptive_filtering(const Matrix<Rgba8>& image)
    -> Thread::Bytes {
  const auto row_stride =
      Count(image.get_column_count()) * Rgba8::get_byte_count();
  const auto output_size = Count(image.get_row_count()) * (1 + row_stride);
  const auto raw_bytes = Data::cast<U8>(image.get_values().get_data());

  Thread::Bytes output(output_size);
  output.forgetful_resize(output_size);

  auto* output_data = output.get_access().get_data();

  // The first row supports two filters, so scoring covers exactly that pair.
  // We use the current row as the previous row since it's not used.
  View::Bytes current_row(raw_bytes, row_stride);
  FilterType best_filter = score_row<True>(current_row, current_row);

  apply_row_filter(
      best_filter, output.get_access().slice(1, row_stride),
      View::Bytes(raw_bytes, row_stride), View::Bytes(raw_bytes, row_stride));

  output_data[0] = U8(best_filter);

  // The rest of the rows after the first are scored against all filters.
  for (Count row = 1; row < image.get_row_count(); row++) {
    View::Bytes previous_row = current_row;
    current_row = View::Bytes(raw_bytes + row * row_stride, row_stride);

    // Score before writing so only the selected residual reaches output instead
    // of materializing all five candidate rows.
    const auto best_filter = score_row<False>(current_row, previous_row);
    Access::Bytes output_row =
        output.get_access().slice(row * (1 + row_stride) + 1, row_stride);
    apply_row_filter(best_filter, output_row, current_row, previous_row);

    output_data[row * (1 + row_stride)] = U8(best_filter);
  }

  return output;
}

// Reconstructs one filtered row. PNG defines unavailable samples above the
// first row as zero, so `first_row` compiles away every `previous_row` access.
// Following rows always receive the preceding reconstructed output.
//
// Raw pointers keep bounds work outside this decoder hot loop. The caller
// validates the complete filtered and output extents before entering it.
template <Bool first_row>
auto reconstruct_row(
    FilterType filter_type,
    const U8* filtered_row,
    U8* output_row,
    const U8* previous_row,
    Count stride,
    Count bytes_per_pixel) -> Bool {
  switch (filter_type) {
  case FilterType::None:
    Data::copy(output_row, filtered_row, stride);
    break;

  case FilterType::Sub:
    Data::copy(output_row, filtered_row, bytes_per_pixel);
    for (Count i = bytes_per_pixel; i < stride; i++) {
      output_row[i] = filtered_row[i] + output_row[i - bytes_per_pixel];
    }

    break;

  case FilterType::Up:
    // The first row's saturated previous values make a direct copy exact.
    if constexpr (first_row) {
      Data::copy(output_row, filtered_row, stride);
    } else {
      for (Count i = 0; i < stride; i++) {
        output_row[i] = filtered_row[i] + previous_row[i];
      }
    }

    break;

  case FilterType::Average:
    if constexpr (first_row) {
      Data::copy(output_row, filtered_row, bytes_per_pixel);
      for (Count i = bytes_per_pixel; i < stride; i++) {
        output_row[i] =
            filtered_row[i] + U8(U32(output_row[i - bytes_per_pixel]) / 2);
      }
    } else {
      // First column
      for (Count i = 0; i < bytes_per_pixel; i++) {
        output_row[i] = filtered_row[i] + U8(U32(previous_row[i]) / 2);
      }

      // Rest of the row
      for (Count i = bytes_per_pixel; i < stride; i++) {
        output_row[i] =
            filtered_row[i] +
            U8((U32(output_row[i - bytes_per_pixel]) + U32(previous_row[i])) /
               2);
      }
    }

    break;

  case FilterType::Paeth:
    // PNG substitutes zero for upper samples on the first row. Paeth then
    // selects the left sample and becomes Sub.
    if constexpr (first_row) {
      Data::copy(output_row, filtered_row, bytes_per_pixel);
      for (Count i = bytes_per_pixel; i < stride; i++) {
        output_row[i] = filtered_row[i] + output_row[i - bytes_per_pixel];
      }
    } else {
      // First column
      for (Count i = 0; i < bytes_per_pixel; i++) {
        output_row[i] = filtered_row[i] + previous_row[i];
      }

      // Rest of the row
      for (Count i = bytes_per_pixel; i < stride; i++) {
        output_row[i] = filtered_row[i] +
                        paeth_predictor(
                            output_row[i - bytes_per_pixel], previous_row[i],
                            previous_row[i - bytes_per_pixel]);
      }
    }

    break;

  default: {
    Diagnostics::Log::Message<64> error_message(Diagnostics::Log::Level::Error);
    error_message << "Png: Unknown filter type "_view << U32(filter_type);
    return False;
  }
  }

  return True;
}

constexpr auto reconstruct_filter(
    View::Bytes filtered_rows,
    Count width,
    Count height,
    Count bytes_per_pixel,
    Access::Bytes output) -> Bool {
  const Count stride = width * bytes_per_pixel;
  const Count row_bytes = 1 + stride;
  const auto filtered_row_data = filtered_rows.get_data();
  auto output_pixels = output.get_data();
  if (filtered_rows.get_size() < row_bytes * height) [[unlikely]] {
    Diagnostics::Log::Message<128> error_message(
        Diagnostics::Log::Level::Error);
    error_message << "Png: Decompressed size "_view << filtered_rows.get_size()
                  << " is smaller than required "_view << (row_bytes * height)
                  << " bytes for this image"_view;
    return False;
  }

  // The first row specialization never reads its null `previous_row` argument.
  FilterType filter_type = FilterType(filtered_row_data[0]);
  if (!reconstruct_row<True>(
          filter_type, filtered_row_data + 1, output_pixels, nullptr, stride,
          bytes_per_pixel)) [[unlikely]] {
    return False;
  }

  // Later rows pass the preceding output directly, so their specialization also
  // needs no runtime null check.
  for (Count row = 1; row < height; row++) {
    FilterType filter_type = FilterType(filtered_row_data[row * row_bytes]);
    const U8* filtered_row = filtered_row_data + row * row_bytes + 1;
    U8* output_row = output_pixels + row * stride;
    const U8* previous_row = output_row - stride;
    if (!reconstruct_row<False>(
            filter_type, filtered_row, output_row, previous_row, stride,
            bytes_per_pixel)) [[unlikely]] {
      return False;
    }
  }

  return True;
}

constexpr auto convert_to_pixels(
    View::Bytes raw_pixels,
    Count width,
    Count height,
    ColorType color_type,
    View::Bytes palette,
    Thread::Vector<Rgba8>& output) -> Bool {
  Count source_channels = number_of_color_channels(color_type);
  if (source_channels == 0) [[unlikely]] {
    return False;
  }

  Count pixel_count = width * height;
  output.resize(pixel_count);

  auto data = raw_pixels.get_data();
  for (Count pixel_index = 0; pixel_index < pixel_count; pixel_index++) {
    Count source_offset = pixel_index * source_channels;
    switch (color_type) {
    case ColorType::Greyscale:
      output[pixel_index] = Rgba8::from_grey(data[source_offset]);
      break;
    case ColorType::GreyscaleAlpha:
      output[pixel_index] =
          Rgba8::from_grey_alpha(data[source_offset], data[source_offset + 1]);
      break;
    case ColorType::Rgb:
      output[pixel_index] = Rgba8::from_rgb(
          data[source_offset + 0], data[source_offset + 1],
          data[source_offset + 2]);
      break;
    case ColorType::Indexed: {
      U8 palette_index = data[source_offset];
      Count palette_offset = Count(palette_index) * 3;

      // Each indexed color consumes three PLTE bytes. Reject an index whose RGB
      // triple is not completely present.
      if (palette_offset + 3 > palette.get_size()) [[unlikely]] {
        Diagnostics::Log::Message<128> error_message(
            Diagnostics::Log::Level::Error);
        error_message << "Png: Palette index "_view << U32(palette_index)
                      << " at pixel "_view << S64(pixel_index)
                      << " exceeds palette size "_view
                      << S64(palette.get_size() / 3);
        return False;
      }

      output[pixel_index] = Rgba8::from_rgb(
          palette[palette_offset + 0], palette[palette_offset + 1],
          palette[palette_offset + 2]);
      break;
    }

      // `process_data` reconstructs RGBA directly into final pixel storage, so
      // conversion deliberately excludes that layout.
    case ColorType::Rgba:
    default:
      return False;
    }
  }

  return True;
}

constexpr auto read_header(const View::Bytes source) -> ImageInfo {
  // Require the signature and complete IHDR chunk before taking any source
  // slices. This bound includes the chunk length, type, payload, and CRC32.
  constexpr Count required_size =
      png_signature.get_size() + chunk_metadata_size + ImageInfo::size;
  if (source.get_size() < required_size) [[unlikely]] {
    return ImageInfo();
  }

  // Validate the PNG signiture
  if (source.slice(0, png_signature.get_size()) != png_signature.get_view())
      [[unlikely]] {
    Diagnostics::Log::error("Png: Invalid file signature"_view);
    return ImageInfo();
  }

  // PNG requires IHDR to be the first chunk. Reading any other payload as
  // ImageInfo would assign dimensions and encoding from unrelated bytes.
  constexpr auto header_tag = "IHDR"_view;
  auto chunk_tag = source.slice(
      png_signature.get_size() + sizeof(U32), header_tag.get_size());
  if (chunk_tag != "IHDR"_view) [[unlikely]] {
    Diagnostics::Log::Message<96> error_message(Diagnostics::Log::Level::Error);
    error_message << "Png: First chunk must be \"IHDR\". header="_view
                  << chunk_tag;
    return ImageInfo();
  }

  // The wire header can start at any byte address. Copy its 13 bytes into
  // aligned storage without reading the native structure's trailing padding.
  ImageInfo image_info;
  Data::copy(
      Data::cast<U8>(&image_info),
      source.get_data() + png_signature.get_size() + sizeof(U32) +
          header_tag.get_size(),
      ImageInfo::size);

  // Rgba8 output supports only 8 bit samples. Other PNG depths require separate
  // unpacking and scaling before conversion.
  if (image_info.get_bit_depth() != Rgba8::get_bit_depth()) [[unlikely]] {
    Diagnostics::Log::Message<96> error_message(Diagnostics::Log::Level::Error);
    error_message << "Png: Unsupported bit depth "_view
                  << U32(image_info.get_bit_depth())
                  << " (only 8 bit is supported)"_view;
    return ImageInfo();
  }

  const ColorType color_type = image_info.get_color_type();
  if (number_of_color_channels(color_type) == 0) [[unlikely]] {
    Diagnostics::Log::Message<96> error_message(Diagnostics::Log::Level::Error);
    error_message << "Png: Unsupported color type "_view << U32(color_type);
    return ImageInfo();
  }

  if (!image_info.uses_standard_methods()) [[unlikely]] {
    Diagnostics::Log::error(
        "Png: Non-standard compression, filter, or interlace method in IHDR"_view);
    return ImageInfo();
  }

  return image_info;
}

constexpr auto process_data(
    const ImageInfo info,
    View::Bytes source,
    View::Bytes palette) -> Option<Matrix<Rgba8>> {
  // Domain checks the pixel count before either decode path allocates. The
  // filtered stream also carries one control byte per row, so its byte extent
  // needs a separate check before the inflater reserves that storage.
  const Domain<2, Boundary::Clip, Boundary::Clip> domain(
      Size<U32, 2>({{info.get_width(), info.get_height()}}));
  const auto pixel_count = domain.get_element_count();
  const Count bytes_per_pixel = number_of_color_channels(info.get_color_type());
  const Count max_bytes = ~CppSize{};
  if (!pixel_count || bytes_per_pixel == 0 ||
      *pixel_count > max_bytes / sizeof(Rgba8) ||
      *pixel_count > (max_bytes - info.get_height()) / bytes_per_pixel) {
    Diagnostics::Log::error("Png: Decoded extent exceeds address space"_view);
    return Option<Matrix<Rgba8>>();
  }

  const Count decompressed_capacity =
      *pixel_count * bytes_per_pixel + info.get_height();

  // Debug validates both the PNG chunk CRC and the zlib Adler32 trailer.
  // Release omits both full payload checksum passes to preserve decode
  // throughput.
#if PERI_DEBUG

  Thread::Bytes filtered_rows = Deflate::inflate(source, decompressed_capacity);

#else

  Thread::Bytes filtered_rows =
      Deflate::inflate_without_checksum(source, decompressed_capacity);

#endif

  if (filtered_rows.is_empty()) [[unlikely]] {
    return Option<Matrix<Rgba8>>();
  }

  Thread::Vector<Rgba8> pixels;

  // PNG RGBA samples match the four byte Rgba8 layout, so reconstruction can
  // write final pixel storage without an intermediate conversion buffer.
  if (info.get_color_type() == ColorType::Rgba) {
    pixels.resize(*pixel_count);
    if (!reconstruct_filter(
            filtered_rows.get_view(), info.get_width(), info.get_height(),
            bytes_per_pixel, pixels.get_access().get_bytes())) [[unlikely]] {
      Diagnostics::Log::error(
          "Png: Filter reconstruction failed. Decompressed data may be truncated"_view);
      return Option<Matrix<Rgba8>>();
    }

  } else {
    // Other channel layouts need an intermediate byte buffer before conversion
    // can fill the Rgba8 vector.
    Thread::Bytes raw_pixels{};
    raw_pixels.forgetful_resize(*pixel_count * bytes_per_pixel);
    if (!reconstruct_filter(
            filtered_rows.get_view(), info.get_width(), info.get_height(),
            bytes_per_pixel, raw_pixels)) [[unlikely]] {
      Diagnostics::Log::error(
          "Png: Filter reconstruction failed. Decompressed data may be truncated"_view);
      return Option<Matrix<Rgba8>>();
    }

    if (!convert_to_pixels(
            raw_pixels.get_view(), info.get_width(), info.get_height(),
            info.get_color_type(), palette, pixels)) [[unlikely]] {
      Diagnostics::Log::error("Png: Rgba8 conversion failed"_view);
      return Option<Matrix<Rgba8>>();
    }
  }

  return Matrix<Rgba8>::project(domain, Data::take(pixels))
      .visit(
          [](Matrix<Rgba8>& matrix) -> Option<Matrix<Rgba8>> {
            return Data::take(matrix);
          },
          [](Matrix<Rgba8>::Failure) -> Option<Matrix<Rgba8>> {
            Diagnostics::Log::error(
                "Png: Decoded pixel count does not match dimensions"_view);
            return {};
          });
}

auto Png::decode(const View::Bytes source) -> Matrix<Rgba8> {
  // `read_header` uses zero dimensions as its failure state. PNG also forbids
  // an empty dimension, so both cases produce the same empty result.
  ImageInfo info = read_header(source);
  if (info.get_width() == 0 || info.get_height() == 0) [[unlikely]] {
    return Matrix<Rgba8>();
  }

  // For PNGs with a single IDAT chunk (the common case) we hold a view to avoid
  // copying the entire compressed stream, but if any additional IDAT chunks are
  // found we switch to merging them into a dynamic buffer.
  View::Bytes binary_data{};
  Thread::Bytes merged_data{};
  View::Bytes palette{};

  // Walk the chunk sequence collecting all IDAT payloads plus the optional
  // PLTE (palette) chunk needed for indexed color images.
  Count chunk_offset = png_signature.get_size();
  while (chunk_offset < source.get_size()) {
    Chunk chunk = read_chunk(source, chunk_offset);
    if (!chunk.get_valid()) [[unlikely]] {
      return Matrix<Rgba8>();
    }

    if (chunk.get_type() == "IEND"_view) {
      break;
    }

    if (chunk.get_type() == "IDAT"_view) {
      if (binary_data.is_empty()) {
        binary_data = chunk.get_data();
      } else {
        // The second IDAT copies the first payload from source into owned
        // storage.
        // Each following `concat` may relocate that storage, so refresh the
        // decoding view after every append rather than retaining its previous
        // address.
        if (merged_data.is_empty()) {
          merged_data.copy(binary_data);
        }

        merged_data.concat(chunk.get_data());
        binary_data = merged_data.get_view();
      }
    } else if (chunk.get_type() == "PLTE"_view) {
      palette = chunk.get_data();
    }

    chunk_offset += chunk_metadata_size + chunk.get_length();
  }

  if (binary_data.is_empty()) [[unlikely]] {
    Diagnostics::Log::error("Png: No IDAT chunk found or chunk was empty"_view);
    return Matrix<Rgba8>();
  }

  return process_data(info, binary_data, palette)
      .visit(
          [] { return Matrix<Rgba8>(); },
          [](Matrix<Rgba8>& image) { return Data::take(image); });
}

auto Png::encode(const Matrix<Rgba8>& image) -> Thread::Bytes {
  View::Vector<Rgba8> pixels = image.get_values();

  // PNG requires at least one scanline, and adaptive filtering reads row zero.
  // An empty Matrix therefore has no encodable representation here.
  if (pixels.is_empty()) [[unlikely]] {
    return Thread::Bytes();
  }

  // Filtering adds one byte per row. Matrix's pixel allocation bound does not
  // cover that extra storage, so check it before creating the filtered rows.
  const Count row_stride = Count(image.get_column_count()) * sizeof(Rgba8);
  const Count max_bytes = ~CppSize{};
  if (row_stride >= max_bytes ||
      image.get_row_count() > max_bytes / (row_stride + 1)) {
    return Thread::Bytes();
  }

  // Filter the rows using adaptive PNG filtering then deflate the output.
  Thread::Bytes filtered_rows = apply_adaptive_filtering(image);

  // Compress using the default compression level for balanced speed vs size.
  Thread::Bytes compressed = Deflate::deflate(filtered_rows.get_view());
  if (compressed.is_empty()) [[unlikely]] {
    return Thread::Bytes();
  }

  // The compressed payload is one PNG chunk, whose length field is 32 bits.
  // Leave room for the signature and three chunk frames in the output buffer.
  const Count framing_size =
      png_signature.get_size() + ImageInfo::size + chunk_metadata_size * 3;
  if (compressed.get_size() > ~U32{} ||
      compressed.get_size() > max_bytes - framing_size) {
    return Thread::Bytes();
  }

  const Count output_size = framing_size + compressed.get_size();
  Thread::Bytes output(output_size);
  output.forgetful_resize(output_size);

  auto data = output.get_access();

  // Emit the required signature, IHDR, image payload, and terminal IEND in wire
  // order. The precomputed capacity includes framing for all three chunks.
  ImageInfo header(image);
  Data::copy(
      data.get_data(), png_signature.get_data(), png_signature.get_size());

  Count write_position = png_signature.get_size();
  write_chunk(
      data, write_position, "IHDR"_view,
      View::Bytes(Data::cast<U8>(&header), ImageInfo::size));
  write_chunk(data, write_position, "IDAT"_view, compressed.get_view());
  write_chunk(data, write_position, "IEND"_view, View::Bytes());

  output.resize(write_position);
  return output;
}
