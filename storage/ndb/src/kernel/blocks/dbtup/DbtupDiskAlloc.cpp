/*
   Copyright (c) 2005, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#define DBTUP_C
#define DBTUP_DISK_ALLOC_CPP
#include "Dbtup.hpp"
#include <signaldata/LgmanContinueB.hpp>
#include "../dblqh/Dblqh.hpp"

#define JAM_FILE_ID 426

//#define DEBUG_LCP 1
#ifdef DEBUG_LCP
#define DEB_LCP(arglist) do { g_eventLogger->info arglist ; } while (0)
#else
#define DEB_LCP(arglist) do { } while (0)
#endif

//#define DEBUG_PGMAN 1
#ifdef DEBUG_PGMAN
#define DEB_PGMAN(arglist) do { g_eventLogger->info arglist ; } while (0)
#else
#define DEB_PGMAN(arglist) do { } while (0)
#endif

#define DEBUG_EXTENT_BITS 1
#ifdef DEBUG_EXTENT_BITS
#define DEB_EXTENT_BITS(arglist) do { g_eventLogger->info arglist ; } while (0)
#else
#define DEB_EXTENT_BITS(arglist) do { } while (0)
#endif

#define DEBUG_UNDO 1
#ifdef DEBUG_UNDO
#define DEB_UNDO(arglist) do { g_eventLogger->info arglist ; } while (0)
#else
#define DEB_UNDO(arglist) do { } while (0)
#endif

static
NdbOut&
operator<<(NdbOut& out, const Ptr<Dbtup::Page> & ptr)
{
  out << "[ Page: ptr.i: " << ptr.i 
      << " ["
      << " m_m_page_lsn_hi: " << ptr.p->m_page_header.m_page_lsn_hi
      << " m_m_page_lsn_lo: " << ptr.p->m_page_header.m_page_lsn_lo
      << " m_page_type: " << ptr.p->m_page_header.m_page_type
      << " m_file_no: " << ptr.p->m_file_no
      << " m_page_no: " << ptr.p->m_page_no
      << " m_table_id: " << ptr.p->m_table_id
      << " m_fragment_id: " << ptr.p->m_fragment_id
      << " m_extent_no: " << ptr.p->m_extent_no
      << " m_extent_info_ptr: " << ptr.p->m_extent_info_ptr
      << " m_restart_seq: " << ptr.p->m_restart_seq
      << "]"
      << " list_index: " << ptr.p->list_index 
      << " free_space: " << ptr.p->free_space
      << " uncommitted_used_space: " << ptr.p->uncommitted_used_space
      << " ]";
  return out;
}

static
NdbOut&
operator<<(NdbOut& out, const Ptr<Dbtup::Page_request> & ptr)
{
  out << "[ Page_request: ptr.i: " << ptr.i
      << " " << ptr.p->m_key
      << " m_original_estimated_free_space: " << ptr.p->m_original_estimated_free_space
      << " m_list_index: " << ptr.p->m_list_index
      << " m_frag_ptr_i: " << ptr.p->m_frag_ptr_i
      << " m_extent_info_ptr: " << ptr.p->m_extent_info_ptr
      << " m_ref_count: " << ptr.p->m_ref_count
      << " m_uncommitted_used_space: " << ptr.p->m_uncommitted_used_space
      << " ]";
  
  return out;
}

static
NdbOut&
operator<<(NdbOut& out, const Ptr<Dbtup::Extent_info> & ptr)
{
  out << "[ Extent_info: ptr.i " << ptr.i
      << " " << ptr.p->m_key
      << " m_first_page_no: " << ptr.p->m_first_page_no
      << " m_empty_page_no: " << ptr.p->m_empty_page_no
      << " m_key: ["
      << " m_file_no=" << ptr.p->m_key.m_file_no
      << " m_page_no=" << ptr.p->m_key.m_page_no
      << " m_page_idx=" << ptr.p->m_key.m_page_idx
      << " ]"
      << " m_free_space: " << ptr.p->m_free_space
      << " m_free_matrix_pos: " << ptr.p->m_free_matrix_pos
      << " m_free_page_count: [";

  for(Uint32 i = 0; i<Dbtup::EXTENT_SEARCH_MATRIX_COLS; i++)
    out << " " << ptr.p->m_free_page_count[i];
  out << " ] ]";

  return out;
}

void 
Dbtup::dump_disk_alloc(Dbtup::Disk_alloc_info & alloc)
{
  const Uint32 limit = 512;
  ndbout_c("dirty pages");
  for(Uint32 i = 0; i<MAX_FREE_LIST; i++)
  {
    printf("  %d : ", i);
    PagePtr ptr;
    Page_pool *pool= (Page_pool*)&m_global_page_pool;
    Local_Page_list list(*pool, alloc.m_dirty_pages[i]);
    Uint32 c = 0;
    for (list.first(ptr); c < limit && !ptr.isNull(); c++, list.next(ptr))
    {
      ndbout << ptr << " ";
    }
    if (c == limit)
    {
      ndbout << "MAXLIMIT ";
    }
    ndbout_c(" ");
  }
  ndbout_c("page requests");
  for(Uint32 i = 0; i<MAX_FREE_LIST; i++)
  {
    printf("  %d : ", i);
    Ptr<Page_request> ptr;
    Local_page_request_list list(c_page_request_pool, 
				 alloc.m_page_requests[i]);
    Uint32 c = 0;
    for (list.first(ptr); c < limit && !ptr.isNull(); c++, list.next(ptr))
    {
      ndbout << ptr << " ";
    }
    if (c == limit)
    {
      ndbout << "MAXLIMIT ";
    }
    ndbout_c(" ");
  }

  ndbout_c("Extent matrix");
  for(Uint32 i = 0; i<alloc.SZ; i++)
  {
    printf("  %d : ", i);
    Ptr<Extent_info> ptr;
    Local_extent_info_list list(c_extent_pool, alloc.m_free_extents[i]);
    Uint32 c = 0;
    for (list.first(ptr); c < limit && !ptr.isNull(); c++, list.next(ptr))
    {
      ndbout << ptr << " ";
    }
    if (c == limit)
    {
      ndbout << "MAXLIMIT ";
    }
    ndbout_c(" ");
  }

  if (alloc.m_curr_extent_info_ptr_i != RNIL)
  {
    Ptr<Extent_info> ptr;
    c_extent_pool.getPtr(ptr, alloc.m_curr_extent_info_ptr_i);
    ndbout << "current extent: " << ptr << endl;
  }
}

#if defined VM_TRACE || 1
#define ddassert(x) do { if(unlikely(!(x))) { dump_disk_alloc(alloc); ndbrequire(false); } } while(0)
#else
#define ddassert(x)
#endif

Dbtup::Disk_alloc_info::Disk_alloc_info(const Tablerec* tabPtrP, 
					Uint32 extent_size)
{
  m_extent_size = extent_size;
  m_curr_extent_info_ptr_i = RNIL; 
  if (tabPtrP->m_no_of_disk_attributes == 0)
    return;
  
  Uint32 min_size= 4*tabPtrP->m_offsets[DD].m_fix_header_size;
  
  if (tabPtrP->m_attributes[DD].m_no_of_varsize == 0)
  {
    Uint32 recs_per_page= (4*Tup_fixsize_page::DATA_WORDS)/min_size;
    m_page_free_bits_map[0] = recs_per_page; // 100% free
    m_page_free_bits_map[1] = 1;
    m_page_free_bits_map[2] = 0;
    m_page_free_bits_map[3] = 0;
    
    Uint32 max= recs_per_page * extent_size;
    for(Uint32 i = 0; i<EXTENT_SEARCH_MATRIX_ROWS; i++)
    {
      m_total_extent_free_space_thresholds[i] = 
	(EXTENT_SEARCH_MATRIX_ROWS - i - 1)*max/EXTENT_SEARCH_MATRIX_ROWS;
    }
  }
  else
  {
    abort();
  }
}

Uint32
Dbtup::Disk_alloc_info::find_extent(Uint32 sz) const
{
  /**
   * Find an extent with sufficient space for sz
   * Find the biggest available (with most free space)
   * Return position in matrix
   */
  Uint32 col = calc_page_free_bits(sz);
  Uint32 mask= EXTENT_SEARCH_MATRIX_COLS - 1;
  for(Uint32 i= 0; i<EXTENT_SEARCH_MATRIX_SIZE; i++)
  {
    // Check that it can cater for request
    if (!m_free_extents[i].isEmpty())
    {
      return i;
    }
    
    if ((i & mask) >= col)
    {
      i = (i & ~mask) + mask;
    }
  }
  
  return RNIL;
}

Uint32
Dbtup::Disk_alloc_info::calc_extent_pos(const Extent_info* extP) const
{
  Uint32 free= extP->m_free_space;
  Uint32 mask= EXTENT_SEARCH_MATRIX_COLS - 1;
  
  Uint32 col= 0, row=0;
  
  /**
   * Find correct row based on total free space
   *   if zero (or very small free space) put 
   *     absolutly last
   */
  {    
    const Uint32 *arr= m_total_extent_free_space_thresholds;
    for(; free < * arr++; row++)
      assert(row < EXTENT_SEARCH_MATRIX_ROWS);
  }

  /**
   * Find correct col based on largest available chunk
   */
  {
    const Uint16 *arr= extP->m_free_page_count;
    for(; col < EXTENT_SEARCH_MATRIX_COLS && * arr++ == 0; col++);
  }

  /**
   * NOTE
   *
   *   If free space on extent is small or zero,
   *     col will be = EXTENT_SEARCH_MATRIX_COLS
   *     row will be = EXTENT_SEARCH_MATRIX_ROWS
   *   in that case pos will be col * row = max pos
   *   (as fixed by + 1 in declaration)
   */
  Uint32 pos= (row * (mask + 1)) + (col & mask);
  
  assert(pos < EXTENT_SEARCH_MATRIX_SIZE);
  return pos;
}

void
Dbtup::update_extent_pos(EmulatedJamBuffer* jamBuf,
                         Disk_alloc_info& alloc, 
                         Ptr<Extent_info> extentPtr,
                         Int32 delta)
{
  if (delta < 0)
  {
    thrjam(jamBuf);
    Uint32 sub = Uint32(- delta);
    ddassert(extentPtr.p->m_free_space >= sub);
    extentPtr.p->m_free_space -= sub;
  }
  else
  {
    thrjam(jamBuf);
    extentPtr.p->m_free_space += delta;
    ndbassert(Uint32(delta) <= alloc.calc_page_free_space(0));
  }

#ifdef VM_TRACE
  Uint32 cnt = 0;
  Uint32 sum = 0;
  for(Uint32 i = 0; i<MAX_FREE_LIST; i++)
  {
    cnt += extentPtr.p->m_free_page_count[i];
    sum += extentPtr.p->m_free_page_count[i] * alloc.calc_page_free_space(i);
  }
  if (extentPtr.p->m_free_page_count[0] == cnt)
  {
    ddassert(extentPtr.p->m_free_space == cnt*alloc.m_page_free_bits_map[0]);
  }
  else
  {
    ddassert(extentPtr.p->m_free_space < cnt*alloc.m_page_free_bits_map[0]);
  }
  ddassert(extentPtr.p->m_free_space >= sum);
  ddassert(extentPtr.p->m_free_space <= cnt*alloc.m_page_free_bits_map[0]);
#endif
  
  Uint32 old = extentPtr.p->m_free_matrix_pos;
  if (old != RNIL)
  {
    Uint32 pos = alloc.calc_extent_pos(extentPtr.p);
    if (old != pos)
    {
      thrjam(jamBuf);
      Local_extent_info_list old_list(c_extent_pool, alloc.m_free_extents[old]);
      Local_extent_info_list new_list(c_extent_pool, alloc.m_free_extents[pos]);
      old_list.remove(extentPtr);
      new_list.addFirst(extentPtr);
      extentPtr.p->m_free_matrix_pos= pos;
    }
  }
  else
  {
    ddassert(alloc.m_curr_extent_info_ptr_i == extentPtr.i);
  }
}

