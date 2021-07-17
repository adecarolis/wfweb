////////////////////////////////////////////////////////////////////////////////
// FILE: ring.h
// DATE: 2016-02-25
// AUTH: Trevor Wilson
// DESC: Defines a lock-free, multi-consumer, multi-producer ring buffer class

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

#ifndef WILT_RING_H
#define WILT_RING_H

#include <atomic>
// - std::atomic
#include <cstddef>
// - std::size_t
// - std::ptrdiff_t
#include <new>
// - ::new(ptr)
#include <type_traits>
// - std::is_nothrow_copy_constructible
// - std::is_nothrow_move_constructible
// - std::is_nothrow_move_assignable
// - std::is_nothrow_destructible
#include <utility>
// - std::move

namespace wilt
{
  //////////////////////////////////////////////////////////////////////////////
  // This structure aims to access elements in a ring buffer from multiple
  // concurrent readers and writers in a lock-free manner.
  // 
  // The class works by allocating the array and storing two pointers (for the
  // beginning and end of the allocated space). Two atomic pointers are used to
  // track the beginning and end of the currently used storage space. To
  // facilitate concurrent reads and writes, theres a read buffer pointer before
  // the read pointer for data currently being read, and a corresponding write
  // buffer pointer beyond the write pointer for data currently being written.
  // These buffer pointers cannot overlap. Just using these pointers suffer from
  // some minute inefficiencies and a few ABA problems. Therfore, atomic
  // integers are used to store the currently used and currently free sizes.
  // 
  // It allows multiple readers and multiple writers by implementing a reserve-
  // commit system. A thread wanting to read will check the used size to see if
  // there's enough data. If there is, it subtracts from the used size to
  // 'reserve' the read. It then does a compare-exchange to 'commit' by
  // increasing the read pointer. If that fails, then it backs out ('un-
  // reserves') by adding back to the used size and tries again. If it
  // succeeds, then it proceeds to read the data. In order to complete, the
  // reader must update the read buffer pointer to where it just finished
  // reading from. However, because other readers that started before may not be
  // done yet, the reader must wait until the read buffer pointer points to
  // where the read started. Only, then is the read buffer pointer updated, and
  // the free size increased. So while this implementation is lock-free, it is
  // not wait-free. This same principle works the same when writing (ammended
  // for the appropriate pointers).
  // 
  // If two readers try to read at the same time and there is only enough data
  // for one of them. The used size MAY be negative because they both 'reserve'
  // the data. This is an over-reserved state. But the compare-exchange will
  // only allow one reader to 'commit' to the read and the other will 'un-
  // reserve' the read.
  // 
  // |beg           |rptr      used=5             |wbuf         - unused
  // |----|----|++++|====|====|====|====|====|++++|----|        + modifying
  //  free=3   |rbuf                         |wptr     |end     = used
  // 
  // The diagram above shows a buffer of size 10 storing 5 bytes with a reader
  // reading one byte and one writer reading one byte.
  // 
  // Out of the box, the class works by reading and writing raw bytes from POD
  // data types and arrays. A wrapper could allow for a nicer interface for
  // pushing and popping elements. As it stands, this structure cannot be easily
  // modified to store types of variable size.

  class Ring_
  {
  private:
    ////////////////////////////////////////////////////////////////////////////
    // TYPE DEFINITIONS
    ////////////////////////////////////////////////////////////////////////////

    typedef char*                       data_ptr;
    typedef std::atomic<std::ptrdiff_t> size_type;
    typedef std::atomic<char*>          atom_ptr;

  private:
    ////////////////////////////////////////////////////////////////////////////
    // PRIVATE MEMBERS
    ////////////////////////////////////////////////////////////////////////////
    // Beginning and end pointers don't need to be atomic because they don't 
    // change. used_ and free_ can be negative in certain cases (and that's ok).

    data_ptr  beg_;  // pointer to beginning of data block
    data_ptr  end_;  // pointer to end of data block

    alignas(64)
    size_type used_; // size of unreserved used space

    alignas(64)
    size_type free_; // size of unreserved free space

    alignas(64)
    atom_ptr  rbuf_; // pointer to beginning of data being read
    atom_ptr  rptr_; // pointer to beginning of data

    alignas(64)
    atom_ptr  wptr_; // pointer to end of data
    atom_ptr  wbuf_; // pointer to end of data being written

  public:
    ////////////////////////////////////////////////////////////////////////////
    // CONSTRUCTORS AND DESTRUCTORS
    ////////////////////////////////////////////////////////////////////////////

