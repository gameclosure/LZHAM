// File: lzham_lzcomp_internal.h
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
#include "lzham_match_accel.h"
#include "lzham_symbol_codec.h"
#include "lzham_task_pool.h"
#include "lzham_lzbase.h"

namespace lzham
{
   typedef lzham::vector<uint8> byte_vec;

   const uint cMaxParseGraphNodes = 3072;
   const uint cMaxParseThreads = 4;

   enum compression_level
   {
      cCompressionLevelFastest,
      cCompressionLevelFaster,
      cCompressionLevelDefault,
      cCompressionLevelBetter,
      cCompressionLevelUber,

      cCompressionLevelCount
   };

   struct comp_settings
   {
      uint m_fast_bytes;
      bool m_fast_adaptive_huffman_updating;
      bool m_use_polar_codes;
      uint m_match_accel_max_matches_per_probe;
      uint m_match_accel_max_probes;
   };
      
   class lzcompressor : public CLZBase
   {
   public:
      lzcompressor();

      struct init_params
      {
         enum
         {
            cMinDictSizeLog2 = CLZBase::cMinDictSizeLog2,
            cMaxDictSizeLog2 = CLZBase::cMaxDictSizeLog2,
            cDefaultBlockSize = 1024U*512U
         };

         init_params() :
            m_pTask_pool(NULL),
            m_max_helper_threads(0),
            m_compression_level(cCompressionLevelDefault),
            m_dict_size_log2(22),
            m_block_size(cDefaultBlockSize),
            m_num_cachelines(0),
            m_cacheline_size(0),
            m_lzham_compress_flags(0)
         {
         }

         task_pool* m_pTask_pool;
         uint m_max_helper_threads;

         compression_level m_compression_level;
         uint m_dict_size_log2;

         uint m_block_size;

         uint m_num_cachelines;
         uint m_cacheline_size;
         
         uint m_lzham_compress_flags;
      };

      bool init(const init_params& params);
      void clear();

      bool put_bytes(const void* pBuf, uint buf_len);

      const byte_vec& get_compressed_data() const   { return m_comp_buf; }
            byte_vec& get_compressed_data()         { return m_comp_buf; }

      uint32 get_src_adler32() const { return m_src_adler32; }

   private:
      class state;
      
      enum
      {
         cLitComplexity = 1,
         cRep0Complexity = 2,
         
         cLongMatchComplexity = 5,
         cLongMatchComplexityLenThresh = 9,
         
         cShortMatchComplexity = 6
      };

      struct lzdecision
      {
         int m_pos;  // dict position where decision was evaluated
         int m_len;  // 0 if literal, 1+ if match
         int m_dist; // <0 if match rep, else >=1 is match dist
         
         inline lzdecision() { }
         inline lzdecision(int pos, int len, int dist) : m_pos(pos), m_len(len), m_dist(dist) { LZHAM_ASSERT(len <= CLZBase::cMaxMatchLen); }

         // does not init m_cost
         inline void init(int pos, int len, int dist) { m_pos = pos; m_len = len; m_dist = dist; LZHAM_ASSERT(len <= CLZBase::cMaxMatchLen); }

         inline bool is_lit() const { return !m_len; }
         inline bool is_match() const { return m_len > 0; } // may be a rep or full match
         inline bool is_full_match() const { return (m_len > 0) && (m_dist >= 1); }
         inline uint get_len() const { return math::maximum<uint>(m_len, 1); }
         inline bool is_rep() const { return m_dist < 0; }
         inline bool is_rep0() const { return m_dist == -1; }

         uint get_match_dist(const state& s) const;

         inline uint get_complexity() const
         {
            if (is_lit())
               return cLitComplexity;
            else if (is_rep())
            {
               LZHAM_ASSUME(cRep0Complexity == 2);
               return 1 + -m_dist;  // 2, 3, 4
            }
            else if (get_len() >= cLongMatchComplexityLenThresh)
               return cLongMatchComplexity;
            else
               return cShortMatchComplexity;
         }