void
Dbtup::restart_setup_page(Ptr<Fragrecord> fragPtr,
                          Disk_alloc_info& alloc,
                          PagePtr pagePtr,
                          Int32 estimate)
{
  jam();
  /**
   * Link to extent, clear uncommitted_used_space
   */
  pagePtr.p->uncommitted_used_space = 0;
  
  Extent_info key;
  key.m_key.m_file_no = pagePtr.p->m_file_no;
  key.m_key.m_page_idx = pagePtr.p->m_extent_no;
  Ptr<Extent_info> extentPtr;
  if (!c_extent_hash.find(extentPtr, key))
  {
    DEB_EXTENT_BITS(("(%u)Crash on page(%u,%u) in tab(%u,%u), extent page: %u"
                     " restart_seq(%u,%u)",
                     instance(),
                     pagePtr.p->m_file_no,
                     pagePtr.p->m_page_no,
                     fragPtr.p->fragTableId,
                     fragPtr.p->fragmentId,
                     pagePtr.p->m_extent_no,
                     pagePtr.p->m_restart_seq,
                     globalData.m_restart_seq));
    ndbrequire(false);
  }
  pagePtr.p->m_restart_seq = globalData.m_restart_seq;
  pagePtr.p->m_extent_info_ptr = extentPtr.i;

  Uint32 real_free = pagePtr.p->free_space;
  const bool prealloc = estimate >= 0;
  Uint32 estimated;
  if (prealloc)
  {
    jam();
    /**
     * If this is during prealloc, use estimate from there
     */
    estimated = (Uint32)estimate;
    Uint32 page_estimated =
      alloc.calc_page_free_space(alloc.calc_page_free_bits(real_free));
    if (page_estimated != estimated && real_free == 0)
    {
      jam();
      /**
       * The page claims it is full, but the extent bits says that it isn't
       * full, this can occur if the tablespace is using the v1 page format.
       * It must be an old dropped page and thus we can safely overwrite it.
       */
      g_eventLogger->info("(%u)tab(%u,%u), page(%u,%u):%u"
                          ", inconsistency between extent and page, most"
                          " likely due to using v1 pages, we assume page"
                          " comes from dropped table and is really empty",
                          instance(),
                          fragPtr.p->fragTableId,
                          fragPtr.p->fragmentId,
                          pagePtr.p->m_file_no,
                          pagePtr.p->m_page_no,
                          pagePtr.i);
      ndbassert(false); //Crash in debug for analysis
      Ptr<Tablerec> tabPtr;
      tabPtr.i= fragPtr.p->fragTableId;
      ptrCheckGuard(tabPtr, cnoOfTablerec, tablerec);
      convertThPage((Fix_page*)pagePtr.p, tabPtr.p, DD);
      estimated = alloc.calc_page_free_space(
        alloc.calc_page_free_bits(real_free));
    }
  }
  else
  {
    jam();
    /**
     * else use the estimate based on the actual free space
     */
    estimated = alloc.calc_page_free_space(alloc.calc_page_free_bits(real_free));
  }

#ifdef VM_TRACE
  {
    Local_key page;
    page.m_file_no = pagePtr.p->m_file_no;
    page.m_page_no = pagePtr.p->m_page_no;

    D("Tablespace_client - restart_setup_page");
    Tablespace_client tsman(0, this, c_tsman,
			    0, 0, 0, 0);
    unsigned uncommitted, committed;
    uncommitted = committed = ~(unsigned)0;
    (void) tsman.get_page_free_bits(&page, &uncommitted, &committed);
    jamEntry();

#ifdef VM_TRACE
    if (alloc.calc_page_free_bits(real_free) != committed)
    {
      g_eventLogger->info("(%u)page(%u,%u):%u, calc_free_bits: %u,"
                          " committed: %u, uncommitted: %u, free_space: %u",
                          instance(),
                          page.m_file_no,
                          page.m_page_no,
                          pagePtr.i,
                          alloc.calc_page_free_bits(real_free),
                          committed,
                          uncommitted,
                          real_free);
    }
    ddassert(alloc.calc_page_free_bits(real_free) == committed);
#endif
    if (prealloc)
    {
      /**
       * tsman.alloc_page sets the uncommitted-bits to MAX_FREE_LIST -1
       *   to avoid page being preallocated several times
       */
      ddassert(uncommitted == MAX_FREE_LIST - 1);
    }
    else
    {
      ddassert(committed == uncommitted);
    }
  }
#endif

  ddassert(real_free >= estimated);

  if (real_free != estimated)
  {
    jam();
    Uint32 delta = (real_free-estimated);
    update_extent_pos(jamBuffer(), alloc, extentPtr, delta);
  }
}

/**
 * - Page free bits -
 * 0 = 00 - free - 100% free
 * 1 = 01 - atleast one row free
 * 2 = 10 - full
 * 3 = 11 - full
 *
 * sz is always 1 when coming here, so calc_page_free_bits will
 * will always return 1 here. This will change with implementation
 * var-sized disk attributes.
 */

#define DBG_DISK 0

int
Dbtup::disk_page_prealloc(Signal* signal, 
			  Ptr<Fragrecord> fragPtr,
			  Local_key* key, Uint32 sz)
{
  int err;
  Uint32 i, ptrI;
  Ptr<Page_request> req;
  Fragrecord* fragPtrP = fragPtr.p; 
  Disk_alloc_info& alloc= fragPtrP->m_disk_alloc_info;
  Uint32 idx= alloc.calc_page_free_bits(sz);
  D("Tablespace_client - disk_page_prealloc");
  Tablespace_client tsman(signal, this, c_tsman,
                fragPtrP->fragTableId,
                fragPtrP->fragmentId,
                c_lqh->getCreateSchemaVersion(fragPtrP->fragTableId),
                fragPtrP->m_tablespace_id);
  
  if (DBG_DISK)
    ndbout << "disk_page_prealloc";

  /**
   * 1) search current dirty pages
   * First check for empty pages and then search for non-full pages.
   */
  for(i= 0; i <= idx; i++)
  {
    if (!alloc.m_dirty_pages[i].isEmpty())
    {
      ptrI= alloc.m_dirty_pages[i].getFirst();
      Ptr<GlobalPage> gpage;
      m_global_page_pool.getPtr(gpage, ptrI);
      
      PagePtr tmp;
      tmp.i = gpage.i;
      tmp.p = reinterpret_cast<Page*>(gpage.p);
      disk_page_prealloc_dirty_page(alloc, tmp, i, sz, fragPtrP);
      key->m_page_no= tmp.p->m_page_no;
      key->m_file_no= tmp.p->m_file_no;
      if (DBG_DISK)
	ndbout << " found dirty page " << *key << endl;
      jam();
      return 0; // Page in memory
    }
  }
  
  /**
   * Search outanding page requests
   *   callback does not need to access page request again
   *   as it's not the first request to this page
   */
  for(i= 0; i <= idx; i++)
  {
    if (!alloc.m_page_requests[i].isEmpty())
    {
      ptrI= alloc.m_page_requests[i].getFirst();
      Ptr<Page_request> req;
      c_page_request_pool.getPtr(req, ptrI);

      disk_page_prealloc_transit_page(alloc, req, i, sz);
      * key = req.p->m_key;
      if (DBG_DISK)
	ndbout << " found transit page " << *key << endl;
      jam();
      return 0;
    }
  }
  
  /**
   * We need to request a page...
   */
  if (!c_page_request_pool.seize(req))
  {
    jam();
    err= 1605;
    return -err;
  }

  req.p->m_ref_count= 1;
  req.p->m_frag_ptr_i= fragPtr.i;
  req.p->m_uncommitted_used_space= sz;
  
  int pageBits; // received
  Ptr<Extent_info> ext;
  const Uint32 bits = alloc.calc_page_free_bits(sz); // required
  bool found= false;

  /**
   * Do we have a current extent
   */
  if ((ext.i = alloc.m_curr_extent_info_ptr_i) != RNIL)
  {
    jam();
    c_extent_pool.getPtr(ext);
    if ((pageBits= tsman.alloc_page_from_extent(&ext.p->m_key, bits)) >= 0) 
    {
      jamEntry();
      jamLine(pageBits);
      found= true;
    }
    else
    {
      jamEntry();
      /**
       * The current extent is not in a free list
       *   and since it couldn't accomodate the request
       *   we put it on the free list per state (so also
       *   a full page is in one of the m_free_extents
       *   lists).
       */
      alloc.m_curr_extent_info_ptr_i = RNIL;
      Uint32 pos= alloc.calc_extent_pos(ext.p);
      ext.p->m_free_matrix_pos = pos;
      Local_extent_info_list list(c_extent_pool, alloc.m_free_extents[pos]);
      list.addFirst(ext);
    }
  }
  
  if (!found)
  {
    Uint32 pos;
    if ((pos= alloc.find_extent(sz)) != RNIL)
    {
      jam();
      Local_extent_info_list list(c_extent_pool, alloc.m_free_extents[pos]);
      list.first(ext);
      list.remove(ext);
    }
    else 
    {
      jam();
      /**
       * We need to alloc an extent
       */
      if (!c_extent_pool.seize(ext))
      {
	jam();
	err= 1606;
	c_page_request_pool.release(req);
	return -err;
      }
      
      if ((err= tsman.alloc_extent(&ext.p->m_key)) < 0)
      {
	jamEntry();
	c_extent_pool.release(ext);
	c_page_request_pool.release(req);
	return err;
      }

      int pages= err;
      
#ifdef VM_TRACE
      ndbout << "allocated " << pages << " pages: " << ext.p->m_key 
	     << " table: " << fragPtr.p->fragTableId 
	     << " fragment: " << fragPtr.p->fragmentId << endl;
#endif
      ext.p->m_first_page_no = ext.p->m_key.m_page_no;
      memset(ext.p->m_free_page_count, 0, sizeof(ext.p->m_free_page_count));
      ext.p->m_free_space= alloc.m_page_free_bits_map[0] * pages; 
      ext.p->m_free_page_count[0]= pages; // All pages are "free"-est
      ext.p->m_empty_page_no = 0;

      DEB_EXTENT_BITS(("new:extent(%u,%u) in tab(%u,%u),"
                       " page(%u,%u)->%u,"
                 " empty_page: %u",
                instance(),
                ext.i,
                fragPtr.p->fragTableId,
                fragPtr.p->fragmentId,
                ext.p->m_key.m_file_no,
                ext.p->m_first_page_no,
                ext.p->m_first_page_no + (pages - 1),
                ext.p->m_empty_page_no));

      c_extent_hash.add(ext);

      Local_fragment_extent_list list1(c_extent_pool, alloc.m_extent_list);
      list1.addFirst(ext);
    }
    jam(); 
    alloc.m_curr_extent_info_ptr_i= ext.i;
    ext.p->m_free_matrix_pos= RNIL;
    pageBits= tsman.alloc_page_from_extent(&ext.p->m_key, bits);
    jamEntry();
    ddassert(pageBits >= 0);
  }
  
  /**
   * We have a page from an extent
   */
  *key= req.p->m_key= ext.p->m_key;

  if (DBG_DISK)
    ndbout << " allocated page " << *key << endl;
  
  /**
   * We don't know exact free space of page
   *   but we know what page free bits it has.
   *   compute free space based on them
   */
  Uint32 size= alloc.calc_page_free_space((Uint32)pageBits);
  
  ddassert(size >= sz);
  req.p->m_original_estimated_free_space = size;

  Uint32 new_size = size - sz;   // Subtract alloc rec
  Uint32 newPageBits= alloc.calc_page_free_bits(new_size);
  ndbrequire(newPageBits != (Uint32)pageBits)
  {
    jam();
    /**
     * We should always enter this path. When the new page was empty
     * before coming here, then it will go from empty state to either
     * non-full or to the full state. If we come here with a page which
     * non-full before, then we will enter the full state. We will
     * possibly return it to the non-full list when the real page have
     * been read and we know the exact fullness level.
     */
    ddassert(ext.p->m_free_page_count[pageBits] > 0);
    ext.p->m_free_page_count[pageBits]--;
    ext.p->m_free_page_count[newPageBits]++;
  }
  update_extent_pos(jamBuffer(), alloc, ext, -Int32(sz));

  // And put page request in correct free list
  idx= alloc.calc_page_free_bits(new_size);
  {
    Local_page_request_list list(c_page_request_pool, 
				 alloc.m_page_requests[idx]);
    
    list.addLast(req);
  }
  req.p->m_list_index= idx;
  req.p->m_extent_info_ptr= ext.i;

  Page_cache_client::Request preq;
  preq.m_page = *key;
  preq.m_table_id = fragPtr.p->fragTableId;
  preq.m_fragment_id = fragPtr.p->fragmentId;
  preq.m_callback.m_callbackData= req.i;
  preq.m_callback.m_callbackFunction = 
    safe_cast(&Dbtup::disk_page_prealloc_callback);
  
  int flags= Page_cache_client::ALLOC_REQ;
  if (pageBits == 0)
  {
    jam();
    flags |= Page_cache_client::EMPTY_PAGE;
    if (ext.p->m_first_page_no + ext.p->m_empty_page_no == key->m_page_no)
    {
      jam();
      ext.p->m_empty_page_no++;
      DEB_EXTENT_BITS(("extent(%u,%u) new page in tab(%u,%u), first_page(%u,%u)"
                       " empty_page: %u",
                instance(),
                ext.p->m_key.m_page_idx,
                fragPtr.p->fragTableId,
                fragPtr.p->fragmentId,
                key->m_file_no,
                key->m_page_no,
                ext.p->m_empty_page_no));
    }
    else
    {
      DEB_EXTENT_BITS(("extent(%u,%u) new page in tab(%u,%u), page(%u,%u)",
                instance(),
                ext.p->m_key.m_page_idx,
                fragPtr.p->fragTableId,
                fragPtr.p->fragmentId,
                key->m_file_no,
                key->m_page_no));
    }
    preq.m_callback.m_callbackFunction = 
      safe_cast(&Dbtup::disk_page_prealloc_initial_callback);
  }
  
  Page_cache_client pgman(this, c_pgman);
  int res= pgman.get_page(signal, preq, flags);
  jamEntry();
  switch(res)
  {
  case 0:
    jam();
    break;
  case -1:
    ndbassert(false);
    return -1604;
  default:
    jam();
    execute(signal, preq.m_callback, res); // run callback
  }
  
  return res;
}

