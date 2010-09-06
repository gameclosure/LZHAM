// File: lzham_match_accel.h
//
// Copyright (c) 2009-2010 Richard Geldreich, Jr. <richgel99@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#pragma once
#include "lzham_lzbase.h"
#include "lzham_task_pool.h"

namespace lzham
{
   const uint cMatchAccelMaxSupportedProbes = 128;
      
   struct node
   {
      uint m_left;
      uint m_right;
   };
   
   LZHAM_DEFINE_BITWISE_MOVABLE(node);
   
#pragma pack(push, 1)      
   struct dict_match
   {
      uint m_dist;
      uint8 m_len;

      inline uint get_dist() const { return m_dist & 0x7FFFFFFF; }
      inline uint get_len() const { return m_len + 2; }
      inline bool is_last() const { return (int)m_dist < 0; }
   };
#pragma pack(pop)  

   LZHAM_DEFINE_BITWISE_MOVABLE(dict_match);
   
   class search_accelerator
   {
   public:
      search_accelerator();

      // If all_matches is true, the match finder returns all found matches with no filtering.
      // Otherwise, the finder will tend to return lists of matches with mostly unique lengths.
      // For each length, it will discard matches with worse distances (in the coding sense).
      bool init(CLZBase* pLZBase, task_pool* pPool, uint max_helper_threads, uint max_dict_size, uint max_matches, bool all_matches, uint max_probes);
      
      inline uint get_max_dict_size() const { return m_max_dict_size; }
      inline uint get_max_dict_size_mask() const { return m_max_dict_size_mask; }
      inline uint get_cur_dict_size() const { return m_cur_dict_size; }
      
      inline uint get_lookahead_pos() const { return m_lookahead_pos; }
      inline uint get_lookahead_size() const { return m_lookahead_size; }
      
      inline uint get_char(int delta_pos) const { return m_dict[(m_lookahead_pos + delta_pos) & m_max_dict_size_mask]; }
      inline const uint8* get_ptr(uint pos) const { return &m_dict[pos]; }
      
      inline uint operator[](uint pos) const { return m_dict[pos]; }
            
      uint get_max_add_bytes() const;
      bool add_bytes_begin(uint num_bytes, const uint8* pBytes);
      void add_bytes_end();
      
      uint get_len2_match(uint lookahead_ofs);
      dict_match* find_matches(uint lookahead_ofs, bool spin = true);
            
      void advance_bytes(uint num_bytes);
      
      LZHAM_FORCE_INLINE uint get_match_len(uint lookahead_ofs, int dist, uint max_match_len) const
      {
         LZHAM_ASSERT(lookahead_ofs < m_lookahead_size);

         const int find_dict_size = m_cur_dict_size + lookahead_ofs;
         if (dist > find_dict_size)
            return 0;

         const uint comp_pos = static_cast<uint>((m_lookahead_pos + lookahead_ofs - dist) & m_max_dict_size_mask);
         const uint lookahead_pos = m_lookahead_pos + lookahead_ofs;
         
         const uint8* pComp = &m_dict[comp_pos];
         const uint8* pLookahead = &m_dict[lookahead_pos];
         
         uint match_len;
         for (match_len = 0; match_len < max_match_len; match_len++)
            if (pComp[match_len] != pLookahead[match_len])
               break;

         return match_len;
      }
            
   public:
      CLZBase* m_pLZBase;
      task_pool* m_pTask_pool;
      uint m_max_helper_threads;
   
      uint m_max_dict_size;
      uint m_max_dict_size_mask;
      
      uint m_lookahead_pos;
      uint m_lookahead_size;
                  
      uint m_cur_dict_size;
            
      lzham::vector<uint8> m_dict;
      
      enum { cHashSize = 65536 };
      lzham::vector<uint> m_hash;
      lzham::vector<node> m_nodes;

      lzham::vector<dict_match> m_matches;
      lzham::vector<LONG> m_match_refs;
      
      lzham::vector<uint8> m_hash_thread_index;
      
      lzham::vector<uint> m_digram_hash;
      lzham::vector<uint> m_digram_next;
                                          
      uint m_fill_lookahead_pos;
      uint m_fill_lookahead_size;
      uint m_fill_dict_size;
      
      uint m_max_probes;
      uint m_max_matches;
      
      bool m_all_matches;
                  
      volatile LONG m_next_match_ref;
                  
      void find_all_matches_callback(uint64 data, void* pData_ptr);
      bool find_all_matches(uint num_bytes);
      bool find_len2_matches();
   };

} // namespace lzham
