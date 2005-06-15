/*-------------------------------------------------------------------------
 *
 * gist_private.h
 *	  private declarations for GiST -- declarations related to the
 *	  internal implementation of GiST, not the public API
 *
 * Portions Copyright (c) 1996-2005, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/access/gist_private.h,v 1.3 2005/06/14 11:45:14 teodor Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef GIST_PRIVATE_H
#define GIST_PRIVATE_H

#include "access/itup.h"
#include "access/gist.h"
#include "access/xlog.h"
#include "access/xlogdefs.h"
#include "fmgr.h"

/*
 * When we descend a tree, we keep a stack of parent pointers. This
 * allows us to follow a chain of internal node points until we reach
 * a leaf node, and then back up the stack to re-examine the internal
 * nodes.
 *
 * 'parent' is the previous stack entry -- i.e. the node we arrived
 * from. 'block' is the node's block number. 'offset' is the offset in
 * the node's page that we stopped at (i.e. we followed the child
 * pointer located at the specified offset).
 */
typedef struct GISTSTACK
{
	struct GISTSTACK *parent;
	OffsetNumber offset;
	BlockNumber block;
} GISTSTACK;

typedef struct GISTSTATE
{
	FmgrInfo	consistentFn[INDEX_MAX_KEYS];
	FmgrInfo	unionFn[INDEX_MAX_KEYS];
	FmgrInfo	compressFn[INDEX_MAX_KEYS];
	FmgrInfo	decompressFn[INDEX_MAX_KEYS];
	FmgrInfo	penaltyFn[INDEX_MAX_KEYS];
	FmgrInfo	picksplitFn[INDEX_MAX_KEYS];
	FmgrInfo	equalFn[INDEX_MAX_KEYS];

	TupleDesc	tupdesc;
} GISTSTATE;

/*
 *	When we're doing a scan, we need to keep track of the parent stack
 *	for the marked and current items.
 */
typedef struct GISTScanOpaqueData
{
	GISTSTACK			*stack;
	GISTSTACK			*markstk;
	uint16				 flags;
	GISTSTATE			*giststate;
	MemoryContext		 tempCxt;
	Buffer				 curbuf;
	Buffer				 markbuf;
} GISTScanOpaqueData;

typedef GISTScanOpaqueData *GISTScanOpaque;

/*
 * GISTInsertStack used for locking buffers and transfer arguments during
 * insertion
 */

typedef struct GISTInsertStack {
	/* current page */
	BlockNumber	blkno;	 
	Buffer		buffer;
	Page		page;
	
	/* child's offset */
	OffsetNumber	childoffnum;

	/* pointer to parent */
	struct GISTInsertStack	*parent;

	bool todelete;
} GISTInsertStack;

typedef struct {
	Relation	r;
	IndexTuple      *itup; /* in/out, points to compressed entry */
	int             ituplen; /* length of itup */
	GISTInsertStack	*stack;
	bool needInsertComplete;
	bool xlog_mode;

	/* pointer to heap tuple */
	ItemPointerData	key;

	/* path to stroe in XLog */
	BlockNumber	*path;
	int 		pathlen; 
} GISTInsertState;

/*
 * When we're doing a scan and updating a tree at the same time, the
 * updates may affect the scan.  We use the flags entry of the scan's
 * opaque space to record our actual position in response to updates
 * that we can't handle simply by adjusting pointers.
 */
#define GS_CURBEFORE	((uint16) (1 << 0))
#define GS_MRKBEFORE	((uint16) (1 << 1))

/* root page of a gist index */
#define GIST_ROOT_BLKNO				0

/*
 * When we update a relation on which we're doing a scan, we need to
 * check the scan and fix it if the update affected any of the pages
 * it touches.  Otherwise, we can miss records that we should see.
 * The only times we need to do this are for deletions and splits. See
 * the code in gistscan.c for how the scan is fixed. These two
 * constants tell us what sort of operation changed the index.
 */
#define GISTOP_DEL		0
#define GISTOP_SPLIT	1

#define ATTSIZE(datum, tupdesc, i, isnull) \
        ( \
                (isnull) ? 0 : \
                   att_addlength(0, (tupdesc)->attrs[(i)-1]->attlen, (datum)) \
        ) 

/* XLog stuff */
#define	XLOG_GIST_ENTRY_UPDATE	0x00
#define	XLOG_GIST_ENTRY_DELETE	0x10
#define XLOG_GIST_NEW_ROOT	0x20