void
Dbtup::disk_page_prealloc_dirty_page(Disk_alloc_info & alloc,
				     PagePtr pagePtr, 
				     Uint32 old_idx,
                                     Uint32 sz,
                                     Fragrecord *fragPtrP)
{
  jam();
  ddassert(pagePtr.p->list_index == old_idx);

  Uint32 free= pagePtr.p->free_space;
  Uint32 used= pagePtr.p->uncommitted_used_space + sz;
  Uint32 ext= pagePtr.p->m_extent_info_ptr;
  
  ddassert(free >= used);
  Ptr<Extent_info> extentPtr;
  c_extent_pool.getPtr(extentPtr, ext);

  Uint32 new_idx= alloc.calc_page_free_bits(free - used);

  if (old_idx != new_idx)
  {
    jam();
    disk_page_move_dirty_page(alloc,
                              extentPtr,
                              pagePtr,
                              old_idx,
                              new_idx,
                              fragPtrP);
  }

  pagePtr.p->uncommitted_used_space = used;
  update_extent_pos(jamBuffer(), alloc, extentPtr, -Int32(sz));
}


void
Dbtup::disk_page_prealloc_transit_page(Disk_alloc_info& alloc,
				       Ptr<Page_request> req, 
				       Uint32 old_idx, Uint32 sz)
{
  jam();
  ddassert(req.p->m_list_index == old_idx);

  Uint32 free= req.p->m_original_estimated_free_space;
  Uint32 used= req.p->m_uncommitted_used_space + sz;
  Uint32 ext= req.p->m_extent_info_ptr;
  
  Ptr<Extent_info> extentPtr;
  c_extent_pool.getPtr(extentPtr, ext);

  ddassert(free >= used);
  Uint32 new_idx= alloc.calc_page_free_bits(free - used);
  
  if (old_idx != new_idx)
  {
    jam();
    disk_page_move_page_request(alloc, extentPtr, req, old_idx, new_idx);
  }

  req.p->m_uncommitted_used_space = used;
  update_extent_pos(jamBuffer(), alloc, extentPtr, -Int32(sz));
}

void
Dbtup::disk_page_prealloc_callback(Signal* signal, 
				   Uint32 page_request, Uint32 page_id)
{
  jamEntry();

  Ptr<Page_request> req;
  c_page_request_pool.getPtr(req, page_request);

  Ptr<GlobalPage> gpage;
  m_global_page_pool.getPtr(gpage, page_id);

  Ptr<Fragrecord> fragPtr;
  fragPtr.i= req.p->m_frag_ptr_i;
  ptrCheckGuard(fragPtr, cnoOfFragrec, fragrecord);

  PagePtr pagePtr;
  pagePtr.i = gpage.i;
  pagePtr.p = reinterpret_cast<Page*>(gpage.p);

  Disk_alloc_info& alloc= fragPtr.p->m_disk_alloc_info;

  Local_key key = req.p->m_key;
  if (key.m_file_no != pagePtr.p->m_file_no ||
      key.m_page_no != pagePtr.p->m_page_no ||
      fragPtr.p->fragTableId != pagePtr.p->m_table_id ||
      fragPtr.p->fragmentId != pagePtr.p->m_fragment_id)
  {
    jam();
    /**
     * At this point we are reading what should be an initialised page
     * and thus file_no, page_no, table and fragment id should be correct.
     * If not crash and provide details.
     */
    g_eventLogger->info("(%u)key(%u,%u), page(%u,%u), restart_seq(%u,%u)"
                        "key_tab(%u,%u), page_tab(%u,%u)",
                        instance(),
                        key.m_file_no,
                        key.m_page_no,
                        pagePtr.p->m_file_no,
                        pagePtr.p->m_page_no,
                        globalData.m_restart_seq,
                        pagePtr.p->m_restart_seq,
                        fragPtr.p->fragTableId,
                        fragPtr.p->fragmentId,
                        pagePtr.p->m_table_id,
                        pagePtr.p->m_fragment_id);
    ndbrequire(false);
  }
  if (unlikely(pagePtr.p->m_restart_seq != globalData.m_restart_seq))
  {
    jam();
    D(V(pagePtr.p->m_restart_seq) << V(globalData.m_restart_seq));
    restart_setup_page(fragPtr, alloc, pagePtr, req.p->m_original_estimated_free_space);
  }

  Ptr<Extent_info> extentPtr;
  c_extent_pool.getPtr(extentPtr, req.p->m_extent_info_ptr);

  pagePtr.p->uncommitted_used_space += req.p->m_uncommitted_used_space;
  ddassert(pagePtr.p->free_space >= pagePtr.p->uncommitted_used_space);

  Uint32 free = pagePtr.p->free_space - pagePtr.p->uncommitted_used_space;
  Uint32 idx = req.p->m_list_index;
  Uint32 real_idx = alloc.calc_page_free_bits(free);

  if (idx != real_idx)
  {
    jam();
    ddassert(extentPtr.p->m_free_page_count[idx]);
    extentPtr.p->m_free_page_count[idx]--;
    extentPtr.p->m_free_page_count[real_idx]++;
    update_extent_pos(jamBuffer(), alloc, extentPtr, 0);
  }
  /*
  DEB_LCP((
    "(%u)tab(%u,%u) page(%u,%u):%u u_u_s: %u, free:%u idx:%u, new_idx:%u",
          instance(),
          fragPtr.p->fragTableId,
          fragPtr.p->fragmentId,
          pagePtr.p->m_file_no,
          pagePtr.p->m_page_no,
          pagePtr.i,
          pagePtr.p->uncommitted_used_space,
          free,
          idx,
          real_idx));
  */
  {
    /**
     * add to dirty list
     */
    pagePtr.p->list_index = real_idx;
    Page_pool *cheat_pool= (Page_pool*)&m_global_page_pool;
    Local_Page_list list(* cheat_pool, alloc.m_dirty_pages[real_idx]);
    list.addFirst(pagePtr);
  }

  {
    /**
     * release page request
     */
    Local_page_request_list list(c_page_request_pool,
				 alloc.m_page_requests[idx]);
    list.release(req);
  }
}

void
Dbtup::disk_page_move_dirty_page(Disk_alloc_info& alloc,
                                 Ptr<Extent_info> extentPtr,
                                 Ptr<Page> pagePtr,
                                 Uint32 old_idx,
                                 Uint32 new_idx,
                                 Fragrecord *fragPtrP)
{
#ifdef VM_TRACE
  if (extentPtr.p->m_free_page_count[old_idx] == 0)
  {
    // Additional printouts when following ddassert fails.
    // The ddassert prints the alloc argument.
    ndbout << "Dbtup::disk_page_move_dirty_page" << endl;
    ndbout << "  extentPtr: " << extentPtr << endl;
    ndbout << "  pagePtr: " << pagePtr << endl;
    ndbout << "  old_idx: " << old_idx << endl;
    ndbout << "  new_idx: " << new_idx << endl;
  }
#endif
  ddassert(extentPtr.p->m_free_page_count[old_idx]);
  extentPtr.p->m_free_page_count[old_idx]--;
  extentPtr.p->m_free_page_count[new_idx]++;

  Page_pool *pool= (Page_pool*)&m_global_page_pool;
  Local_Page_list new_list(*pool, alloc.m_dirty_pages[new_idx]);
  Local_Page_list old_list(*pool, alloc.m_dirty_pages[old_idx]);
  old_list.remove(pagePtr);
  new_list.addFirst(pagePtr);

  /*
  DEB_LCP(("(%u)tab(%u,%u) page(%u,%u):%u new_idx: %u",
          instance(),
          fragPtrP->fragTableId,
          fragPtrP->fragmentId,
          pagePtr.p->m_file_no,
          pagePtr.p->m_page_no,
          pagePtr.i,
          new_idx));
  */
  pagePtr.p->list_index = new_idx;
}

void
Dbtup::disk_page_move_page_request(Disk_alloc_info& alloc,
                                   Ptr<Extent_info> extentPtr,
                                   Ptr<Page_request> req,
                                   Uint32 old_idx, Uint32 new_idx)
{
  Page_request_list::Head *lists = alloc.m_page_requests;
  Local_page_request_list old_list(c_page_request_pool, lists[old_idx]);
  Local_page_request_list new_list(c_page_request_pool, lists[new_idx]);
  old_list.remove(req);
  new_list.addLast(req);

  ddassert(extentPtr.p->m_free_page_count[old_idx]);
  extentPtr.p->m_free_page_count[old_idx]--;
  extentPtr.p->m_free_page_count[new_idx]++;
  req.p->m_list_index= new_idx;
}

/**
 * We have read in a page which is at the moment empty. It is possible that
 * the information on this page is garbage since this could be our first
 * access to this page. It could even have belonged to another table that
 * was deleted before getting here. So we need to initialise the page header
 * at this point in time.
 */
void
Dbtup::disk_page_prealloc_initial_callback(Signal*signal, 
					   Uint32 page_request, 
					   Uint32 page_id)
{
  jamEntry();
  /**
   * 1) lookup page request
   * 2) lookup page
   * 3) lookup table
   * 4) init page (according to page type)
   * 5) call ordinary callback
   */
  Ptr<Page_request> req;
  c_page_request_pool.getPtr(req, page_request);

  Ptr<GlobalPage> gpage;
  m_global_page_pool.getPtr(gpage, page_id);
  PagePtr pagePtr;
  pagePtr.i = gpage.i;
  pagePtr.p = reinterpret_cast<Page*>(gpage.p);

  Ptr<Fragrecord> fragPtr;
  fragPtr.i= req.p->m_frag_ptr_i;
  ptrCheckGuard(fragPtr, cnoOfFragrec, fragrecord);

  Ptr<Tablerec> tabPtr;
  tabPtr.i = fragPtr.p->fragTableId;
  ptrCheckGuard(tabPtr, cnoOfTablerec, tablerec);

  Ptr<Extent_info> extentPtr;
  c_extent_pool.getPtr(extentPtr, req.p->m_extent_info_ptr);

  ndbrequire(tabPtr.p->m_attributes[DD].m_no_of_varsize == 0);

  /**
   * We can come here even when the page have been already initialised.
   *
   * Unfortunately there is no sure way of discovering if we are reusing
   * an already used disk page. The extent information isn't synchronised
   * together with the disk page itself. So it is perfectly possible to
   * allocate an extent and write a page in it and then restart and as
   * part of recovery processing the extent isn't any more a part of this
   * fragment. A new extent can be used and this can be any extent. So this
   * means that we can even allocate the same extent once more by the same
   * fragment after the restart.
   *
   * So we simply go ahead and write this new page as an initial page.
   * There are plenty of other safeguards against wrong use of disk
   * pages and checkpointing algorithms.
   */

  /**
   * Ensure that all unset header variables are set to 0.
   */
  memset((char*)pagePtr.p, 0, Page::HEADER_WORDS * 4);

  convertThPage((Fix_page*)pagePtr.p, tabPtr.p, DD);

  pagePtr.p->m_page_no= req.p->m_key.m_page_no;
  pagePtr.p->m_file_no= req.p->m_key.m_file_no;
  pagePtr.p->m_table_id= fragPtr.p->fragTableId;
  pagePtr.p->m_ndb_version = htonl(NDB_DISK_V2);
  pagePtr.p->m_create_table_version =
    c_lqh->getCreateSchemaVersion(fragPtr.p->fragTableId);
  pagePtr.p->m_fragment_id = fragPtr.p->fragmentId;
  pagePtr.p->m_extent_no = extentPtr.p->m_key.m_page_idx; // logical extent no
  pagePtr.p->m_extent_info_ptr= req.p->m_extent_info_ptr;
  pagePtr.p->m_restart_seq = globalData.m_restart_seq;
  pagePtr.p->nextList = pagePtr.p->prevList = RNIL;
  pagePtr.p->list_index = req.p->m_list_index;
  pagePtr.p->uncommitted_used_space = req.p->m_uncommitted_used_space;

  Disk_alloc_info& alloc= fragPtr.p->m_disk_alloc_info;
  Uint32 idx = req.p->m_list_index;

  {
    Uint32 free = pagePtr.p->free_space - pagePtr.p->uncommitted_used_space;
    ddassert(idx == alloc.calc_page_free_bits(free));
    ddassert(pagePtr.p->free_space == req.p->m_original_estimated_free_space);
  }

  {
    /**
     * add to dirty list
     */
    Page_pool *cheat_pool= (Page_pool*)&m_global_page_pool;
    Local_Page_list list(* cheat_pool, alloc.m_dirty_pages[idx]);
    list.addFirst(pagePtr);
  }

  {
    /**
     * release page request
     */
    Local_page_request_list list(c_page_request_pool, 
				 alloc.m_page_requests[idx]);
    list.release(req);
  }
}