         inline uint get_min_codable_len() const
         {
            if (is_lit() || is_rep0())
               return 1;
            else
               return CLZBase::cMinMatchLen;
         }
      };
      
      struct saved_state
      {
         uint m_cur_ofs;
         uint m_cur_state;
         uint m_match_hist[CLZBase::cMatchHistSize];

         inline bool operator== (const saved_state &rhs) const
         {
            if (m_cur_state != rhs.m_cur_state)
               return false;
            for (uint i = 0; i < CLZBase::cMatchHistSize; i++)
               if (m_match_hist[i] != rhs.m_match_hist[i])
                  return false;
            return true;
         }

         void partial_advance(const lzdecision& lzdec);
         
         inline void save_partial_state(saved_state& dst)
         {
            dst.m_cur_ofs = m_cur_ofs;
            dst.m_cur_state = m_cur_state;
            memcpy(dst.m_match_hist, m_match_hist, sizeof(m_match_hist));
         }

         inline void restore_partial_state(const saved_state& src)
         {
            m_cur_ofs = src.m_cur_ofs;
            m_cur_state = src.m_cur_state;
            memcpy(m_match_hist, src.m_match_hist, sizeof(m_match_hist));
         }
      };

      class state : public saved_state
      {
      public:
         state();

         void clear();

         bool init(CLZBase& lzbase, bool fast_adaptive_huffman_updating, bool use_polar_codes);

         float get_len2_match_cost(CLZBase& lzbase, uint len2_match_dist, uint is_match_model_index);
         float get_cost(CLZBase& lzbase, const search_accelerator& dict, const lzdecision& lzdec) const;
         float get_lit_cost(const search_accelerator& dict, uint dict_pos, uint lit_pred0, uint is_match_model_index) const;

         // Returns actual cost.
         void get_rep_match_costs(float *pBitcosts, uint match_hist_index, int min_len, int max_len, uint is_match_model_index) const;
         void get_full_match_costs(CLZBase& lzbase, float *pBitcosts, uint match_dist, int min_len, int max_len, uint is_match_model_index) const;

         float update_stats(CLZBase& lzbase, const search_accelerator& dict, const lzdecision& lzdec);

         bool encode(symbol_codec& codec, CLZBase& lzbase, const search_accelerator& dict, const lzdecision& lzdec);
         bool encode_eob(symbol_codec& codec, const search_accelerator& dict);
         bool encode_reset_state_partial(symbol_codec& codec, const search_accelerator& dict);

         void update_match_hist(uint match_dist);
         int find_match_dist(uint match_hist) const;

         void reset_state_partial();
         void start_of_block(const search_accelerator& dict, uint cur_ofs);

         uint get_pred_char(const search_accelerator& dict, int pos, int backward_ofs) const;

         inline bool will_reference_last_match(const lzdecision& lzdec) const
         {
            return (!lzdec.is_match()) &&  (m_cur_state >= CLZBase::cNumLitStates);
         }
         
         uint m_block_ofs;

         adaptive_bit_model m_is_match_model[(1 << CLZBase::cNumIsMatchContextBits) * 16];//CLZBase::cNumStates];

         adaptive_bit_model m_is_rep_model[CLZBase::cNumStates];
         adaptive_bit_model m_is_rep0_model[CLZBase::cNumStates];
         adaptive_bit_model m_is_rep0_single_byte_model[CLZBase::cNumStates];
         adaptive_bit_model m_is_rep1_model[CLZBase::cNumStates];

         typedef quasi_adaptive_huffman_data_model sym_data_model;
         sym_data_model m_lit_table[1 << CLZBase::cNumLitPredBits];
         sym_data_model m_delta_lit_table[1 << CLZBase::cNumDeltaLitPredBits];

         sym_data_model m_main_table;
         sym_data_model m_rep_len_table[2];
         sym_data_model m_large_len_table[2];
         sym_data_model m_dist_lsb_table;
      };

      struct coding_stats
      {
         coding_stats() { clear(); }

         void clear();

         void update(const lzdecision& lzdec, const state& cur_state, const search_accelerator& dict, float cost);
         void print();

