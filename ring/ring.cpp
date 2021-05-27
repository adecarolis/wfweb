////////////////////////////////////////////////////////////////////////////////
// FILE: ring.cpp
// DATE: 2016-02-25
// AUTH: Trevor Wilson
// DESC: Implements a lock-free, multi-consumer, multi-producer ring buffer 
//       class

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016 Trevor Wilson
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell 
// copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions :
// 
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "ring.h"
using namespace wilt;

#include <cstring>
// - std::memcpy

Ring_::Ring_()
  : beg_(nullptr)
  , end_(nullptr)
{
  std::atomic_init(&used_, static_cast<std::ptrdiff_t>(0));
  std::atomic_init(&free_, static_cast<std::ptrdiff_t>(0));
  std::atomic_init(&rbuf_, static_cast<char*>(0));
  std::atomic_init(&rptr_, static_cast<char*>(0));
  std::atomic_init(&wptr_, static_cast<char*>(0));
  std::atomic_init(&wbuf_, static_cast<char*>(0));
}

Ring_::Ring_(std::size_t size)
  : beg_(new char[size])
  , end_(beg_ + size)
{
  std::atomic_init(&used_, static_cast<std::ptrdiff_t>(0));
  std::atomic_init(&free_, static_cast<std::ptrdiff_t>(size));
  std::atomic_init(&rbuf_, beg_);
  std::atomic_init(&rptr_, beg_);
  std::atomic_init(&wptr_, beg_);
  std::atomic_init(&wbuf_, beg_);
}

Ring_::Ring_(Ring_&& ring)
  : beg_(ring.beg_)
  , end_(ring.end_)
{
  std::atomic_init(&used_, ring.used_.load());
  std::atomic_init(&free_, ring.free_.load());
  std::atomic_init(&rbuf_, ring.rbuf_.load());
  std::atomic_init(&rptr_, ring.rptr_.load());
  std::atomic_init(&wptr_, ring.wptr_.load());
  std::atomic_init(&wbuf_, ring.wbuf_.load());

  ring.beg_ = nullptr;
  ring.end_ = nullptr;

  ring.used_.store(0);
  ring.free_.store(0);
  ring.rbuf_.store(nullptr);
  ring.rptr_.store(nullptr);
  ring.wptr_.store(nullptr);
  ring.wbuf_.store(nullptr);
}

Ring_& Ring_::operator= (Ring_&& ring)
{
  delete[] beg_;

  beg_ = ring.beg_;
  end_ = ring.end_;

  used_.store(ring.used_.load());
  free_.store(ring.free_.load());
  rbuf_.store(ring.rbuf_.load());
  rptr_.store(ring.rptr_.load());
  wptr_.store(ring.wptr_.load());
  wbuf_.store(ring.wbuf_.load());

  ring.beg_ = nullptr;
  ring.end_ = nullptr;

  ring.used_.store(0);
  ring.free_.store(0);
  ring.rbuf_.store(nullptr);
  ring.rptr_.store(nullptr);
  ring.wptr_.store(nullptr);
  ring.wbuf_.store(nullptr);

  return *this;
}

Ring_::~Ring_()
{
  delete[] beg_;
}

std::size_t Ring_::size() const
{
  // The 'used' space can be negative in an over-reserved case, but it can be
  // clamped to 0 for simplicity.

  auto s = used_.load();
  return s < 0 ? 0 : static_cast<std::size_t>(s);
}

std::size_t Ring_::capacity() const
{
  return static_cast<std::size_t>(end_ - beg_);
}

void Ring_::read(void* data, std::size_t length) noexcept
{
  auto block = acquire_read_block_(length);

  copy_read_block_(block, (char*)data, length);
  release_read_block_(block, length);
}

void Ring_::write(const void* data, std::size_t length) noexcept
{
  auto block = acquire_write_block_(length);

  copy_write_block_(block, (const char*)data, length);
  release_write_block_(block, length);
}

bool Ring_::try_read(void* data, std::size_t length) noexcept
{
  auto block = try_acquire_read_block_(length);
  if (block == nullptr)
    return false;

  copy_read_block_(block, (char*)data, length);
  release_read_block_(block, length);

  return true;
}

bool Ring_::try_write(const void* data, std::size_t length) noexcept
{
  auto block = try_acquire_write_block_(length);
  if (block == nullptr)
    return false;

  copy_write_block_(block, (const char*)data, length);
  release_write_block_(block, length);

  return true;
}