void
Dbtup::disk_page_set_dirty(PagePtr pagePtr)
{
  jam();
  Uint32 idx = pagePtr.p->list_index;
  if ((pagePtr.p->m_restart_seq == globalData.m_restart_seq) &&
      ((idx & 0x8000) == 0))
  {
    jam();
    /**
     * Already in dirty list
     */
    return ;
  }
  
  Local_key key;
  key.m_page_no = pagePtr.p->m_page_no;
  key.m_file_no = pagePtr.p->m_file_no;

  pagePtr.p->nextList = pagePtr.p->prevList = RNIL;

  if (DBG_DISK)
    ndbout << " disk_page_set_dirty " << key << endl;
  
  Ptr<Tablerec> tabPtr;
  tabPtr.i= pagePtr.p->m_table_id;
  ptrCheckGuard(tabPtr, cnoOfTablerec, tablerec);
  
  Ptr<Fragrecord> fragPtr;
  getFragmentrec(fragPtr, pagePtr.p->m_fragment_id, tabPtr.p);
  
  Disk_alloc_info& alloc= fragPtr.p->m_disk_alloc_info;

  Uint32 free = pagePtr.p->free_space;
  Uint32 used = pagePtr.p->uncommitted_used_space;
  if (unlikely(pagePtr.p->m_restart_seq != globalData.m_restart_seq))
  {
    jam();
    D(V(pagePtr.p->m_restart_seq) << V(globalData.m_restart_seq));
    restart_setup_page(fragPtr, alloc, pagePtr, -1);
    ndbassert(free == pagePtr.p->free_space);
    free = pagePtr.p->free_space;
    idx = alloc.calc_page_free_bits(free);
    used = 0;
  }
  else
  {
    jam();
    idx &= ~0x8000;
    DEB_EXTENT_BITS(("((%u)Reset list_index bit 0x8000 on page(%u,%u)",
                     instance(),
                     pagePtr.p->m_file_no,
                     pagePtr.p->m_page_no));
    ddassert(idx == alloc.calc_page_free_bits(free - used));
  }
  
  ddassert(free >= used);
  
  D("Tablespace_client - disk_page_set_dirty");
  Tablespace_client tsman(0, this, c_tsman,
                        fragPtr.p->fragTableId,
                        fragPtr.p->fragmentId,
                        c_lqh->getCreateSchemaVersion(fragPtr.p->fragTableId),
                        fragPtr.p->m_tablespace_id);
  
  pagePtr.p->list_index = idx;
  Page_pool *pool= (Page_pool*)&m_global_page_pool;
  Local_Page_list list(*pool, alloc.m_dirty_pages[idx]);
  list.addFirst(pagePtr);
  
  // Make sure no one will allocate it...
  tsman.unmap_page(&key, MAX_FREE_LIST - 1);
  jamEntry();
}

void
Dbtup::disk_page_unmap_callback(Uint32 when,
				Uint32 page_id, Uint32 dirty_count)
{
  jamEntry();
  Ptr<GlobalPage> gpage;
  m_global_page_pool.getPtr(gpage, page_id);
  PagePtr pagePtr;
  pagePtr.i = gpage.i;
  pagePtr.p = reinterpret_cast<Page*>(gpage.p);
  
  Uint32 type = pagePtr.p->m_page_header.m_page_type;
  if (unlikely((type != File_formats::PT_Tup_fixsize_page &&
		type != File_formats::PT_Tup_varsize_page) ||
	       f_undo_done == false))
  {
    jam();
    D("disk_page_unmap_callback" << V(type) << V(f_undo_done));
    return ;
  }

  Uint32 idx = pagePtr.p->list_index;

  Ptr<Tablerec> tabPtr;
  tabPtr.i= pagePtr.p->m_table_id;
  ptrCheckGuard(tabPtr, cnoOfTablerec, tablerec);
  
  Ptr<Fragrecord> fragPtr;
  getFragmentrec(fragPtr, pagePtr.p->m_fragment_id, tabPtr.p);

  DEB_LCP(("(%u)unmap page: tab(%u,%u), page(%u,%u):%u",
           instance(),
           pagePtr.p->m_table_id,
           pagePtr.p->m_fragment_id,
           pagePtr.p->m_file_no,
           pagePtr.p->m_page_no,
           pagePtr.i));

  Disk_alloc_info& alloc= fragPtr.p->m_disk_alloc_info;
  
  if (when == 0)
  {
    /**
     * Before pageout
     */
    jam();

    if (DBG_DISK)
    {
      Local_key key;
      key.m_page_no = pagePtr.p->m_page_no;
      key.m_file_no = pagePtr.p->m_file_no;
      ndbout << "disk_page_unmap_callback(before) " << key 
	     << " cnt: " << dirty_count << " " << (idx & ~0x8000) << endl;
    }

    ndbassert((idx & 0x8000) == 0);

    Page_pool *pool= (Page_pool*)&m_global_page_pool;
    Local_Page_list list(*pool, alloc.m_dirty_pages[idx]);
    Local_Page_list list2(*pool, alloc.m_unmap_pages);
    list.remove(pagePtr);
    list2.addFirst(pagePtr);

    if (dirty_count == 0)
    {
      jam();
      pagePtr.p->list_index = idx | 0x8000;      
      DEB_EXTENT_BITS(("(%u)Set list_index bit 0x8000 on page(%u,%u)"
                       " when unmap",
                       instance(),
                       pagePtr.p->m_file_no,
                       pagePtr.p->m_page_no));
      
      Local_key key;
      key.m_page_no = pagePtr.p->m_page_no;
      key.m_file_no = pagePtr.p->m_file_no;
      
      Uint32 free = pagePtr.p->free_space;
      Uint32 used = pagePtr.p->uncommitted_used_space;
      ddassert(free >= used);
      ddassert(alloc.calc_page_free_bits(free - used) == idx);
      
      D("Tablespace_client - disk_page_unmap_callback");
      Tablespace_client tsman(0, this, c_tsman,
                    fragPtr.p->fragTableId,
                    fragPtr.p->fragmentId,
                    c_lqh->getCreateSchemaVersion(fragPtr.p->fragTableId),
                    fragPtr.p->m_tablespace_id);
      
      tsman.unmap_page(&key, idx);
      jamEntry();
    }
  }
  else if (when == 1)
  {
    /**
     * After page out
     */
    jam();

    Local_key key;
    key.m_page_no = pagePtr.p->m_page_no;
    key.m_file_no = pagePtr.p->m_file_no;
    Uint32 real_free = pagePtr.p->free_space;
    
    if (DBG_DISK)
    {
      ndbout << "disk_page_unmap_callback(after) " << key 
	     << " cnt: " << dirty_count << " " << (idx & ~0x8000) << endl;
    }

    Page_pool *pool= (Page_pool*)&m_global_page_pool;
    Local_Page_list list(*pool, alloc.m_unmap_pages);
    list.remove(pagePtr);

    D("Tablespace_client - disk_page_unmap_callback");
    Tablespace_client tsman(0, this, c_tsman,
                   fragPtr.p->fragTableId,
                   fragPtr.p->fragmentId,
                   c_lqh->getCreateSchemaVersion(fragPtr.p->fragTableId),
                   fragPtr.p->m_tablespace_id);
    
    if (DBG_DISK && alloc.calc_page_free_bits(real_free) != (idx & ~0x8000))
    {
      ndbout << key 
	     << " calc: " << alloc.calc_page_free_bits(real_free)
	     << " idx: " << (idx & ~0x8000)
	     << endl;
    }
    DEB_EXTENT_BITS(("(%u)tab(%u,%u), page(%u,%u):%u real_free: %u, new_bits: %u",
                    instance(),
                    fragPtr.p->fragTableId,
                    fragPtr.p->fragmentId,
                    pagePtr.p->m_file_no,
                    pagePtr.p->m_page_no,
                    pagePtr.i,
                    real_free,
                    alloc.calc_page_free_bits(real_free)));

    tsman.update_page_free_bits(&key, alloc.calc_page_free_bits(real_free));
    jamEntry();
  }
}

void
Dbtup::disk_page_alloc(Signal* signal, 
		       Tablerec* tabPtrP, Fragrecord* fragPtrP, 
		       Local_key* key, PagePtr pagePtr, Uint32 gci)
{
  jam();
  Uint32 logfile_group_id= fragPtrP->m_logfile_group_id;
  Disk_alloc_info& alloc= fragPtrP->m_disk_alloc_info;

  Uint64 lsn;
  if (tabPtrP->m_attributes[DD].m_no_of_varsize == 0)
  {
    jam();
    ddassert(pagePtr.p->uncommitted_used_space > 0);
    pagePtr.p->uncommitted_used_space--;
    key->m_page_idx= ((Fix_page*)pagePtr.p)->alloc_record();
    lsn= disk_page_undo_alloc(signal,
                              pagePtr.p,
                              key,
                              1,
                              gci,
                              logfile_group_id);
     DEB_PGMAN((
       "(%u)disk_page_alloc: tab(%u,%u):%u,page(%u,%u).%u,gci: %u",
                instance(),
                pagePtr.p->m_table_id,
                pagePtr.p->m_fragment_id,
                pagePtr.p->m_create_table_version,
                key->m_file_no,
                key->m_page_no,
                key->m_page_idx,
                gci));
  }
  else
  {
    jam();
    Uint32 sz= key->m_page_idx;
    ddassert(pagePtr.p->uncommitted_used_space >= sz);
    pagePtr.p->uncommitted_used_space -= sz;
    key->m_page_idx= ((Var_page*)pagePtr.p)->
      alloc_record(sz, (Var_page*)ctemp_page, 0);
    
    lsn= disk_page_undo_alloc(signal,
                              pagePtr.p,
                              key,
                              sz,
                              gci,
                              logfile_group_id);
  }
}

