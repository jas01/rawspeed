/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2017 Uwe Müssel
    Copyright (C) 2017 Roman Lebedev

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#pragma once

#include "common/Common.h"                      // for ushort16
#include "common/RawImage.h"                    // for RawImage
#include "decompressors/AbstractDecompressor.h" // for AbstractDecompressor
#include "io/BitPumpMSB.h"                      // for BitPumpMSB
#include "io/ByteStream.h"                      // for ByteStream
#include "metadata/ColorFilterArray.h"          // for CFAColor
#include <algorithm>                            // for move
#include <array>                                // for array
#include <cassert>                              // for cassert
#include <cstddef>                              // for size_t
#include <vector>                               // for vector

namespace rawspeed {

class FujiDecompressor final : public AbstractDecompressor {
public:
  struct FujiHeader {
    FujiHeader() = default;

    explicit FujiHeader(ByteStream* input_);
    explicit __attribute__((pure)) operator bool() const; // validity check

    ushort16 signature;
    uchar8 version;
    uchar8 raw_type;
    uchar8 raw_bits;
    ushort16 raw_height;
    ushort16 raw_rounded_width;
    ushort16 raw_width;
    ushort16 block_size;
    uchar8 blocks_in_row;
    ushort16 total_lines;
  };

  FujiHeader header;

  struct FujiStrip {
    // part of which 'image' this block is
    const FujiHeader& h;

    // which strip is this, 0 .. h.blocks_in_row-1
    const int n;

    // the compressed data of this strip
    const ByteStream bs;

    FujiStrip(const FujiHeader& h_, int block, ByteStream bs_)
        : h(h_), n(block), bs(std::move(bs_)) {
      assert(n >= 0 && n < h.blocks_in_row);
    }

    // each strip's line corresponds to 6 output lines.
    static int lineHeight() { return 6; }

    // how many vertical lines does this block encode?
    int height() const { return h.total_lines; }

    // how many horizontal pixels does this block encode?
    int width() const {
      // if this is not the last block, we are good.
      if ((n + 1) != h.blocks_in_row)
        return h.block_size;

      // ok, this is the last block...

      assert(h.block_size * h.blocks_in_row >= h.raw_width);
      return h.raw_width - offsetX();
    }

    // where vertically does this block start?
    int offsetY(int line = 0) const {
      assert(line >= 0 && line < height());
      return lineHeight() * line;
    }

    // where horizontally does this block start?
    int offsetX() const { return h.block_size * n; }
  };

  FujiDecompressor(ByteStream input, const RawImage& img);

  void fuji_compressed_load_raw();

  void fuji_decode_loop(size_t start, size_t end) const;

protected:
  struct fuji_compressed_params {
    fuji_compressed_params() = default;

    explicit fuji_compressed_params(const FujiDecompressor& d);

    std::vector<char> q_table; /* quantization table */
    int q_point[5]; /* quantization points */
    int max_bits;
    int min_value;
    int raw_bits;
    int total_values;
    int maxDiff;
    ushort16 line_width;
  };

  fuji_compressed_params common_info;

  struct int_pair {
    int value1;
    int value2;
  };

  enum _xt_lines {
    _R0 = 0,
    _R1,
    _R2,
    _R3,
    _R4,
    _G0,
    _G1,
    _G2,
    _G3,
    _G4,
    _G5,
    _G6,
    _G7,
    _B0,
    _B1,
    _B2,
    _B3,
    _B4,
    _ltotal
  };

  struct fuji_compressed_block {
    fuji_compressed_block() = default;

    void reset(const fuji_compressed_params* params);

    int_pair grad_even[3][41]; // tables of gradients
    int_pair grad_odd[3][41];
    std::vector<ushort16> linealloc;
    ushort16* linebuf[_ltotal];
  };

private:
  ByteStream input;
  RawImage mImg;

  std::array<std::array<CFAColor, 6>, 6> CFA;

  std::vector<FujiStrip> strips;

  void fuji_decode_strip(fuji_compressed_block* info_block,
                         const FujiStrip& strip) const;

  template <typename T>
  void copy_line(fuji_compressed_block* info, const FujiStrip& strip,
                 int cur_line, T&& idx) const;

  void copy_line_to_xtrans(fuji_compressed_block* info, const FujiStrip& strip,
                           int cur_line) const;
  void copy_line_to_bayer(fuji_compressed_block* info, const FujiStrip& strip,
                          int cur_line) const;

  void fuji_zerobits(BitPumpMSB* pump, int* count) const;
  int bitDiff(int value1, int value2) const;
  void fuji_decode_sample_even_internal(const ushort16* line_buf_cur,
                                        int* interp_val, int* grad,
                                        int* gradient) const;
  int fuji_decode_sample_even(fuji_compressed_block* info, BitPumpMSB* pump,
                              ushort16* line_buf, int* pos,
                              int_pair* grads) const;
  void fuji_decode_sample_odd_internal(const ushort16* line_buf_cur,
                                       int* interp_val, int* grad,
                                       int* gradient) const;
  int fuji_decode_sample_odd(fuji_compressed_block* info, BitPumpMSB* pump,
                             ushort16* line_buf, int* pos,
                             int_pair* grads) const;
  void fuji_decode_interpolation_even(int line_width, ushort16* line_buf,
                                      int* pos) const;
  void fuji_extend_generic(ushort16* linebuf[_ltotal], int line_width,
                           int start, int end) const;
  void fuji_extend_red(ushort16* linebuf[_ltotal], int line_width) const;
  void fuji_extend_green(ushort16* linebuf[_ltotal], int line_width) const;
  void fuji_extend_blue(ushort16* linebuf[_ltotal], int line_width) const;
  void xtrans_decode_block(fuji_compressed_block* info,
                           BitPumpMSB* pump, int cur_line) const;
  void fuji_bayer_decode_block(fuji_compressed_block* info,
                               BitPumpMSB* pump, int cur_line) const;
};

} // namespace rawspeed
