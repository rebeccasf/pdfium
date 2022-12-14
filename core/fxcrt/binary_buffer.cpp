// Copyright 2017 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fxcrt/binary_buffer.h"

#include <algorithm>
#include <utility>

#include "core/fxcrt/fx_safe_types.h"

namespace fxcrt {

BinaryBuffer::BinaryBuffer() = default;

BinaryBuffer::BinaryBuffer(BinaryBuffer&& that) noexcept
    : m_AllocStep(that.m_AllocStep),
      m_AllocSize(that.m_AllocSize),
      m_DataSize(that.m_DataSize),
      m_pBuffer(std::move(that.m_pBuffer)) {
  // Can't just default, need to leave |that| in a valid state, which means
  // that the size members reflect the (null) moved-from buffer.
  that.m_AllocStep = 0;
  that.m_AllocSize = 0;
  that.m_DataSize = 0;
}

BinaryBuffer::~BinaryBuffer() = default;

BinaryBuffer& BinaryBuffer::operator=(BinaryBuffer&& that) noexcept {
  // Can't just default, need to leave |that| in a valid state, which means
  // that the size members reflect the (null) moved-from buffer.
  m_AllocStep = that.m_AllocStep;
  m_AllocSize = that.m_AllocSize;
  m_DataSize = that.m_DataSize;
  m_pBuffer = std::move(that.m_pBuffer);
  that.m_AllocStep = 0;
  that.m_AllocSize = 0;
  that.m_DataSize = 0;
  return *this;
}

void BinaryBuffer::DeleteBuf(size_t start_index, size_t count) {
  if (!m_pBuffer || count > m_DataSize || start_index > m_DataSize - count)
    return;

  memmove(m_pBuffer.get() + start_index, m_pBuffer.get() + start_index + count,
          m_DataSize - start_index - count);
  m_DataSize -= count;
}

pdfium::span<uint8_t> BinaryBuffer::GetSpan() {
  return {m_pBuffer.get(), GetSize()};
}

pdfium::span<const uint8_t> BinaryBuffer::GetSpan() const {
  return {m_pBuffer.get(), GetSize()};
}

size_t BinaryBuffer::GetLength() const {
  return m_DataSize;
}

void BinaryBuffer::Clear() {
  m_DataSize = 0;
}

std::unique_ptr<uint8_t, FxFreeDeleter> BinaryBuffer::DetachBuffer() {
  m_DataSize = 0;
  m_AllocSize = 0;
  return std::move(m_pBuffer);
}

void BinaryBuffer::EstimateSize(size_t size) {
  if (m_AllocSize < size)
    ExpandBuf(size - m_DataSize);
}

void BinaryBuffer::ExpandBuf(size_t add_size) {
  FX_SAFE_SIZE_T new_size = m_DataSize;
  new_size += add_size;
  if (m_AllocSize >= new_size.ValueOrDie())
    return;

  size_t alloc_step = std::max(static_cast<size_t>(128),
                               m_AllocStep ? m_AllocStep : m_AllocSize / 4);
  new_size += alloc_step - 1;  // Quantize, don't combine these lines.
  new_size /= alloc_step;
  new_size *= alloc_step;
  m_AllocSize = new_size.ValueOrDie();
  m_pBuffer.reset(m_pBuffer
                      ? FX_Realloc(uint8_t, m_pBuffer.release(), m_AllocSize)
                      : FX_Alloc(uint8_t, m_AllocSize));
}

void BinaryBuffer::AppendSpan(pdfium::span<const uint8_t> span) {
  return AppendBlock(span.data(), span.size());
}

void BinaryBuffer::AppendBlock(const void* pBuf, size_t size) {
  if (size == 0)
    return;

  ExpandBuf(size);
  if (pBuf) {
    memcpy(m_pBuffer.get() + m_DataSize, pBuf, size);
  } else {
    memset(m_pBuffer.get() + m_DataSize, 0, size);
  }
  m_DataSize += size;
}

void BinaryBuffer::AppendString(const ByteString& str) {
  AppendBlock(str.c_str(), str.GetLength());
}

void BinaryBuffer::AppendByte(uint8_t byte) {
  ExpandBuf(1);
  m_pBuffer.get()[m_DataSize++] = byte;
}

}  // namespace fxcrt