void
Dbtup::disk_page_free(Signal *signal, 
		      Tablerec *tabPtrP, Fragrecord * fragPtrP,
		      Local_key* key, PagePtr pagePtr, Uint32 gci)
{
  jam();
  if (DBG_DISK)
    ndbout << " disk_page_free " << *key << endl;
  
  Uint32 page_idx= key->m_page_idx;
  Uint32 logfile_group_id= fragPtrP->m_logfile_group_id;
  Disk_alloc_info& alloc= fragPtrP->m_disk_alloc_info;
  Uint32 old_free= pagePtr.p->free_space;

  Uint32 sz;
  Uint64 lsn;
  if (tabPtrP->m_attributes[DD].m_no_of_varsize == 0)
  {
    sz = 1;
    const Uint32 *src= ((Fix_page*)pagePtr.p)->get_ptr(page_idx, 0);

#ifdef VM_TRACE
    if (((*(src + 1)) & Tup_fixsize_page::FREE_RECORD) ==
               Tup_fixsize_page::FREE_RECORD)
    {
      g_eventLogger->info(
        "(%u)disk_page_free crash:tab(%u,%u):%u,page(%u,%u).%u,gci:%u",
                 instance(),
                 fragPtrP->fragTableId,
                 fragPtrP->fragmentId,
                 pagePtr.p->m_create_table_version,
                 pagePtr.p->m_file_no,
                 pagePtr.p->m_page_no,
                 page_idx,
                 gci);
    }
#endif
    ndbassert(((*(src + 1)) & Tup_fixsize_page::FREE_RECORD) !=
               Tup_fixsize_page::FREE_RECORD);
    lsn= disk_page_undo_free(signal, pagePtr.p, key,
			     src, tabPtrP->m_offsets[DD].m_fix_header_size,
			     gci, logfile_group_id);
    
    ((Fix_page*)pagePtr.p)->free_record(page_idx);

    DEB_PGMAN((
      "(%u)disk_page_free:tab(%u,%u):%u,page(%u,%u).%u,gci:%u",
               instance(),
               fragPtrP->fragTableId,
               fragPtrP->fragmentId,
               pagePtr.p->m_create_table_version,
               pagePtr.p->m_file_no,
               pagePtr.p->m_page_no,
               page_idx,
               gci));
  }
  else
  {
    const Uint32 *src= ((Var_page*)pagePtr.p)->get_ptr(page_idx);
    sz= ((Var_page*)pagePtr.p)->get_entry_len(page_idx);
    lsn= disk_page_undo_free(signal, pagePtr.p, key,
			     src, sz,
			     gci, logfile_group_id);
    
    ((Var_page*)pagePtr.p)->free_record(page_idx, 0);
  }    
  
  Uint32 new_free = pagePtr.p->free_space;
  
  Uint32 ext = pagePtr.p->m_extent_info_ptr;
  Uint32 used = pagePtr.p->uncommitted_used_space;
  Uint32 old_idx = pagePtr.p->list_index;
  ddassert(old_free >= used);
  ddassert(new_free >= used);
  ddassert(new_free >= old_free);
  ddassert((old_idx & 0x8000) == 0);

  Uint32 new_idx = alloc.calc_page_free_bits(new_free - used);
  ddassert(alloc.calc_page_free_bits(old_free - used) == old_idx);
  
  Ptr<Extent_info> extentPtr;
  c_extent_pool.getPtr(extentPtr, ext);
  
  if (old_idx != new_idx)
  {
    jam();
    disk_page_move_dirty_page(alloc,
                              extentPtr,
                              pagePtr,
                              old_idx,
                              new_idx,
                              fragPtrP);
  }
  
  update_extent_pos(jamBuffer(), alloc, extentPtr, sz);
#ifdef NOT_YET_FREE_EXTENT
  if (check_free(extentPtr.p) == 0)
  {
    ndbout_c("free: extent is free");
  }
#endif
}

void
Dbtup::disk_page_abort_prealloc(Signal *signal, Fragrecord* fragPtrP, 
				Local_key* key, Uint32 sz)
{
  jam();

  Page_cache_client::Request req;
  req.m_callback.m_callbackData= sz;
  req.m_callback.m_callbackFunction = 
    safe_cast(&Dbtup::disk_page_abort_prealloc_callback);
  
  int flags= Page_cache_client::DIRTY_REQ;
  memcpy(&req.m_page, key, sizeof(Local_key));
  req.m_table_id = fragPtrP->fragTableId;
  req.m_fragment_id = fragPtrP->fragmentId;

  Page_cache_client pgman(this, c_pgman);
  int res= pgman.get_page(signal, req, flags);
  jamEntry();
  switch(res)
  {
  case 0:
    jam();
    break;
  case -1:
    ndbrequire(false);
    break;
  default:
    jam();
    Ptr<GlobalPage> gpage;
    m_global_page_pool.getPtr(gpage, (Uint32)res);
    PagePtr pagePtr;
    pagePtr.i = gpage.i;
    pagePtr.p = reinterpret_cast<Page*>(gpage.p);

    disk_page_abort_prealloc_callback_1(signal, fragPtrP, pagePtr, sz);
  }
}

void
Dbtup::disk_page_abort_prealloc_callback(Signal* signal, 
					 Uint32 sz, Uint32 page_id)
{
  jamEntry();  
  Ptr<GlobalPage> gpage;
  m_global_page_pool.getPtr(gpage, page_id);
  
  PagePtr pagePtr;
  pagePtr.i = gpage.i;
  pagePtr.p = reinterpret_cast<Page*>(gpage.p);

  Ptr<Tablerec> tabPtr;
  tabPtr.i= pagePtr.p->m_table_id;
  ptrCheckGuard(tabPtr, cnoOfTablerec, tablerec);
  
  Ptr<Fragrecord> fragPtr;
  getFragmentrec(fragPtr, pagePtr.p->m_fragment_id, tabPtr.p);

  disk_page_abort_prealloc_callback_1(signal, fragPtr.p, pagePtr, sz);
}

void
Dbtup::disk_page_abort_prealloc_callback_1(Signal* signal, 
					   Fragrecord* fragPtrP,
					   PagePtr pagePtr,
					   Uint32 sz)
{
  jam();
  disk_page_set_dirty(pagePtr);

  Disk_alloc_info& alloc= fragPtrP->m_disk_alloc_info;

  Ptr<Extent_info> extentPtr;
  c_extent_pool.getPtr(extentPtr, pagePtr.p->m_extent_info_ptr);

  Uint32 idx = pagePtr.p->list_index & 0x7FFF;
  Uint32 used = pagePtr.p->uncommitted_used_space;
  Uint32 free = pagePtr.p->free_space;

  ddassert(free >= used);
  ddassert(used >= sz);
  ddassert(alloc.calc_page_free_bits(free - used) == idx);

  pagePtr.p->uncommitted_used_space = used - sz;

  Uint32 new_idx = alloc.calc_page_free_bits(free - used + sz);

  if (idx != new_idx)
  {
    jam();
    disk_page_move_dirty_page(alloc,
                              extentPtr,
                              pagePtr,
                              idx,
                              new_idx,
                              fragPtrP);
  }
  
  update_extent_pos(jamBuffer(), alloc, extentPtr, sz);
#ifdef NOT_YET_FREE_EXTENT
  if (check_free(extentPtr.p) == 0)
  {
    ndbout_c("abort: extent is free");
  }
#endif
}

#ifdef NOT_YET_UNDO_ALLOC_EXTENT
void
Dbtup::disk_page_alloc_extent_log_buffer_callback(Signal* signal,
						  Uint32 extentPtrI,
						  Uint32 unused)
{
  Ptr<Extent_info> extentPtr;
  c_extent_pool.getPtr(extentPtr, extentPtrI);

  Local_key key = extentPtr.p->m_key;
  Tablespace_client2 tsman(signal, c_tsman, &key);

  Ptr<Tablerec> tabPtr;
  tabPtr.i= tsman.m_table_id;
  ptrCheckGuard(tabPtr, cnoOfTablerec, tablerec);
  
  Ptr<Fragrecord> fragPtr;
  getFragmentrec(fragPtr, tsman.m_fragment_id, tabPtr.p);

  Disk_undo::AllocExtent alloc;
  alloc.m_table = tabPtr.i;
  alloc.m_fragment = tsman.m_fragment_id;
  alloc.m_page_no = key.m_page_no;
  alloc.m_file_no = key.m_file_no;
  alloc.m_type_length = (Disk_undo::UNDO_ALLOC_EXTENT<<16) |
                        (sizeof(alloc)>> 2);

  Logfile_client::Change c[1] = {{ &alloc, sizeof(alloc) >> 2 } };
  Uint64 lsn;
  {
    Logfile_client lgman(this, c_lgman, fragPtr.p->m_logfile_group_id);
    lsn= lgman.add_entry(c, 1);
  }
  
  tsman.update_lsn(signal, &key, lsn);
  jamEntry();
}
#endif

Uint64
Dbtup::disk_page_undo_alloc(Signal *signal,
                            Page* page,
                            const Local_key* key,
			    Uint32 sz,
                            Uint32 gci,
                            Uint32 logfile_group_id)
{
  jam();
  Disk_undo::Alloc alloc;
  alloc.m_type_length= (Disk_undo::UNDO_ALLOC << 16) | (sizeof(alloc) >> 2);
  alloc.m_page_no = key->m_page_no;
  alloc.m_file_no_page_idx= key->m_file_no << 16 | key->m_page_idx;
  
  Logfile_client::Change c[1] = {{ &alloc, sizeof(alloc) >> 2 } };
  
  Uint64 lsn;
  {
    D("Logfile_client - disk_page_undo_alloc");
    Logfile_client lgman(this, c_lgman, logfile_group_id);
    lsn= lgman.add_entry(c, 1);
  }
  jamEntry();
  {
    Page_cache_client pgman(this, c_pgman);
    pgman.update_lsn(signal, * key, lsn);
  }
  jamEntry();

  return lsn;
}

Uint64
Dbtup::disk_page_undo_update(Signal *signal,
                             Page* page,
                             const Local_key* key,
			     const Uint32* src,
                             Uint32 sz,
			     Uint32 gci,
                             Uint32 logfile_group_id)
{
  jam();

  Disk_undo::Update update;
  update.m_page_no = key->m_page_no;
  update.m_file_no_page_idx= key->m_file_no << 16 | key->m_page_idx;
  update.m_gci= gci;
  
  update.m_type_length= 
    (Disk_undo::UNDO_UPDATE << 16) | (sz + (sizeof(update) >> 2) - 1);

  Logfile_client::Change c[3] = {
    { &update, 3 },
    { src, sz },
    { &update.m_type_length, 1 }
  };

  ndbassert(4*(3 + sz + 1) == (sizeof(update) + 4*sz - 4));

  Uint64 lsn;
  {
    D("Logfile_client - disk_page_undo_update");
    Logfile_client lgman(this, c_lgman, logfile_group_id);
    lsn= lgman.add_entry(c, 3);
  }
  jamEntry();
  {
    Page_cache_client pgman(this, c_pgman);
    pgman.update_lsn(signal, * key, lsn);
  }
  jamEntry();

  return lsn;
}
  
Uint64
Dbtup::disk_page_undo_free(Signal *signal,
                           Page* page,
                           const Local_key* key,
			   const Uint32* src,
                           Uint32 sz,
			   Uint32 gci,
                           Uint32 logfile_group_id)
{
  jam();

  Disk_undo::Free free;
  free.m_page_no = key->m_page_no;
  free.m_file_no_page_idx= key->m_file_no << 16 | key->m_page_idx;
  free.m_gci= gci;
  
  free.m_type_length= 
    (Disk_undo::UNDO_FREE << 16) | (sz + (sizeof(free) >> 2) - 1);
  
  Logfile_client::Change c[3] = {
    { &free, 3 },
    { src, sz },
    { &free.m_type_length, 1 }
  };
  
  ndbassert(4*(3 + sz + 1) == (sizeof(free) + 4*sz - 4));
  
  Uint64 lsn;
  {
    D("Logfile_client - disk_page_undo_free");
    Logfile_client lgman(this, c_lgman, logfile_group_id);
    lsn= lgman.add_entry(c, 3);
  }
  jamEntry();
  {
    Page_cache_client pgman(this, c_pgman);
    pgman.update_lsn(signal, * key, lsn);
  }
  jamEntry();
  return lsn;
}
  
#define DBG_UNDO 0

