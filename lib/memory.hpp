#pragma once

#include <memory.h>
   
   class memory_keeper
   {
   friend void* malloc(uint32_t size);
   friend void* realloc(void* ptr, uint32_t size);
   friend void free(void* ptr);
   public:
      memory_keeper()
      : _heaps_actual_size(0)
      , _active_heap(0)
      , _active_free_heap(0)
      {
      }

   private:
      class memory;

      memory* next_active_heap()
      {
         memory* const current_memory = _available_heaps + _active_heap;

         auto remaining = 1024 * 1024 - reinterpret_cast<int32_t>(sbrk(0));
         if (remaining <= 0)
         {
            current_memory->cleanup_remaining();
            ++_active_heap;
            _heaps_actual_size = _active_heap;
            return nullptr;
         }

         const uint32_t new_heap_size = remaining > _new_heap_size ? _new_heap_size : remaining;
         char* new_memory_start = static_cast<char*>(sbrk(new_heap_size));
         if (current_memory->expand_memory(new_memory_start, new_heap_size))
            return current_memory;

         current_memory->cleanup_remaining();

         ++_active_heap;
         memory* const next = _available_heaps + _active_heap;
         next->init(new_memory_start, new_heap_size);

         return next;
      }

      void* malloc(uint32_t size)
      {
         if (size == 0)
            return nullptr;

         if (_heaps_actual_size == 0)
            _heaps_actual_size = _heaps_size;

         modify_mem_block(size);

         uint32_t needs_init = 0;
         char* buffer = nullptr;
         memory* current = nullptr;
         if (_active_heap < _heaps_actual_size)
         {
            memory* const start_heap = &_available_heaps[_active_heap];
            if(_active_heap == 0 && !start_heap->is_init())
            {
               start_heap->init(_initial_heap, _initial_heap_size);
            }

            current = start_heap;
         }

         while (current != nullptr)
         {
            buffer = current->malloc(size);
            if (buffer != nullptr)
               break;

            current = next_active_heap();
         }

         if (buffer == nullptr)
         {
            const uint32_t end_free_heap = _active_free_heap;

            do
            {
               buffer = _available_heaps[_active_free_heap].malloc_from_freed(size);

               if (buffer != nullptr)
                  break;

               if (++_active_free_heap == _heaps_actual_size)
                  _active_free_heap = 0;

            } while (_active_free_heap != end_free_heap);
         }

         return buffer;
      }

      void* realloc(void* ptr, uint32_t size)
      {
         if (size == 0)
         {
            free(ptr);
            return nullptr;
         }

         const uint32_t REMOVE = size;
         modify_mem_block(size);

         char* realloc_ptr = nullptr;
         uint32_t orig_ptr_size = 0;
         if (ptr != nullptr)
         {
            char* const char_ptr = static_cast<char*>(ptr);
            for (memory* realloc_heap = _available_heaps; realloc_heap < _available_heaps + _heaps_actual_size && realloc_heap->is_init(); ++realloc_heap)
            {
               if (realloc_heap->is_in_heap(char_ptr))
               {
                  realloc_ptr = realloc_heap->realloc_in_place(char_ptr, size, &orig_ptr_size);

                  if (realloc_ptr != nullptr)
                     return realloc_ptr;
                  else
                     break;
               }
            }
         }

         char* new_alloc = static_cast<char*>(malloc(size));
         if (new_alloc == nullptr)
            return nullptr;

         const uint32_t copy_size = (size < orig_ptr_size) ? size : orig_ptr_size;
         if (copy_size > 0)
         {
            memcpy(new_alloc, ptr, copy_size);
            free (ptr);
         }

         return new_alloc;
      }

      void free(void* ptr)
      {
         if (ptr == nullptr)
            return;

         char* const char_ptr = static_cast<char*>(ptr);
         for (memory* free_heap = _available_heaps; free_heap < _available_heaps + _heaps_actual_size && free_heap->is_init(); ++free_heap)
         {
            if (free_heap->is_in_heap(char_ptr))
            {
               free_heap->free(char_ptr);
               break;
            }
         }
      }

      void modify_mem_block(uint32_t& size)
      {
         const uint32_t remainder = (size + _size_marker) & _rem_mem_block_mask;
         if (remainder > 0)
         {
            size += _mem_block - remainder;
         }
      }

      class memory
      {
      public:
         memory()
         : _heap_size(0)
         , _heap(nullptr)
         , _offset(0)
         {
         }

         void init(char* const mem_heap, uint32_t size)
         {
            _heap_size = size;
            _heap = mem_heap;
            memset(_heap, 0, _heap_size);
         }

         uint32_t is_init() const
         {
            return _heap != nullptr;
         }

         uint32_t is_in_heap(const char* const ptr) const
         {
            const char* const end_of_buffer = _heap + _heap_size;
            const char* const first_ptr_of_buffer = _heap + _size_marker;
            return ptr >= first_ptr_of_buffer && ptr < end_of_buffer;
         }

         uint32_t is_capacity_remaining() const
         {
            return _offset + _size_marker < _heap_size;
         }

         char* malloc(uint32_t size)
         {
            uint32_t used_up_size = _offset + size + _size_marker;
            if (used_up_size > _heap_size)
            {
               return nullptr;
            }

            buffer_ptr new_buff(&_heap[_offset + _size_marker], size, _heap + _heap_size);
            _offset += size + _size_marker;
            new_buff.mark_alloc();
            return new_buff.ptr();
         }

         char* malloc_from_freed(uint32_t size)
         {
            assert(_offset == _heap_size, "malloc_from_freed was designed to only be called after _heap was completely allocated");

            char* current = _heap + _size_marker;
            while (current != nullptr)
            {
               buffer_ptr current_buffer(current, _heap + _heap_size);
               if (!current_buffer.is_alloc())
               {
                  if (current_buffer.merge_contiguous(size))
                  {
                     current_buffer.mark_alloc();
                     return current;
                  }
               }

               current = current_buffer.next_ptr();
            }

            return nullptr;
         }

         char* realloc_in_place(char* const ptr, uint32_t size, uint32_t* orig_ptr_size)
         {
            const char* const end_of_buffer = _heap + _heap_size;

            buffer_ptr orig_buffer(ptr, end_of_buffer);
            *orig_ptr_size = orig_buffer.size();
            char* const orig_buffer_end = orig_buffer.end();
            if (orig_buffer_end > end_of_buffer)
            {
               *orig_ptr_size = 0;
               return nullptr;
            }

            if (ptr > end_of_buffer - size)
            {
               return nullptr;
            }

            const int32_t diff = size - *orig_ptr_size;
            if (diff < 0)
            {
               char* const new_ptr = ptr + size + _size_marker;
               buffer_ptr excess_to_free(new_ptr, -diff, _heap + _heap_size);
               excess_to_free.mark_free();

               return ptr;
            }
            else if (orig_buffer_end == &_heap[_offset])
            {
               orig_buffer.size(size);
               _offset += diff;

               return ptr;
            }
            if (-diff == 0)
               return ptr;

            if (!orig_buffer.merge_contiguous_if_available(size))
               return nullptr;

            orig_buffer.mark_alloc();
            return ptr;
         }

         void free(char* ptr)
         {
            buffer_ptr to_free(ptr, _heap + _heap_size);
            to_free.mark_free();
         }

         void cleanup_remaining()
         {
            if (_offset == _heap_size)
               return;

            const uint32_t size = _heap_size - _offset - _size_marker;
            buffer_ptr new_buff(&_heap[_offset + _size_marker], size, _heap + _heap_size);
            _offset = _heap_size;
            new_buff.mark_free();
         }

         bool expand_memory(char* exp_mem, uint32_t size)
         {
            if (_heap + _heap_size != exp_mem)
               return false;

            _heap_size += size;

            return true;
         }

      private:
         class buffer_ptr
         {
         public:
            buffer_ptr(void* ptr, const char* const heap_end)
            : _ptr(static_cast<char*>(ptr))
            , _size(*reinterpret_cast<uint32_t*>(static_cast<char*>(ptr) - _size_marker) & ~_alloc_memory_mask)
            , _heap_end(heap_end)
            {
            }

            buffer_ptr(void* ptr, uint32_t buff_size, const char* const heap_end)
            : _ptr(static_cast<char*>(ptr))
            , _heap_end(heap_end)
            {
               size(buff_size);
            }

            uint32_t size() const
            {
               return _size;
            }

            char* next_ptr() const
            {
               char* const next = end() + _size_marker;
               if (next >= _heap_end)
                  return nullptr;

               return next;
            }

            void size(uint32_t val)
            {
               const uint32_t memory_state = *reinterpret_cast<uint32_t*>(_ptr - _size_marker) & _alloc_memory_mask;
               *reinterpret_cast<uint32_t*>(_ptr - _size_marker) = val | memory_state;
               _size = val;
            }

            char* end() const
            {
               return _ptr + _size;
            }

            char* ptr() const
            {
               return _ptr;
            }

            void mark_alloc()
            {
               *reinterpret_cast<uint32_t*>(_ptr - _size_marker) |= _alloc_memory_mask;
            }

            void mark_free()
            {
               *reinterpret_cast<uint32_t*>(_ptr - _size_marker) &= ~_alloc_memory_mask;
            }

            bool is_alloc() const
            {
               return *reinterpret_cast<const uint32_t*>(_ptr - _size_marker) & _alloc_memory_mask;
            }

            bool merge_contiguous_if_available(uint32_t needed_size)
            {
               return merge_contiguous(needed_size, true);
            }

            bool merge_contiguous(uint32_t needed_size)
            {
               return merge_contiguous(needed_size, false);
            }
         private:
            bool merge_contiguous(uint32_t needed_size, bool all_or_nothing)
            {
               if (all_or_nothing && _heap_end - _ptr < needed_size)
                  return false;

               uint32_t possible_size = _size;
               while (possible_size < needed_size  && (_ptr + possible_size < _heap_end))
               {
                  const uint32_t next_mem_flag_size = *reinterpret_cast<const uint32_t*>(_ptr + possible_size);
                  if (next_mem_flag_size & _alloc_memory_mask)
                     break;

                  possible_size += (next_mem_flag_size & ~_alloc_memory_mask) + _size_marker;
               }

               if (all_or_nothing && possible_size < needed_size)
                  return false;

               const uint32_t new_size = possible_size < needed_size ? possible_size : needed_size;
               size(new_size);

               if (possible_size > needed_size)
               {
                  const uint32_t freed_size = possible_size - needed_size - _size_marker;
                  buffer_ptr freed_remainder(_ptr + needed_size + _size_marker, freed_size, _heap_end);
                  freed_remainder.mark_free();
               }

               return new_size == needed_size;
            }

            char* _ptr;
            uint32_t _size;
            const char* const _heap_end;
         };

         uint32_t _heap_size;
         char* _heap;
         uint32_t _offset;
      };

      static const uint32_t _size_marker = sizeof(uint32_t);
      static const uint32_t _mem_block = 8;
      static const uint32_t _rem_mem_block_mask = _mem_block - 1;
      static const uint32_t _initial_heap_size = 8192;
      static const uint32_t _new_heap_size = 65536;
      static const uint32_t _heaps_size = 16;
      char _initial_heap[_initial_heap_size];
      memory _available_heaps[_heaps_size];
      uint32_t _heaps_actual_size;
      uint32_t _active_heap;
      uint32_t _active_free_heap;
      static const uint32_t _alloc_memory_mask = 1 << 31;
   } memory_heap;

   inline void* malloc(uint32_t size)
   {
      return memory_heap.malloc(size);
   }

   inline void* realloc(void* ptr, uint32_t size)
   {
      return memory_heap.realloc(ptr, size);
   }

    inline void free(void* ptr)
    {
       return memory_heap.free(ptr);
    }
