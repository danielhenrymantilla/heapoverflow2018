#if !MALLOC_DEBUG 
# define check_malloc_state(A)
#else
# define check_malloc_state(A)         do_check_malloc_state (A)

/* ... */

static void
do_check_malloc_state (mstate av)
{

  /* ... */

  /* A contiguous main_arena is consistent with sbrk_base.  */
  if (av == &main_arena && contiguous (av))
    assert ((char *) mp_.sbrk_base + av->system_mem ==
            (char *) av->top + chunksize (av->top));




/* ----------- Routines dealing with system allocation -------------- */
 
/*
   sysmalloc handles malloc cases requiring more memory from the system.
   On entry, it is assumed that av->top does not have enough
   space to service request for nb bytes, thus requiring that av->top
   be extended or replaced.
 */
 
static void *
sysmalloc (INTERNAL_SIZE_T nb, mstate av)
{
  mchunkptr old_top;              /* incoming value of av->top */
  INTERNAL_SIZE_T old_size;       /* its size */
  char *old_end;                  /* its end address */
 
  long size;                      /* arg to first MORECORE or mmap call */
  char *brk;                      /* return value from MORECORE */
 
  long correction;                /* arg to 2nd MORECORE call */
  char *snd_brk;                  /* 2nd return val */
 
  INTERNAL_SIZE_T front_misalign; /* unusable bytes at front of new space */
  INTERNAL_SIZE_T end_misalign;   /* partial page left at end of new space */
  char *aligned_brk;              /* aligned offset into brk */

  /* ... */

  /* if_have_mmap
      ...
  */

  /* Record incoming configuration of top */
 
  old_top = av->top;
  old_size = chunksize (old_top);
  old_end = (char *) (chunk_at_offset (old_top, old_size));
 
  brk = snd_brk = (char *) (MORECORE_FAILURE);
 
  /*
     If not the first time through, we require old_size to be
     at least MINSIZE and to have prev_inuse set.
   */
 
  assert ((old_top == initial_top (av) && old_size == 0) ||
          ((unsigned long) (old_size) >= MINSIZE &&
           prev_inuse (old_top) &&
           ((unsigned long) old_end & (pagesize - 1)) == 0));
 
  /* Precondition: not enough current space to satisfy nb request */
  assert ((unsigned long) (old_size) < (unsigned long) (nb + MINSIZE));

  if (av != main_arena)
    {
      /* ... */
    }
  else     /* av == main_arena */

    { /* Request enough space for nb + pad + overhead */
      size = nb + mp_.top_pad + MINSIZE;
 
      /*
         If contiguous, we can subtract out existing space that we hope to
         combine with new space. We add it back later only if
         we don't actually get contiguous space.
       */
 
      if (contiguous (av))
        size -= old_size;
 
      /*
         Round to a multiple of page size.
         If MORECORE is not contiguous, this ensures that we only call it
         with whole-page arguments.  And if MORECORE is contiguous and
         this is not first time through, this preserves page-alignment of
         previous calls. Otherwise, we correct to page-align below.
       */
 
      size = ALIGN_UP (size, pagesize);
 
      /*
         Don't try to call MORECORE if argument is so big as to appear
         negative. Note that since mmap takes size_t arg, it may succeed
         below even if we cannot call MORECORE.
       */
 
      if (size > 0)
        {
          brk = (char *) (MORECORE (size));
          LIBC_PROBE (memory_sbrk_more, 2, brk, size);
        }
 
      if (brk != (char *) (MORECORE_FAILURE))
        {
          /* Call the `morecore' hook if necessary.  */
          void (*hook) (void) = atomic_forced_read (__after_morecore_hook);
          if (__builtin_expect (hook != NULL, 0))
            (*hook)();
        }
      else
        {
          /*
             If have mmap, try using it as a backup when MORECORE fails or
             cannot be used. This is worth doing on systems that have "holes" in
             address space, so sbrk cannot extend to give contiguous space, but
             space is available elsewhere.  Note that we ignore mmap max count
             and threshold limits, since the space will not be used as a
             segregated mmap region.
           */
 
          /* Cannot merge with old top, so add its size back in */
          if (contiguous (av))
            size = ALIGN_UP (size + old_size, pagesize);
 
          /* If we are relying on mmap as backup, then use larger units */
          if ((unsigned long) (size) < (unsigned long) (MMAP_AS_MORECORE_SIZE))
            size = MMAP_AS_MORECORE_SIZE;
 
          /* Don't try if size wraps around 0 */
          if ((unsigned long) (size) > (unsigned long) (nb))
            {
              char *mbrk = (char *) (MMAP (0, size, PROT_READ | PROT_WRITE, 0));
 
              if (mbrk != MAP_FAILED)
                {
                  /* We do not need, and cannot use, another sbrk call to find end */
                  brk = mbrk;
                  snd_brk = brk + size;
 
                  /*
                     Record that we no longer have a contiguous sbrk region.
                     After the first time mmap is used as backup, we do not
                     ever rely on contiguous space since this could incorrectly
                     bridge regions.
                   */
                  set_noncontiguous (av);
                }
            }
        }
 
      if (brk != (char *) (MORECORE_FAILURE))
        {
          if (mp_.sbrk_base == 0)
            mp_.sbrk_base = brk;
          av->system_mem += size;
 
          /*
             If MORECORE extends previous space, we can likewise extend top size.
           */
 
          if (brk == old_end && snd_brk == (char *) (MORECORE_FAILURE))
            set_head (old_top, (size + old_size) | PREV_INUSE);
 
          else if (contiguous (av) && old_size && brk < old_end)
            /* Oops!  Someone else killed our space..  Can't touch anything.  */
            malloc_printerr ("break adjusted to free malloc space");
 
          /*
             Otherwise, make adjustments:
 
           * If the first time through or noncontiguous, we need to call sbrk
              just to find out where the end of memory lies.
 
           * We need to ensure that all returned chunks from malloc will meet
              MALLOC_ALIGNMENT
 
           * If there was an intervening foreign sbrk, we need to adjust sbrk
              request size to account for fact that we will not be able to
              combine new space with existing space in old_top.
 
           * Almost all systems internally allocate whole pages at a time, in
              which case we might as well use the whole last page of request.
              So we allocate enough more memory to hit a page boundary now,
              which in turn causes future contiguous calls to page-align.
           */
 
          else
            {
              front_misalign = 0;
              end_misalign = 0;
              correction = 0;
              aligned_brk = brk;
 
              /* handle contiguous cases */
              if (contiguous (av))
                {
                  /* Count foreign sbrk as system_mem.  */
                  if (old_size)
                    av->system_mem += brk - old_end;
 
                  /* Guarantee alignment of first new chunk made from this space */
 
                  front_misalign = (INTERNAL_SIZE_T) chunk2mem (brk) & MALLOC_ALIGN_MASK;
                  if (front_misalign > 0)
                    {
                      /*
                         Skip over some bytes to arrive at an aligned position.
                         We don't need to specially mark these wasted front bytes.
                         They will never be accessed anyway because
                         prev_inuse of av->top (and any chunk created from its start)
                         is always true after initialization.
                       */
 
                      correction = MALLOC_ALIGNMENT - front_misalign;
                      aligned_brk += correction;
                    }
 
                  /*
                     If this isn't adjacent to existing space, then we will not
                     be able to merge with old_top space, so must add to 2nd request.
                   */
 
                  correction += old_size;
 
                  /* Extend the end address to hit a page boundary */
                  end_misalign = (INTERNAL_SIZE_T) (brk + size + correction);
                  correction += (ALIGN_UP (end_misalign, pagesize)) - end_misalign;
 
                  assert (correction >= 0);
                  snd_brk = (char *) (MORECORE (correction));
 
                  /*
                     If can't allocate correction, try to at least find out current
                     brk.  It might be enough to proceed without failing.
 
                     Note that if second sbrk did NOT fail, we assume that space
                     is contiguous with first sbrk. This is a safe assumption unless
                     program is multithreaded but doesn't use locks and a foreign sbrk
                     occurred between our first and second calls.
                   */
 
                  if (snd_brk == (char *) (MORECORE_FAILURE))
                    {
                      correction = 0;
                      snd_brk = (char *) (MORECORE (0));
                    }
                  else
                    {
                      /* Call the `morecore' hook if necessary.  */
                      void (*hook) (void) = atomic_forced_read (__after_morecore_hook);
                      if (__builtin_expect (hook != NULL, 0))
                        (*hook)();
                    }
                }
 
              /* handle non-contiguous cases */
              else
                {
                  if (MALLOC_ALIGNMENT == 2 * SIZE_SZ)
                    /* MORECORE/mmap must correctly align */
                    assert (((unsigned long) chunk2mem (brk) & MALLOC_ALIGN_MASK) == 0);
                  else
                    {
                      front_misalign = (INTERNAL_SIZE_T) chunk2mem (brk) & MALLOC_ALIGN_MASK;
                      if (front_misalign > 0)
                        {
                          /*
                             Skip over some bytes to arrive at an aligned position.
                             We don't need to specially mark these wasted front bytes.
                             They will never be accessed anyway because
                             prev_inuse of av->top (and any chunk created from its start)
                             is always true after initialization.
                           */
 
                          aligned_brk += MALLOC_ALIGNMENT - front_misalign;
                        }
                    }
 
                  /* Find out current end of memory */
                  if (snd_brk == (char *) (MORECORE_FAILURE))
                    {
                      snd_brk = (char *) (MORECORE (0));
                    }
                }
 
              /* Adjust top based on results of second sbrk */
              if (snd_brk != (char *) (MORECORE_FAILURE))
                {
                  av->top = (mchunkptr) aligned_brk;
                  set_head (av->top, (snd_brk - aligned_brk + correction) | PREV_INUSE);
                  av->system_mem += correction;
 
                  /*
                     If not the first time through, we either have a
                     gap due to foreign sbrk or a non-contiguous region.  Insert a
                     double fencepost at old_top to prevent consolidation with space
                     we don't own. These fenceposts are artificial chunks that are
                     marked as inuse and are in any case too small to use.  We need
                     two to make sizes and alignments work out.
                   */
 
                  if (old_size != 0)
                    {
                      /*
                         Shrink old_top to insert fenceposts, keeping size a
                         multiple of MALLOC_ALIGNMENT. We know there is at least
                         enough space in old_top to do this.
                       */
                      old_size = (old_size - 4 * SIZE_SZ) & ~MALLOC_ALIGN_MASK;
                      set_head (old_top, old_size | PREV_INUSE);
 
                      /*
                         Note that the following assignments completely overwrite
                         old_top when old_size was previously MINSIZE.  This is
                         intentional. We need the fencepost, even if old_top otherwise gets
                         lost.
                       */
                      set_head (chunk_at_offset (old_top, old_size),
                                (2 * SIZE_SZ) | PREV_INUSE);
                      set_head (chunk_at_offset (old_top, old_size + 2 * SIZE_SZ),
                                (2 * SIZE_SZ) | PREV_INUSE);
 
                      /* If possible, release the rest. */
                      if (old_size >= MINSIZE)
                        {
                          _int_free (av, old_top, 1);
                        }
                    }
                }
            }
        }
    } /* if (av !=  &main_arena) */