void
Dbtup::disk_restart_undo(Signal* signal,
                         Uint64 lsn,
			 Uint32 type,
                         const Uint32 * ptr,
                         Uint32 len)
{
  f_undo_done = false;
  f_undo.m_lsn= lsn;
  f_undo.m_ptr= ptr;
  f_undo.m_len= len;
  f_undo.m_type = type;

  Page_cache_client::Request preq;
  switch(f_undo.m_type){
  case File_formats::Undofile::UNDO_LOCAL_LCP_FIRST:
  case File_formats::Undofile::UNDO_LOCAL_LCP:
  case File_formats::Undofile::UNDO_LCP_FIRST:
  case File_formats::Undofile::UNDO_LCP:
  {
    jam();
    Uint32 lcpId;
    Uint32 localLcpId;
    Uint32 tableId;
    Uint32 fragId;
    if (f_undo.m_type == File_formats::Undofile::UNDO_LOCAL_LCP ||
        f_undo.m_type == File_formats::Undofile::UNDO_LOCAL_LCP_FIRST)
    {
      jam();
      ndbrequire(len == 4);
      lcpId = ptr[0];
      localLcpId = ptr[1];
      tableId = ptr[2] >> 16;
      fragId = ptr[2] & 0xFFFF;
    }
    else
    {
      jam();
      ndbrequire(len == 3);
      lcpId = ptr[0];
      localLcpId = 0;
      tableId = ptr[1] >> 16;
      fragId = ptr[1] & 0xFFFF;
    }
    disk_restart_undo_lcp(tableId,
                          fragId,
                          Fragrecord::UC_LCP,
                          lcpId,
                          localLcpId);
    if (!isNdbMtLqh())
      disk_restart_undo_next(signal);
    
    DEB_UNDO(("(%u)UNDO LCP [%u,%u] (%u,%u)",
              instance(),
              lcpId,
              localLcpId,
              tableId,
              fragId));
    return;
  }
  case File_formats::Undofile::UNDO_TUP_ALLOC:
  {
    jam();
    Disk_undo::Alloc* rec= (Disk_undo::Alloc*)ptr;
    preq.m_page.m_page_no = rec->m_page_no;
    preq.m_page.m_file_no  = rec->m_file_no_page_idx >> 16;
    preq.m_page.m_page_idx = rec->m_file_no_page_idx & 0xFFFF;
    break;
  }
  case File_formats::Undofile::UNDO_TUP_UPDATE:
  {
    jam();
    Disk_undo::Update* rec= (Disk_undo::Update*)ptr;
    preq.m_page.m_page_no = rec->m_page_no;
    preq.m_page.m_file_no  = rec->m_file_no_page_idx >> 16;
    preq.m_page.m_page_idx = rec->m_file_no_page_idx & 0xFFFF;
    break;
  }
  case File_formats::Undofile::UNDO_TUP_FREE:
  {
    jam();
    Disk_undo::Free* rec= (Disk_undo::Free*)ptr;
    preq.m_page.m_page_no = rec->m_page_no;
    preq.m_page.m_file_no  = rec->m_file_no_page_idx >> 16;
    preq.m_page.m_page_idx = rec->m_file_no_page_idx & 0xFFFF;
    break;
  }
  case File_formats::Undofile::UNDO_TUP_CREATE:
    /**
     * 
     */
  {
    jam();
    Disk_undo::Create* rec= (Disk_undo::Create*)ptr;
    Ptr<Tablerec> tabPtr;
    tabPtr.i= rec->m_table;
    if (tabPtr.i < cnoOfTablerec)
    {
      jam();
      ptrAss(tabPtr, tablerec);
      /**
       * We could come here in a number of situations.
       * 1) It is the first record we reach before finding any DROP
       *    or LCP record of this fragment. In this case we treat it
       *    as if it was the end of the LCP, we set undo_complete to
       *    UC_CREATE to ensure that no extents are allocated to us
       *    since we should start with an empty set of extents.
       * 2) It is a record that have been preceded by UNDO_TUP_DROP,
       *    in this case we can simply ignore the record.
       * 3) It could be a record that is preceded by the fragment
       *    LCP record. In this case the LCP happened after the
       *    create of the table and we can also safely ignore the
       *    this record.
       */
      DEB_UNDO(("(%u)UNDO_TUP_CREATE: lsn: %llu, tab: %u",
                instance(),
                lsn,
                tabPtr.i));
      for(Uint32 i = 0; i<NDB_ARRAY_SIZE(tabPtr.p->fragrec); i++)
      {
        jam();
        if (tabPtr.p->fragrec[i] != RNIL)
        {
          jam();
          jamLine(Uint16(tabPtr.p->fragid[i]));
          disk_restart_undo_lcp(tabPtr.i, tabPtr.p->fragid[i],
                                 Fragrecord::UC_CREATE, 0, 0);
        }
      }
    }
    if (!isNdbMtLqh())
      disk_restart_undo_next(signal);
    return;
  }
  case File_formats::Undofile::UNDO_TUP_DROP:
  {
    jam();
    Disk_undo::Drop* rec = (Disk_undo::Drop*)ptr;
    Ptr<Tablerec> tabPtr;
    /**
     * We could come here in a number of situations:
     * 1) It could be a record that belongs to a table that we are not
     *    restoring, in this case we won't find the table in the search
     *    below.
     * 2) It could be a record that have been preceded by a UNDO_TUP_CREATE
     *    record. In this case we should simply ignore this record since
     *    we know that this is belonging to an old table.
     * 3) It could be a record that is not preceded by any UNDO_TUP_CREATE
     *    record and in this case it must be the case that we have created
     *    a table, but the crash occurred before we got to write the
     *    UNDO_TUP_CREATE record. In this case we can treat this as if it
     *    was an UNDO_TUP_CREATE record.
     * 
     * Coming here after we reached the end of the fragment LCP should not
     * happen, so we insert an ndbrequire to ensure this doesn't happen.
     */
    tabPtr.i= rec->m_table;
    if (tabPtr.i < cnoOfTablerec)
    {
      jam();
      ptrAss(tabPtr, tablerec);
      DEB_UNDO(("(%u)UNDO_TUP_DROP: lsn: %llu, tab: %u",
               instance(),
               lsn,
               tabPtr.i));
      for(Uint32 i = 0; i<NDB_ARRAY_SIZE(tabPtr.p->fragrec); i++)
      {
        jam();
        if (tabPtr.p->fragrec[i] != RNIL)
        {
          jam();
          jamLine(Uint16(tabPtr.p->fragid[i]));
          disk_restart_undo_lcp(tabPtr.i, tabPtr.p->fragid[i],
                                Fragrecord::UC_DROP, 0, 0);
        }
      }
    }
    if (!isNdbMtLqh())
      disk_restart_undo_next(signal);
    return;
  }
  case File_formats::Undofile::UNDO_TUP_ALLOC_EXTENT:
    jam();
  case File_formats::Undofile::UNDO_TUP_FREE_EXTENT:
    jam();
    disk_restart_undo_next(signal);
    return;

  case File_formats::Undofile::UNDO_END:
    jam();
    f_undo_done = true;
    return;
  default:
    ndbrequire(false);
  }

  f_undo.m_key = preq.m_page;
  preq.m_table_id = (~0); /* Special code for table id for UNDO_REQ */
  preq.m_fragment_id = 0;
  preq.m_callback.m_callbackFunction = 
    safe_cast(&Dbtup::disk_restart_undo_callback);

  int flags = Page_cache_client::UNDO_REQ;
  Page_cache_client pgman(this, c_pgman);
  int res= pgman.get_page(signal, preq, flags);
  jamEntry();
  switch(res)
  {
  case 0:
    jam();
    m_immediate_flag = false;
    break; // Wait for callback
  case -1:
    ndbrequire(false);
    break;
  default:
    jam();
    /**
     * The m_immediate_flag variable stays false except for the time
     * from this call to execute until we reach the callback
     * where it is immediately read and immediately set back to
     * false again. Essentially this is a parameter to the
     * callback which is hard to get into the callback handling.
     */
    m_immediate_flag = true;
    execute(signal, preq.m_callback, res); // run callback
  }
}

void
Dbtup::disk_restart_undo_next(Signal* signal, Uint32 applied)
{
  signal->theData[0] = LgmanContinueB::EXECUTE_UNDO_RECORD;
  /* Flag indicating whether UNDO log was applied. */
  signal->theData[1] = applied;
  sendSignal(LGMAN_REF, GSN_CONTINUEB, signal, 2, JBB);
}

/**
 * This method is called before the UNDO log execution. It is called with
 * lcpId == RNIL when no LCP exists. It is called with the lcpId to restore
 * the fragment with when called with a value other than RNIL.
 */
void
Dbtup::disk_restart_lcp_id(Uint32 tableId,
                           Uint32 fragId,
                           Uint32 lcpId,
                           Uint32 localLcpId)
{
  /**
   * disk_restart_lcp_id is called from DBLQH when the restore of a
   * fragment is completed. At this time we know exactly which
   * lcpId that this fragment should use in its restore.
   * If no LCP is used to restore then lcpId is RNIL.
   */
  if (lcpId == RNIL)
  {
    jam();
    disk_restart_undo_lcp(tableId, fragId, Fragrecord::UC_CREATE, 0, 0);
    DEB_UNDO(("(%u)mark_no_lcp tab(%u,%u), UC_CREATE",
              instance(),
              tableId,
              fragId));
  }
  else
  {
    jam();
    disk_restart_undo_lcp(tableId,
                          fragId,
                          Fragrecord::UC_SET_LCP,
                          lcpId,
                          localLcpId); 
    DEB_UNDO(("(%u)mark_no_lcp tab(%u,%u), UC_SET_LCP",
              instance(),
              tableId,
              fragId));
  }
}

void
Dbtup::disk_restart_undo_lcp(Uint32 tableId,
                             Uint32 fragId,
                             Uint32 flag, 
			     Uint32 lcpId,
                             Uint32 localLcpId)
{
  Ptr<Tablerec> tabPtr;
  tabPtr.i= tableId;
  ptrCheckGuard(tabPtr, cnoOfTablerec, tablerec);

  if (tabPtr.p->tableStatus == DEFINED && tabPtr.p->m_no_of_disk_attributes)
  {
    jam();
    FragrecordPtr fragPtr;
    getFragmentrec(fragPtr, fragId, tabPtr.p);
    if (!fragPtr.isNull())
    {
      jam();
      DEB_UNDO(("(%u)tab(%u,%u), lcp(%u,%u), flag: %u",
                instance(),
                tableId,
                fragId,
                lcpId,
                localLcpId,
                flag));
      switch(flag){
      case Fragrecord::UC_DROP:
      {
        jam();
        /**
         * In this case we have decided to start with a table.
         * If the table was dropped it must have been another table
         * and this must happen after we have encountered either a
         * start of an LCP for the started fragment or an UNDO_TUP_CREATE
         * for the new table must have been reached. So we put an
         * ndbrequire to assert that this is actually the case.
         */
        ndbrequire(fragPtr.p->m_undo_complete != 0);
        return;
      }
      case Fragrecord::UC_CREATE:
        /**
         * We have reached a point in the undo log record where the table
         * was created. We need however to check if we are still executing
         * log records for this fragment. This particular fragment might
         * have completed a fragment LCP even though there are fragments
         * that have no completed LCPs since this LCP.
         * We require that we don't restart from an LCP if the UNDO_TUP_CREATE
         * is found, the LCP start undo log entry must come after the
         * UNDO_TUP_CREATE entry.
         * 
         * We also come here when we didn't find any LCP to restore from.
         * In this we simply need no UNDO logging since we will start the
         * fragment from an empty state.
         */
        if (!fragPtr.p->m_undo_complete)
        {
          jam();
	  fragPtr.p->m_undo_complete = Fragrecord::UC_CREATE;
          ndbassert(fragPtr.p->m_restore_lcp_id == RNIL);
        }
        else
        {
          jam();
          /**
           * We have already seen the end of the LCP either by seeing a
           * DROP before this point, or by seeing a CREATE before this
           * point or that there was a fragment LCP completed before
           * this record. Only if this is a DROP and the LCP was found
           * before this one there is cause for alarm.
           */
        }
	return;
      case Fragrecord::UC_LCP:
	jam();
        if (fragPtr.p->m_undo_complete == 0 &&
            fragPtr.p->m_restore_lcp_id == lcpId &&
            fragPtr.p->m_restore_local_lcp_id == localLcpId)
	{
	  jam();
          /**
           * We have reached the LCP UNDO log record, this indicates that the
           * fragment is now rolled back to where it should be.
           * We might still need to execute UNDO log record to synchronize the
           * page information with the extent bits.
           */
	  fragPtr.p->m_undo_complete = flag;
          DEB_UNDO(("(%u)tab(%u,%u) lcp(%u,%u) -> done",
                     instance(),
                     tableId,
                     fragId,
                     lcpId,
                     localLcpId));
	}
	return;
      case Fragrecord::UC_SET_LCP:
      {
	jam();
        /**
         * Used before UNDO log execution starts to set
         * m_restore_lcp_id for the fragment.
         */
        DEB_UNDO(("(%u)table(%u,%u) restore to lcp: %u",
                  instance(),
                  tableId,
                  fragId,
                  lcpId));
	ndbrequire(fragPtr.p->m_undo_complete == 0);
	ndbrequire(fragPtr.p->m_restore_lcp_id == RNIL);
	fragPtr.p->m_restore_lcp_id = lcpId;
	return;
      }
      }
      jamLine(flag);
      ndbrequire(false);
    }
  }
}