    // Constructs a ring without a buffer (capacity() == 0)
    Ring_();

    // Constructs a ring with a buffer with a size
    Ring_(std::size_t size);

    // Moves the buffer between rings, assumes no concurrent operations
    Ring_(Ring_&& ring);

    // Moves the buffer between rings, assumes no concurrent operations on
    // either ring. Deallocates the buffer
    Ring_& operator= (Ring_&& ring);

    // No copying
    Ring_(const Ring_&)             = delete;
    Ring_& operator= (const Ring_&) = delete;

    // Deallocates the buffer
    ~Ring_();

  public:
    ////////////////////////////////////////////////////////////////////////////
    // QUERY FUNCTIONS
    ////////////////////////////////////////////////////////////////////////////
    // Functions only report on the state of the ring

    // Returns the current amount of non-reserved used space (amount of written 
    // data that a read hasn't yet reserved). Over-reserved scenarios mean this
    // number is not the ultimate source of truth with concurrent operations,
    // but its the closest safe approximation. This, of course, doesn't report
    // writes that have not completed.
    std::size_t size() const;

    // Maximum amount of data that can be held
    std::size_t capacity() const;

  public:
    ////////////////////////////////////////////////////////////////////////////
    // ACCESSORS AND MODIFIERS
    ////////////////////////////////////////////////////////////////////////////
    // All operations assume object has not been moved. Blocking operations run
    // until operation is completed. Non-blocking operations fail if there is
    // not enough space

    void read(void* data, std::size_t length) noexcept;
    void write(const void* data, std::size_t length) noexcept;
    bool try_read(void* data, std::size_t length) noexcept;
    bool try_write(const void* data, std::size_t length) noexcept;

  protected:
    ////////////////////////////////////////////////////////////////////////////
    // PROTECTED FUNCTIONS
    ////////////////////////////////////////////////////////////////////////////
    // Helper functions

    // Wraps a pointer within the array. Assumes 'beg_ <= ptr < end_+capacity()'
    char* normalize_(char*);

    char* acquire_read_block_(std::size_t length);
    char* try_acquire_read_block_(std::size_t length);
    void  copy_read_block_(const char* block, char* data, std::size_t length);
    void  release_read_block_(char* block, std::size_t length);

    char* acquire_write_block_(std::size_t length);
    char* try_acquire_write_block_(std::size_t length);
    void  copy_write_block_(char* block, const char* data, std::size_t length);
    void  release_write_block_(char* block, std::size_t length);

    char* begin_alloc_()             { return beg_;  }
    const char* begin_alloc_() const { return beg_;  }
    char* end_alloc_()               { return end_;  }
    const char* end_alloc_() const   { return end_;  }
    char* begin_data_()              { return rptr_; }
    const char* begin_data_() const  { return rptr_; }
    char* end_data_()                { return wptr_; }
    const char* end_data_() const    { return wptr_; }

  }; // class Ring_

  template <class T>
  class Ring : protected Ring_
  {
  public:
    ////////////////////////////////////////////////////////////////////////////
    // CONSTRUCTORS AND DESTRUCTORS
    ////////////////////////////////////////////////////////////////////////////

    // Constructs a ring without a buffer (capacity() == 0)
    Ring();

    // Constructs a ring with a buffer with a size
    Ring(std::size_t size);

    // Moves the buffer between rings, assumes no concurrent operations
    Ring(Ring<T>&& ring);

    // Moves the buffer between rings, assumes no concurrent operations on
    // either ring. Deallocates the buffer
    Ring<T>& operator= (Ring<T>&& ring);

    // No copying
    Ring(const Ring_&)             = delete;
    Ring& operator= (const Ring_&) = delete;

    // Deallocates the buffer, destructs stored data.
    ~Ring();

  public:
    ////////////////////////////////////////////////////////////////////////////
    // QUERY FUNCTIONS
    ////////////////////////////////////////////////////////////////////////////
    // Functions only report on the state of the ring

    // Returns the current amount of non-reserved used space (amount of written 
    // data that a read hasn't yet reserved). Over-reserved scenarios mean this
    // number is not the ultimate source of truth with concurrent operations,
    // but its the closest safe approximation. This, of course, doesn't report
    // writes that have not completed.
    std::size_t size() const;

    // Maximum amount of data that can be held
    std::size_t capacity() const;