typedef struct gistxlogEntryUpdate {
	RelFileNode	node;
	BlockNumber	blkno;

	/* if todeleteoffnum!=InvalidOffsetNumber then delete it. */ 
	OffsetNumber	todeleteoffnum;
	uint16		pathlen;

	/* 
	 * It used to identify compliteness of insert.
         * Sets to leaf itup 
         */ 
	ItemPointerData	key;

	/* follow:
	 * 1. path to root (BlockNumber) 
	 * 2. tuples to insert
         */ 
} gistxlogEntryUpdate;

#define XLOG_GIST_PAGE_SPLIT	0x30

typedef struct gistxlogPageSplit {
	RelFileNode	node;
	BlockNumber	origblkno; /*splitted page*/
	OffsetNumber	todeleteoffnum;
	uint16		pathlen;
	int		npage;
	int		nitup;

	/* see comments on gistxlogEntryUpdate */
	ItemPointerData	key;
 
	/* follow:
	 * 1. path to root (BlockNumber) 
	 * 2. tuples to insert
	 * 3. gistxlogPage and array of OffsetNumber per page
         */ 
} gistxlogPageSplit;

typedef struct gistxlogPage {
	BlockNumber	blkno;
	int		num;
} gistxlogPage;	


#define XLOG_GIST_INSERT_COMPLETE  0x40

typedef struct gistxlogInsertComplete {
	RelFileNode	node;
	ItemPointerData	key;
} gistxlogInsertComplete;

#define XLOG_GIST_CREATE_INDEX	0x50

/* gist.c */
extern Datum gistbuild(PG_FUNCTION_ARGS);
extern Datum gistinsert(PG_FUNCTION_ARGS);
extern Datum gistbulkdelete(PG_FUNCTION_ARGS);
extern MemoryContext createTempGistContext(void);
extern void initGISTstate(GISTSTATE *giststate, Relation index);
extern void freeGISTstate(GISTSTATE *giststate);
extern void gistnewroot(Relation r, IndexTuple *itup, int len, ItemPointer key, bool xlog_mode);
extern void gistmakedeal(GISTInsertState *state, GISTSTATE *giststate);

/* gistxlog.c */
extern void gist_redo(XLogRecPtr lsn, XLogRecord *record);
extern void gist_desc(char *buf, uint8 xl_info, char *rec);
extern void gist_xlog_startup(void);
extern void gist_xlog_cleanup(void);

/* gistget.c */
extern Datum gistgettuple(PG_FUNCTION_ARGS);
extern Datum gistgetmulti(PG_FUNCTION_ARGS);

/* gistutil.c */
extern OffsetNumber gistfillbuffer(Relation r, Page page, IndexTuple *itup,
                                int len, OffsetNumber off);
extern bool gistnospace(Page page, IndexTuple *itvec, int len);
extern IndexTuple * gistextractbuffer(Buffer buffer, int *len /* out */ );
extern IndexTuple * gistjoinvector(
                           IndexTuple *itvec, int *len,
                           IndexTuple *additvec, int addlen);
extern IndexTuple gistunion(Relation r, IndexTuple *itvec,
                  int len, GISTSTATE *giststate);
extern IndexTuple gistgetadjusted(Relation r,
                                IndexTuple oldtup,
                                IndexTuple addtup,
                                GISTSTATE *giststate);
extern int gistfindgroup(GISTSTATE *giststate,
                          GISTENTRY *valvec, GIST_SPLITVEC *spl);
extern void gistadjsubkey(Relation r,
                          IndexTuple *itup, int *len,
                          GIST_SPLITVEC *v,
                          GISTSTATE *giststate);
extern IndexTuple gistFormTuple(GISTSTATE *giststate,
                        Relation r, Datum *attdata, int *datumsize, bool *isnull);

extern OffsetNumber gistchoose(Relation r, Page p,
                   IndexTuple it,
                   GISTSTATE *giststate);
extern void gistcentryinit(GISTSTATE *giststate, int nkey,
                           GISTENTRY *e, Datum k, 
                           Relation r, Page pg,
                           OffsetNumber o, int b, bool l, bool isNull);
extern void gistDeCompressAtt(GISTSTATE *giststate, Relation r,
                              IndexTuple tuple, Page p, OffsetNumber o,
                              GISTENTRY *attdata, bool *isnull);
extern void gistunionsubkey(Relation r, GISTSTATE *giststate, 
                            IndexTuple *itvec, GIST_SPLITVEC *spl);
extern void GISTInitBuffer(Buffer b, uint32 f);
extern void gistdentryinit(GISTSTATE *giststate, int nkey, GISTENTRY *e,
			   Datum k, Relation r, Page pg, OffsetNumber o,
			   int b, bool l, bool isNull);

#endif	/* GIST_PRIVATE_H */