         uint m_total_bytes;
         uint m_total_contexts;
         double m_total_cost;

         double m_total_match_bits_cost;
         double m_worst_match_bits_cost;
         double m_total_match0_bits_cost;
         double m_total_match1_bits_cost;
         uint m_total_nonmatches;
         uint m_total_matches;

         uint m_total_lits;
         double m_total_lit_cost;
         double m_worst_lit_cost;

         uint m_total_delta_lits;
         double m_total_delta_lit_cost;
         double m_worst_delta_lit_cost;

         uint m_total_reps;
         uint m_total_rep0_len1_matches;
         double m_total_rep0_len1_cost;
         double m_worst_rep0_len1_cost;

         uint m_total_rep_matches[CLZBase::cMatchHistSize];
         double m_total_rep_cost[CLZBase::cMatchHistSize];
         double m_worst_rep_cost[CLZBase::cMatchHistSize];

         uint m_total_full_matches[cMaxMatchLen + 1];
         double m_total_full_match_cost[cMaxMatchLen + 1];
         double m_worst_full_match_cost[cMaxMatchLen + 1];
         
         uint m_total_far_len2_matches;
         uint m_total_near_len2_matches;
      };

      init_params m_params;
      comp_settings m_settings;

      int64 m_src_size;
      uint32 m_src_adler32;

      search_accelerator m_accel;

      symbol_codec m_codec;

      coding_stats m_stats;

      byte_vec m_block_buf;
      byte_vec m_comp_buf;

      uint m_step;

      uint m_start_dict_ofs;

      uint m_block_index;

      bool m_finished;
      bool m_use_task_pool;
      
      struct node_state
      {
         LZHAM_FORCE_INLINE void clear()
         {
            m_total_cost = math::cNearlyInfinite;
            m_total_complexity = UINT_MAX;
         }
         
         // the lzdecision that led from parent to this node_state
         lzdecision m_lzdec;                 
         
         // This is either the state of the parent node (optimal parsing), or the state of the child node (extreme parsing).
         state::saved_state m_saved_state;     
         
         // Total cost to arrive at this node state.
         float m_total_cost;                 
         uint m_total_complexity;
         
         // Parent node index.
         int16 m_parent_index;               
         
         // Parent node state index (only valid when extreme parsing).
         int8 m_parent_state_index;          
      };

      struct node
      {
         LZHAM_FORCE_INLINE void clear()
         {
            m_num_node_states = 0;
         }
         
         uint m_num_node_states;                                    
         enum { cMaxNodeStates = 4 };
         node_state m_node_states[cMaxNodeStates];
         
         void add_state(int parent_index, int parent_state_index, const lzdecision &lzdec, state &parent_state,float total_cost, uint total_complexity);
      };

      state m_initial_state;                    // state at start of block
      
      state m_state;                            // main thread's current coding state

      struct raw_parse_thread_state
      {
         uint m_start_ofs;
         uint m_bytes_to_match;

         state m_approx_state;

         node m_nodes[cMaxParseGraphNodes + 1];
                  
         lzham::vector<lzdecision> m_best_decisions;
         bool m_emit_decisions_backwards;

         bool m_issued_reset_state_partial;

         bool m_failed;
      };

      struct parse_thread_state : raw_parse_thread_state
      {
         uint8 m_unused_alignment_array[128 - (sizeof(raw_parse_thread_state) & 127)];
      };

      uint m_num_parse_threads;
      parse_thread_state m_parse_thread_state[cMaxParseThreads];

      volatile LONG m_parse_jobs_remaining;
      semaphore m_parse_jobs_complete;

      bool send_final_block();
      bool send_configuration();
      bool greedy_parse(parse_thread_state &parse_state);
      bool extreme_parse(parse_thread_state &parse_state);
      bool optimal_parse(parse_thread_state &parse_state);
      void parse_job_callback(uint64 data, void* pData_ptr);
      bool compress_block(const void* pBuf, uint buf_len);
      bool code_decision(lzdecision lzdec, uint& cur_ofs, uint& bytes_to_match);
   };

} // namespace lzham