  public:
    ////////////////////////////////////////////////////////////////////////////
    // ACCESSORS AND MODIFIERS
    ////////////////////////////////////////////////////////////////////////////
    // All operations assume object has not been moved. Blocking operations run
    // until operation is completed. Non-blocking operations fail if there is
    // not enough space

    void read(T& data) noexcept;            // blocking read
    void write(const T& data) noexcept;     // blocking write
    void write(T&& data) noexcept;          // blocking write
    bool try_read(T& data) noexcept;        // non-blocking read
    bool try_write(const T& data) noexcept; // non-blocking write
    bool try_write(T&& data) noexcept;      // non-blocking write

  private:
    ////////////////////////////////////////////////////////////////////////////
    // PRIVATE HELPER FUNCTIONS
    ////////////////////////////////////////////////////////////////////////////

    void destruct_();

  }; // class Ring<T>

  template <class T>
  Ring<T>::Ring()
    : Ring_()
  { }

  template <class T>
  Ring<T>::Ring(std::size_t size)
    : Ring_(size * sizeof(T))
  { }

  template <class T>
  Ring<T>::Ring(Ring<T>&& ring)
    : Ring_(std::move(ring))
  { }

  template <class T>
  Ring<T>& Ring<T>::operator= (Ring<T>&& ring)
  {
    destruct_();

    Ring_::operator= (ring);

    return *this;
  }

  template <class T>
  Ring<T>::~Ring()
  {
    destruct_();
  }

  template <class T>
  void Ring<T>::destruct_()
  {
    if (size() == 0)
      return;

    auto itr = begin_data_();
    auto end = end_data_();
    do
    {
      auto t = reinterpret_cast<T*>(itr);
      t->~T();

      itr = normalize_(itr + sizeof(T));
    } while (itr != end);
  }

  template <class T>
  std::size_t Ring<T>::size() const
  {
    return Ring_::size() / sizeof(T);
  }

  template <class T>
  std::size_t Ring<T>::capacity() const
  {
    return Ring_::capacity() / sizeof(T);
  }

  template <class T>
  void Ring<T>::read(T& data) noexcept
  {
    static_assert(std::is_nothrow_move_assignable<T>::value, "T move assignment must not throw");
    static_assert(std::is_nothrow_destructible<T>::value, "T destructor must not throw");

    auto block = acquire_read_block_(sizeof(T));

    // critical section
    auto t = reinterpret_cast<T*>(block);
    data = std::move(*t);
    t->~T();

    release_read_block_(block, sizeof(T));
  }

  template <class T>
  void Ring<T>::write(const T& data) noexcept
  {
    static_assert(std::is_nothrow_copy_constructible<T>::value, "T copy constructor must not throw");

    auto block = acquire_write_block_(sizeof(T));

    // critical section
    new(block) T(data);

    release_write_block_(block, sizeof(T));
  }

  template <class T>
  void Ring<T>::write(T&& data) noexcept
  {
    static_assert(std::is_nothrow_move_constructible<T>::value, "T move constructor must not throw");

    auto block = acquire_write_block_(sizeof(T));

    // critical section
    new(block) T(std::move(data));

    release_write_block_(block, sizeof(T));
  }

  template <class T>
  bool Ring<T>::try_read(T& data) noexcept
  {
    static_assert(std::is_nothrow_move_assignable<T>::value, "T move assignment must not throw");
    static_assert(std::is_nothrow_destructible<T>::value, "T destructor must not throw");

    auto block = try_acquire_read_block_(sizeof(T));
    if (block == nullptr)
      return false;

    // critical section
    auto t = reinterpret_cast<T*>(block);
    data = std::move(*t);
    t->~T();

    release_read_block_(block, sizeof(T));

    return true;
  }

  template <class T>
  bool Ring<T>::try_write(const T& data) noexcept
  {
    static_assert(std::is_nothrow_copy_constructible<T>::value, "T copy constructor must not throw");

    auto block = try_acquire_write_block_(sizeof(T));
    if (block == nullptr)
      return false;

    // critical section
    new(block) T(data);

    release_write_block_(block, sizeof(T));

    return true;
  }

  template <class T>
  bool Ring<T>::try_write(T&& data) noexcept
  {
    static_assert(std::is_nothrow_move_constructible<T>::value, "T move constructor must not throw");

    auto block = try_acquire_write_block_(sizeof(T));
    if (block == nullptr)
      return false;

    // critical section
    new(block) T(std::move(data));

    release_write_block_(block, sizeof(T));

    return true;
  }

} // namespace wilt

#endif // !WILT_RING_H