void
Dbtup::disk_restart_undo_callback(Signal* signal,
				  Uint32 id, 
				  Uint32 page_id)
{
  jamEntry();
  Ptr<GlobalPage> gpage;
  m_global_page_pool.getPtr(gpage, page_id);
  PagePtr pagePtr;
  pagePtr.i = gpage.i;
  pagePtr.p = reinterpret_cast<Page*>(gpage.p);
  bool immediate_flag = m_immediate_flag;
  m_immediate_flag = false;

  Apply_undo* undo = &f_undo;

  /**
   * Before we apply the UNDO record we need to discover which table
   * the page belongs to. For most pages this is listed in the page
   * header. However we cannot trust the page header since we could
   * come here with an UNDO log record for a page that have not ever
   * been written to disk after table creation. Worse the table could
   * even be listed as belonging to a different table and thus we
   * would create a mess here.
   *
   * To get the true identity of the page we will look up the table
   * in tsman, from this we will get the table id and fragment id
   * of the extent and this will also be the table id and fragment
   * id of the page we're dealing with here.
   *
   * Two things could happen here. We could come here with a page
   * that is belonging to table RNIL, this means that the page
   * was allocated after start of the LCP and also the extent was
   * allocated after the start of the LCP. In this case we don't
   * need to do anything, the extent isn't allocated to any table
   * and thus should remain a free extent and thus it doesn't make
   * sense to write to the page anything.
   *
   * Another variant is that the page belongs to a table which
   * isn't part of the restart, this can happen if the table
   * was dropped just before the crash.
   * Also in this case there is no need to do anything.
   *
   * Finally if we find that it belongs to an existing table, then
   * we will use this table id and fragment id here.
   *
   * Now the next question is if the page have been initialised
   * yet. We need to check 3 header variables for this.
   * table id, fragment id and table version.
   * Table id and fragment id isn't enough, the page could have belonged
   * a table with the same table id and fragment id, but it cannot at the
   * same time also have the same table version.
   *
   * Actually older versions didn't set the table version in the pages.
   * So it isn't possible here to be fully certain that the page belongs
   * to the correct table.
   *
   * A simple optimisation here is that this only needs to be done for
   * pages that misses in the page cache. If they are already in the page
   * cache then we can use the table id and fragment id as found in the
   * page header.
   *
   * For all pages that are changed or read into the page cache we will
   * also synchronize the extent bits with the page information.
   */

  if (! (pagePtr.p->list_index & 0x8000) ||
      pagePtr.p->nextList != RNIL ||
      pagePtr.p->prevList != RNIL)
  {
    jam();
    pagePtr.p->list_index |= 0x8000;
    pagePtr.p->nextList = pagePtr.p->prevList = RNIL;
    DEB_EXTENT_BITS(("(%u)Set list_index bit 0x8000 on page(%u,%u)"
                     " when undo",
                     instance(),
                     pagePtr.p->m_file_no,
                     pagePtr.p->m_page_no));
  }
  
  Uint32 tableId= pagePtr.p->m_table_id;
  Uint32 fragId = pagePtr.p->m_fragment_id;

  {
    /**
     * Ensure that the Page entry in PGMAN has the correct table id
     * fragment id set if it will be used in a future LCP.
     */
    Page_cache_client::Request preq;
    preq.m_page.m_file_no = undo->m_key.m_file_no;
    preq.m_page.m_page_no = undo->m_key.m_page_no;
    preq.m_table_id = tableId;
    preq.m_fragment_id = fragId;
    Page_cache_client pgman(this, c_pgman);
    ndbrequire(pgman.init_page_entry(preq));
  }

  if (tableId >= cnoOfTablerec)
  {
    jam();
    DEB_UNDO(("(%u)UNDO table> %u", instance(), tableId));
    disk_restart_undo_next(signal);
    return;
  }
  undo->m_table_ptr.i = tableId;
  ptrCheckGuard(undo->m_table_ptr, cnoOfTablerec, tablerec);
  
  if (! (undo->m_table_ptr.p->tableStatus == DEFINED && 
         undo->m_table_ptr.p->m_no_of_disk_attributes))
  {
    jam();
    DEB_UNDO(("(%u)UNDO !defined (%u) ", instance(), tableId));
    disk_restart_undo_next(signal);
    return;
  }

  Uint32 create_table_version = pagePtr.p->m_create_table_version;
  Uint32 page_version = ntohl(pagePtr.p->m_ndb_version);

  if (page_version >= NDB_DISK_V2)
  {
    if (create_table_version !=
          c_lqh->getCreateSchemaVersion(tableId))
    {
      jam();
      if (DBG_UNDO)
        ndbout_c("UNDO fragment null %u/%u, old,new=(%u,%u)",
                 tableId,
                 fragId,
                 create_table_version,
                 c_lqh->getCreateSchemaVersion(tableId));
      disk_restart_undo_next(signal);
      return;
    }
  }

  getFragmentrec(undo->m_fragment_ptr, fragId, undo->m_table_ptr.p);
  if(undo->m_fragment_ptr.isNull())
  {
    jam();
    DEB_UNDO(("(%u)UNDO fragment null tab(%u,%u)",
              instance(),
              tableId,
              fragId));
    disk_restart_undo_next(signal);
    return;
  }

  Local_key key = undo->m_key;
  
  Uint64 lsn = 0;
  Uint32 applied = 0;
  lsn += pagePtr.p->m_page_header.m_page_lsn_hi;
  lsn <<= 32;
  lsn += pagePtr.p->m_page_header.m_page_lsn_lo;

  undo->m_page_ptr = pagePtr;
  
  if (undo->m_lsn <= lsn &&
      !undo->m_fragment_ptr.p->m_undo_complete)
  {
    jam();
    
    applied = 1;
    /**
     * Apply undo record
     */
    switch(undo->m_type){
    case File_formats::Undofile::UNDO_TUP_ALLOC:
      jam();
      DEB_UNDO(("(%u)applying %lld UNDO_TUP_ALLOC on page(%u,%u).%u",
                instance(),
                undo->m_lsn,
                undo->m_key.m_file_no,
                undo->m_key.m_page_no,
                undo->m_key.m_page_idx));
      disk_restart_undo_alloc(undo);
      break;
    case File_formats::Undofile::UNDO_TUP_UPDATE:
      jam();
      DEB_UNDO(("(%u)applying %lld UNDO_TUP_UPDATE on page(%u,%u).%u",
                instance(),
                undo->m_lsn,
                undo->m_key.m_file_no,
                undo->m_key.m_page_no,
                undo->m_key.m_page_idx));
      disk_restart_undo_update(undo);
      break;
    case File_formats::Undofile::UNDO_TUP_FREE:
      jam();
      DEB_UNDO(("(%u)applying %lld UNDO_TUP_FREE on page(%u,%u).%u",
                instance(),
                undo->m_lsn,
                undo->m_key.m_file_no,
                undo->m_key.m_page_no,
                undo->m_key.m_page_idx));
      disk_restart_undo_free(undo);
      break;
    default:
      ndbrequire(false);
    }

    lsn = undo->m_lsn - 1; // make sure undo isn't run again...

    Page_cache_client pgman(this, c_pgman);
    pgman.update_lsn(signal, undo->m_key, lsn);
    jamEntry();

    disk_restart_undo_page_bits(signal, undo);
  }
  else
  {
    jam();
    if (!immediate_flag)
    {
      jam();
      /**
       * See Lemma 1 and Lemma 2 in analysis of extent page
       * synchronisation at restart.
       *
       * We don't need to call this function when immediate
       * flag since we already applied the first UNDO log
       * record on the page, there is no need to update
       * the page bits and the first log record have ensured
       * that the extent information is already allocated
       * properly.
       */
      disk_restart_undo_page_bits(signal, undo);
    }
    if (DBG_UNDO)
    {
      ndbout << "ignore: " << undo->m_lsn << "(" << lsn << " )" 
             << key << " type: " << undo->m_type 
             << " tab: " << tableId << endl;
    }
  }

  disk_restart_undo_next(signal, applied);
}

void
Dbtup::disk_restart_undo_alloc(Apply_undo* undo)
{
  ndbassert(undo->m_page_ptr.p->m_file_no == undo->m_key.m_file_no);
  ndbassert(undo->m_page_ptr.p->m_page_no == undo->m_key.m_page_no);
  if (undo->m_table_ptr.p->m_attributes[DD].m_no_of_varsize == 0)
  {
    ((Fix_page*)undo->m_page_ptr.p)->free_record(undo->m_key.m_page_idx);
  }
  else
    ((Var_page*)undo->m_page_ptr.p)->free_record(undo->m_key.m_page_idx, 0);
}

void
Dbtup::disk_restart_undo_update(Apply_undo* undo)
{
  Uint32* ptr;
  Uint32 len= undo->m_len - 4;
  if (undo->m_table_ptr.p->m_attributes[DD].m_no_of_varsize == 0)
  {
    ptr= ((Fix_page*)undo->m_page_ptr.p)->get_ptr(undo->m_key.m_page_idx, len);
    ndbrequire(len == undo->m_table_ptr.p->m_offsets[DD].m_fix_header_size);
  }
  else
  {
    ptr= ((Var_page*)undo->m_page_ptr.p)->get_ptr(undo->m_key.m_page_idx);
    abort();
  }  
  
  const Disk_undo::Update *update = (const Disk_undo::Update*)undo->m_ptr;
  const Uint32* src= update->m_data;
  memcpy(ptr, src, 4 * len);
}

void
Dbtup::disk_restart_undo_free(Apply_undo* undo)
{
  Uint32* ptr, idx = undo->m_key.m_page_idx;
  Uint32 len= undo->m_len - 4;
  if (undo->m_table_ptr.p->m_attributes[DD].m_no_of_varsize == 0)
  {
    ndbrequire(len == undo->m_table_ptr.p->m_offsets[DD].m_fix_header_size);
    idx= ((Fix_page*)undo->m_page_ptr.p)->alloc_record(idx);
    ptr= ((Fix_page*)undo->m_page_ptr.p)->get_ptr(idx, len);
  }
  else
  {
    abort();
  }  

  if (idx != undo->m_key.m_page_idx)
  {
    Uint64 lsn = undo->m_lsn;
    jam();
    jamLine(lsn & 0xFFFF);
    jamLine((lsn >> 16) & 0xFFFF);
    jamLine((lsn >> 32) & 0xFFFF);
    jamLine((lsn >> 48) & 0xFFFF);
    ndbrequire(false);
  }
  const Disk_undo::Free *free = (const Disk_undo::Free*)undo->m_ptr;
  const Uint32* src= free->m_data;
  memcpy(ptr, src, 4 * len);
}

void
Dbtup::disk_restart_undo_page_bits(Signal* signal, Apply_undo* undo)
{
  Fragrecord* fragPtrP = undo->m_fragment_ptr.p;
  Disk_alloc_info& alloc= fragPtrP->m_disk_alloc_info;
  
  /**
   * Set alloc.m_curr_extent_info_ptr_i to
   *   current this extent (and move old extend into free matrix)
   */
  Page* pageP = undo->m_page_ptr.p;
  Uint32 free = pageP->free_space;
  Uint32 new_bits = alloc.calc_page_free_bits(free);
  pageP->list_index = 0x8000 | new_bits;

  D("Tablespace_client - disk_restart_undo_page_bits");
  Tablespace_client tsman(signal, this, c_tsman,
			  fragPtrP->fragTableId,
	 		  fragPtrP->fragmentId,
                          c_lqh->getCreateSchemaVersion(fragPtrP->fragTableId),
			  fragPtrP->m_tablespace_id);

  DEB_EXTENT_BITS(("(%u)tab(%u,%u), page(%u,%u):%u new_bits: %u",
                  instance(),
                  fragPtrP->fragTableId,
                  fragPtrP->fragmentId,
                  pageP->m_file_no,
                  pageP->m_page_no,
                  undo->m_page_ptr.i,
                  new_bits));

  tsman.restart_undo_page_free_bits(&undo->m_key, new_bits);
  jamEntry();
}