char* Ring_::normalize_(char* ptr)
{
  return ptr < end_ ? ptr : ptr - capacity();
}

char* Ring_::acquire_read_block_(std::size_t length)
{
  auto size = static_cast<std::ptrdiff_t>(length);
  while (true)                                              // loop while conflict
  {
    auto old_rptr = rptr_.load(std::memory_order_consume);  // read rptr
    while (used_.load(std::memory_order_consume) < size)    // check for data
      ;                                                     // spin until success

    auto new_rptr = normalize_(old_rptr + size);            // get block end
    used_.fetch_sub(size);                                  // reserve
    if (rptr_.compare_exchange_strong(old_rptr, new_rptr))  // try commit
      return old_rptr;                                      // committed

    used_.fetch_add(size, std::memory_order_relaxed);       // un-reserve
  }
}

char* Ring_::try_acquire_read_block_(std::size_t length)
{
  auto size = static_cast<std::ptrdiff_t>(length);
  while (true)                                              // loop while conflict
  {
    auto old_rptr = rptr_.load(std::memory_order_consume);  // read rptr
    if (used_.load(std::memory_order_consume) < size)       // check for data
      return nullptr;                                       // return failure

    auto new_rptr = normalize_(old_rptr + size);            // get block end
    used_.fetch_sub(size);                                  // reserve
    if (rptr_.compare_exchange_strong(old_rptr, new_rptr))  // try commit
      return old_rptr;                                      // committed

    used_.fetch_add(size, std::memory_order_relaxed);       // un-reserve
  }
}

void Ring_::copy_read_block_(const char* block, char* data, std::size_t length)
{
  if (block + length < end_)
  {
    std::memcpy(data, block, length);
  }
  else
  {
    auto first = end_ - block;
    std::memcpy(data, block, first);
    std::memcpy(data + first, beg_, length - first);
  }
}

void Ring_::release_read_block_(char* old_rptr, std::size_t length)
{
  auto new_rptr = normalize_(old_rptr + length);            // get block end
  while (rbuf_.load() != old_rptr)                          // check for earlier reads
    ;                                                       // spin until reads complete

  rbuf_.store(new_rptr);                                    // finish commit
  free_.fetch_add(length, std::memory_order_relaxed);       // add to free space
}

char* Ring_::acquire_write_block_(std::size_t length)
{
  auto size = static_cast<std::ptrdiff_t>(length);
  while (true)                                              // loop while conflict
  {
    auto old_wbuf = wbuf_.load(std::memory_order_consume);  // read wbuf
    while (free_.load(std::memory_order_consume) < size)    // check for space
      ;                                                     // spin until success

    auto new_wbuf = normalize_(old_wbuf + size);            // get block end
    free_.fetch_sub(size);                                  // reserve
    if (wbuf_.compare_exchange_strong(old_wbuf, new_wbuf))  // try commit
      return old_wbuf;                                      // committed

    free_.fetch_add(size, std::memory_order_relaxed);       // un-reserve
  }
}

char* Ring_::try_acquire_write_block_(std::size_t length)
{
  auto size = static_cast<std::ptrdiff_t>(length);
  while (true)                                              // loop while conflict
  {
    auto old_wbuf = wbuf_.load(std::memory_order_consume);  // read wbuf
    if (free_.load(std::memory_order_consume) < size)       // check for space
      return nullptr;                                       // return failure

    auto new_wbuf = normalize_(old_wbuf + size);            // get block end
    free_.fetch_sub(size);                                  // reserve
    if (wbuf_.compare_exchange_strong(old_wbuf, new_wbuf))  // try commit
      return old_wbuf;                                      // committed

    free_.fetch_add(size, std::memory_order_relaxed);       // un-reserve
  }
}

void Ring_::copy_write_block_(char* block, const char* data, std::size_t length)
{
  if (block + length < end_)
  {
    std::memcpy(block, data, length);
  }
  else
  {
    auto first = end_ - block;
    std::memcpy(block, data, first);
    std::memcpy(beg_, data + first, length - first);
  }
}

void Ring_::release_write_block_(char* old_wbuf, std::size_t length)
{
  auto new_wbuf = normalize_(old_wbuf + length);            // get block end
  while (wptr_.load() != old_wbuf)                          // wait for earlier writes
    ;                                                       // spin until writes complete

  wptr_.store(new_wbuf);                                    // finish commit
  used_.fetch_add(length, std::memory_order_relaxed);       // add to used space
}