/**
 * disk_restart_alloc_extent is called during scan of extent
 * headers in TSMAN. It ensures that we build the extent data
 * structures that ensures that we select the proper extent for
 * new records.
 *
 * The data to build is to start with the Extent_info struct.
 * m_free_space
 * ------------
 * This variable contains the number free records available
 * in the extent. It is initialised to
 * number of pages in extent times the number of records per
 * page when creating a new extent. Each prealloc will
 * decrease the number by one and each free will increase it
 * by one (also abort of prealloc).
 * At restarts we don't know the number so it is first set to
 * 0. Next it is set according to the page bits in the extent
 * information stored on disk by TSMAN.
 * The page bits on disk have the following meaning:
 * 0: The page is free, no records stored there
 * 1: The page is not free and not full, at least one record
 *    is stored in the page.
 * 2: The page is full
 * 3: The page is full
 *
 * For free pages we add number of records per page, for "half full"
 * pages we add to number of free pages in extent.
 * This means that this number is a minimum of the actual number of
 * free records in the extent.
 * Each time we use a page we will check the m_restart_seq variable on
 * the page (not checked during UNDO log execution since the variables
 * are not initialised at that time). If it isn't set to the
 * current m_restart_seq it means that the page is not yet fully
 * known. In this case we will call restart_setup_page that will
 * update the m_free_pages variable correctly for the page and will
 * also update the extent position (explained below).
 *
 * m_free_page_count
 * -----------------
 * For each state above we have a count of how many pages of each type
 * that we have. When initialised we set all pages to be in free bucket.
 * At restart we set all counters to 0, next we check each page in the
 * call to disk_restart_page_bits, this is called immediately after
 * the call to disk_restart_alloc_extent for each page in the extent.
 *
 * m_empty_page_no
 * ---------------
 * This is only used the first time we create the extent. It is never
 * used after a node restart. It makes sure that we allocate free
 * pages from the beginning of the extent to the end of the extent.
 * The variable isn't really necessary since it will work fairly good
 * also after a restart.
 *
 * m_first_page_no
 * ---------------
 * This is the page number of the first page in the extent. This is the
 * page id in the data file, so page id 3 is the 3rd 32kByte page in the
 * data file.
 *
 * m_key
 * -----
 * This represents the information about the extent page and extent number.
 * m_key.m_file_no is the file number of the extent
 * m_key.m_page_no is the page number of the first page in the extent
 * m_key.m_page_idx is the extent number, can be used to find the exact place
 *   of the extent information on the page
 *
 * nextHash, prevHash
 * ------------------
 * Each extent is placed in a hash table c_extent_hash. The key to this
 * hash table is m_key above, the m_page_no is not part of the key. So
 * a key with m_file_no set to file number and m_page_idx set to
 * extent number will find the appropriate extent.
 *
 * nextPool
 * --------
 * Used for linking free extent records in the c_extent_pool.
 * When allocated it is used to keep things in the m_extent_list.
 *
 * nextList, prevList
 * ------------------
 * Used to store the extent information in one of the 20 lists
 * in m_free_extents in the Disk_alloc_info struct as part of
 * the fragment.
 * The general idea about this matrix is explained in the
 * paper "Recovery in MySQL Cluster 5.1" presented at
 * VLDB 2005.
 *
 * m_free_matrix_pos
 * -----------------
 * This specifies which of the 20 lists the extent is currently
 * stored in. If set to RNIL then it is the extent referred to
 * from the m_curr_extent_info_ptr_i in the Disk_alloc_info
 * struct of the fragment. This indicates the current extent
 * used to insert data into.
 *
 * The data structures in Disk_alloc_info is referring to extent
 * information.
 *
 * Disk_alloc_info data variables (part of fragment)
 * -------------------------------------------------
 *
 * m_extent_size
 * -------------
 * Size of the extents used by this fragment
 *
 * m_curr_extent_info_ptr_i
 * ------------------------
 * Pointing to the current extent used for inserts, RNIL if
 * no current one.
 *
 * m_free_extents
 * --------------
 * List of extents as arranged in a matrix, there are 20
 * entries in a 5,4 matrix.
 *
 * The row information is the free level.
 * Row 0 is at least 80% free
 * Row 1 is at least 60% free
 * Row 2 is at least 40% free
 * Row 3 is at least 20% free
 * Row 4 is at least 0% free
 *
 * Col is based on the states described above. So if any page
 * in extent is fully free it will be in column 0.
 * If at least one page in extent is in "half full" state it
 * will be in column 1, if any page is in full state 2 it will
 * be in column 2 and otherwise it will be in column 3.
 * Search starts in Row 0 and goes through the columns, next
 * to Row 1 and so forth.
 *
 * m_total_extent_free_space_thresholds
 * ------------------------------------
 * This variable is static after creating the fragment. It
 * provides the levels on number of records for 80% level,
 * 60% level and so forth.
 *
 * m_page_free_bits_map
 * --------------------
 * This is also static information after creation of fragment.
 * It describes the number of free records in a page when in
 * states 0 through.
 * In state 0 it is set to records per page.
 * State 1 is set to 1
 * State 2 and 3 is set to 0.
 *
 * m_extent_list
 * -------------
 * This list is used for disk scans. In this case we need to know all
 * disk pages and these are found by scanning all extents one by one.
 * New extents are added first, so new pages added during scan are not
 * seen by the scan. Disk scans are currently only used for backups.
 *
 * m_dirty_pages
 * -------------
 * This is one list per state. When allocating a new page for insert we
 * search for a page in the free (state 0) and "half full" (state 1)
 * lists. If any page is in these lists we're done with our search of
 * page to insert into. This happens in disk_page_prealloc.
 * If a page is found in dirty pages we immediately update the
 * extent position of the page, we also move the page to another
 * list in m_dirty_pages if state changed due to insert, finally
 * we also update m_free_page_count above on the extent if state
 * changed.
 *
 * If the prealloc is aborted we remove the record from the page
 * and update the same structures again if necessary.
 *
 * When the page arrives from disk we also check whether there is a
 * need to change the m_free_page_count and extent position. A page
 * only arrives from disk after disk_page_prealloc if we were unable
 * to find a page among the ones already in memory that could fit the
 * new row. Here it is also placed in the proper m_dirty_pages list.
 * It is a new page at this point not currently in any list since it
 * comes from disk. It could actually come from the page cache still.
 * This could happen when a page have been read and is used for writing.
 * We don't use any knowledge of what pages have been read when
 * selecting which page to write.
 *
 * There are also some important variables on each page that is used
 * for page allocation.
 *
 * m_unmap_pages
 * -------------
 * Whenever a data page (not extent page) is to be flushed to disk PGMAN
 * will inform DBTUP about this. It will inform it before the flush and
 * also when the flush is completed.
 *
 * Before flush we will move the page away from the m_dirty_pages list
 * and into the m_unmap_pages list. If the dirty count is down to 0
 * we will also set list_index bit 0x8000 to indicate page is not in
 * dirty page list. We also set the uncommitted bits in the extent
 * information before we flush it to disk.
 *
 * After flush we will remove it from the unmap pages list.
 * We will also update the extent information if necessary and if it
 * has changed we will set the page to be dirty in PGMAN.
 *
 * m_page_requests
 * ---------------
 * This is a set of lists, one list for each state as described above.
 * Pages in these lists are in transit from disk to the memory to be
 * made dirty. Thus they are suitable to be used if no dirty pages are
 * available in memory. When we use those pages we will also move them
 * to the proper list to ensure that they are no longer used when already
 * full.
 *
 * list_index
 * ----------
 * This represents the state of the page from above (0 free, 1 "half full",
 * 2 and 3 full). Also if 0x8000 is set the page isn't in the m_dirty_pages
 * list.
 *
 * free_space
 * ----------
 * This is the count of the number of records stored on the page. It is
 * update by calls to free_record and alloc_record in tuppage.cpp.
 *
 * disk_page_prealloc
 * ------------------
 * This function is called to allocate a record for use in insert of disk
 * record. It returns the page id and page index of the row to be used.
 * The page isn't necessarily available in memory when returned from
 * this function. It is however guaranteed to at least be in transit
 * from disk. So the caller can safely call get_page on this page and
 * know that when it arrives it will be ready for consumption. The
 * callbacks are executed in order, so this means that
 * disk_page_prealloc_callback is called before the callback used by
 * the caller to actually perform the insert action.
 */
int
Dbtup::disk_restart_alloc_extent(EmulatedJamBuffer* jamBuf, 
                                 Uint32 tableId,
                                 Uint32 fragId,
                                 Uint32 create_table_version,
				 const Local_key* key,
                                 Uint32 pages)
{
  TablerecPtr tabPtr;
  FragrecordPtr fragPtr;
  tabPtr.i = tableId;
  ptrCheckGuard(tabPtr, cnoOfTablerec, tablerec);
  Uint32 current_create_table_version = c_lqh->getCreateSchemaVersion(tableId);
  DEB_EXTENT_BITS(("(%u)disk_restart_alloc_extent: tab(%u,%u):%u,"
                   " current version: %u",
                   instance(),
                   tableId,
                   fragId,
                   create_table_version,
                   current_create_table_version));

  if (tabPtr.p->tableStatus == DEFINED &&
      tabPtr.p->m_no_of_disk_attributes &&
      (current_create_table_version == create_table_version ||
       create_table_version == 0))
  {
    getFragmentrec(fragPtr, fragId, tabPtr.p);

    if (!fragPtr.isNull())
    {
      thrjam(jamBuf);

      Disk_alloc_info& alloc= fragPtr.p->m_disk_alloc_info;
      
      Ptr<Extent_info> ext;
      ndbrequire(c_extent_pool.seize(ext));
#ifdef VM_TRACE
      ndbout << "allocated " << pages << " pages: " << *key 
	     << " table: " << tabPtr.i << " fragment: " << fragId << endl;
#endif      
      ext.p->m_key = *key;
      ext.p->m_first_page_no = ext.p->m_key.m_page_no;
      ext.p->m_free_space= 0;
      ext.p->m_empty_page_no = (1 << 16); // We don't know, so assume none
      DEB_EXTENT_BITS(("(%u)restart:extent(%u) in tab(%u,%u),"
                       " first_page(%u,%u)",
                instance(),
                ext.p->m_key.m_page_idx,
                fragPtr.p->fragTableId,
                fragPtr.p->fragmentId,
                ext.p->m_key.m_file_no,
                ext.p->m_first_page_no));
      memset(ext.p->m_free_page_count, 0, sizeof(ext.p->m_free_page_count));
      
      if (alloc.m_curr_extent_info_ptr_i != RNIL)
      {
	thrjam(jamBuf);
	Ptr<Extent_info> old;
	c_extent_pool.getPtr(old, alloc.m_curr_extent_info_ptr_i);
	ndbassert(old.p->m_free_matrix_pos == RNIL);
	Uint32 pos= alloc.calc_extent_pos(old.p);
	Local_extent_info_list new_list(c_extent_pool, alloc.m_free_extents[pos]);
        new_list.addFirst(old);
	old.p->m_free_matrix_pos= pos;
      }
      
      alloc.m_curr_extent_info_ptr_i = ext.i;
      ext.p->m_free_matrix_pos = RNIL;
      c_extent_hash.add(ext);

      Local_fragment_extent_list list1(c_extent_pool, alloc.m_extent_list);
      list1.addFirst(ext);
      return 0;
    }
  }

  return -1;
}

void
Dbtup::disk_restart_page_bits(EmulatedJamBuffer* jamBuf, 
                              Uint32 tableId, Uint32 fragId,
			      const Local_key* key, Uint32 bits)
{
  thrjam(jamBuf);
  TablerecPtr tabPtr;
  FragrecordPtr fragPtr;
  tabPtr.i = tableId;
  ptrCheckGuard(tabPtr, cnoOfTablerec, tablerec);
  if (tabPtr.p->tableStatus == DEFINED && tabPtr.p->m_no_of_disk_attributes)
  {
    thrjam(jamBuf);
    getFragmentrec(fragPtr, fragId, tabPtr.p);
    Disk_alloc_info& alloc= fragPtr.p->m_disk_alloc_info;
    
    Ptr<Extent_info> ext;
    c_extent_pool.getPtr(ext, alloc.m_curr_extent_info_ptr_i);
    
    Uint32 size= alloc.calc_page_free_space(bits);  
    
    ext.p->m_free_page_count[bits]++;
    // actually only to update free_space
    update_extent_pos(jamBuf, alloc, ext, size);
    ndbassert(ext.p->m_free_matrix_pos == RNIL);
  }
}

void
Dbtup::disk_page_get_allocated(const Tablerec* tabPtrP,
                               const Fragrecord * fragPtrP,
                               Uint64 res[2])
{
  res[0] = res[1] = 0;
  if (tabPtrP->m_no_of_disk_attributes)
  {
    jam();
    const Disk_alloc_info& alloc= fragPtrP->m_disk_alloc_info;
    Uint64 cnt = 0;
    Uint64 free = 0;

    {
      Disk_alloc_info& tmp = const_cast<Disk_alloc_info&>(alloc);
      Local_fragment_extent_list list(c_extent_pool, tmp.m_extent_list);
      Ptr<Extent_info> extentPtr;
      for (list.first(extentPtr); !extentPtr.isNull(); list.next(extentPtr))
      {
        cnt++;
        free += extentPtr.p->m_free_space;
      }
    }
    res[0] = cnt * alloc.m_extent_size * File_formats::NDB_PAGE_SIZE;
    res[1] = free * 4 * tabPtrP->m_offsets[DD].m_fix_header_size;
  }
}
