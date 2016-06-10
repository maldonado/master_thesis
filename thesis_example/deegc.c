/*
  Dee System run-time Garbage Collection
  and Object Allocation routines.
  Lawrence A. Hegarty
  Feb. 1992
*/



#include <stdio.h>
#include "CeeGlob.h"

#define PROMO_AGE  5
#define MAX_GEN 4
#define OLDEST_GEN ( MAX_GEN - 1 )
#define CHUNK_SIZE (1*1024)

/* A little extra room for that last object that is always there */
#define ACTUAL_CHUNK_SIZE (CHUNK_SIZE+sizeof(struct Object))

#define NEXT(p) p -> Class
#define PREV(p) p -> Tag.Bool
#define SIZE(p) p -> Tag.GCStuff[1]


#define ACTIVE(p) ( p -> Status.Mark == MarkCount )
#define INACTIVE(p) ( p -> Status.Mark != MarkCount )
#define IS_LINKED_OBJECT(p) ( p -> Status.Link )

typedef struct Chunk *ChunkPtr;

struct Chunk {
  int ObjectCount;
  int BytesUsed, BytesFree;
  char* ObjSpace;
  ObjectPtr FreeList;
  ChunkPtr  NextChunk;
 };

static int MarkCount = 0;
static int LiveObjCount;
static int OallocCount = 0;
static long BytesOalloced = 0;
static long BytesMalloced = 0;
static ChunkPtr GenN[MAX_GEN];
static int GenChunkCount[MAX_GEN] = { 1, 1, 1, 1 };
static int GenNeedsColl[MAX_GEN] = { 0 };
static int LeastOnePromo = 0;


void AddToList( F, O )
     ObjectPtr F,O;
{
  ObjectPtr q = NEXT(F);

  O -> Status.Link = TRUE;
  PREV(O) = F;
  NEXT(O) = q;
  NEXT(F) = O;
  PREV(q) = O;
} /* Add to list */


void DeleteFromList( F, P )
     ObjectPtr F, P;
{
  ObjectPtr q = NEXT(P);
  ObjectPtr p = PREV(P);

  if ( P == F ) return;
  P -> Status.Link = FALSE;
  NEXT(p) = q;
  PREV(q) = p;
}

    

static ObjectPtr InitObject( O, Padding )
     ObjectPtr O;
     int Padding;
{
  O -> Status.Mark = MarkCount;
  O -> Status.Age = 0;
  O -> Status.Pad = Padding;
  O -> Status.Link = FALSE;
  O -> Status.Promo = FALSE;
  O -> Status.Test = FALSE;
  O -> Status.Gen = 0;
  return O;
}


GetMemory( Size )
     unsigned int Size;
{
  char *p = malloc( Size );
  if (!Size) {
    fprintf( stderr, "Can't get size 0 bytes.\n" );
    exit(2);
  }
  if (p) {
    BytesMalloced += Size;
    return p;
  } else {
    fprintf( stderr, "Out of memory (%d).\n", Size );
    exit(1);
  }
} /* get memory */


void GCReport()
{
  printf( "\nBytes Oalloced = %d, Bytes Malloced = %d\n",
	  BytesOalloced, BytesMalloced );
  printf( "Chunks per Gen: %d, %d, %d, %d\n", GenChunkCount[0],
	  GenChunkCount[1], GenChunkCount[2], GenChunkCount[3] );
}
  

ObjectPtr Oalloc( NewClass )
     ClassPtr NewClass;
{
  int NeedSize = NewClass -> InstanceSize;
  int Padding = 0, ChunkCount;
  ObjectPtr p;
  ChunkPtr TryChunk, NewChunk, LastChunk;
  int i;

  OallocCount++;
  BytesOalloced += NeedSize;
  for ( TryChunk = GenN[0]; TryChunk != NULL;
        LastChunk = TryChunk, TryChunk = TryChunk -> NextChunk ) 
    for ( p = TryChunk -> NEXT(FreeList);
	  p != TryChunk -> FreeList; p = NEXT(p) ) 
      if ( SIZE(p) == NeedSize )  {
	/* exact match, then just take it off the list and return it */
	DeleteFromList( TryChunk -> FreeList, p );
	TryChunk -> ObjectCount++;
	TryChunk -> BytesUsed += NeedSize;
	TryChunk -> BytesFree -= NeedSize;
	return InitObject( p, 0 );
      } else if ( SIZE(p) > NeedSize ) {
	/* if p is too big, add the rest back into the free list */
	char *tmp = (char *) p;  /* need a char ptr so we can add to it */
	ObjectPtr Rest = (ObjectPtr) (tmp + NeedSize);
	if ( tmp + NeedSize >= TryChunk -> ObjSpace + CHUNK_SIZE ) break;
	if ( SIZE(p) - NeedSize < sizeof( struct Object ) )  {
	  /* if the left over space is < an object, leave it with this obj. */
	  Padding = SIZE(p) - NeedSize;
	} else { /* else, there is enough left over for another object */
	  SIZE(Rest) = SIZE(p) - NeedSize;
	  AddToList( TryChunk -> FreeList, Rest );
	} /* else */
	DeleteFromList( TryChunk -> FreeList, p );
	TryChunk -> ObjectCount++;
	TryChunk -> BytesFree -= (NeedSize + Padding);
	TryChunk -> BytesUsed += (NeedSize + Padding);
	return InitObject( p, Padding );
      } /* take what we need and leave the rest in the free list */

  /* if we got here we need to allocate another chunk */
  Mark();
  for ( TryChunk = GenN[0]; TryChunk; TryChunk = TryChunk -> NextChunk )
    SweepChunk(TryChunk);

  for ( i = 1; i < MAX_GEN; i++ ) 
    if ( 1 || GenNeedsColl[i] )  {
      for ( TryChunk = GenN[i]; TryChunk; TryChunk = TryChunk -> NextChunk )
	SweepChunk(TryChunk);
      GenNeedsColl[i] = FALSE;
    } /* if */


  NewChunk = (ChunkPtr) GetMemory( sizeof( struct Chunk ) );
  NewChunk -> ObjSpace = (char *) GetMemory( ACTUAL_CHUNK_SIZE );
  NewChunk -> ObjectCount = 1;
  NewChunk -> BytesUsed = NeedSize;
  NewChunk -> BytesFree = CHUNK_SIZE - NeedSize;
  NewChunk -> FreeList = (ObjectPtr) (NewChunk -> ObjSpace + CHUNK_SIZE);
  NewChunk -> NEXT(FreeList) = NewChunk -> FreeList;
  NewChunk -> PREV(FreeList) = NewChunk -> FreeList;
  GenChunkCount[0]++;
  /* add the new chunk to the end of the list of chunks for Gen[0] */
#if 1
  LastChunk -> NextChunk = NewChunk;
  NewChunk -> NextChunk = NULL;
#else
  NewChunk -> NextChunk = Gen[0];
  Gen[0] = NewChunk;
#endif
  /* take enough for our obj and leave the rest in the free list */
  p = (ObjectPtr) (NewChunk -> ObjSpace + NeedSize);
  SIZE(p) = CHUNK_SIZE - NeedSize;
  AddToList( NewChunk -> FreeList, p );
  return InitObject( (ObjectPtr) (NewChunk -> ObjSpace), 0 );
} /* Object Allocate */



void InitGC( void )
{
  ChunkPtr NewChunk;
  ObjectPtr p;
  int i;

  for ( i = 0; i < MAX_GEN; i++ ) {
    NewChunk = (ChunkPtr) GetMemory( sizeof( struct Chunk ) );
    NewChunk -> ObjectCount = 0;
    NewChunk -> BytesUsed = 0;
    NewChunk -> BytesFree = CHUNK_SIZE;
    NewChunk -> ObjSpace = (char *) GetMemory( ACTUAL_CHUNK_SIZE );
    p = (ObjectPtr) NewChunk -> ObjSpace;
    NewChunk -> FreeList = (ObjectPtr) (NewChunk -> ObjSpace + CHUNK_SIZE);
    NewChunk -> NEXT(FreeList) = NewChunk -> FreeList;
    NewChunk -> PREV(FreeList) = NewChunk -> FreeList;
    SIZE(p) = CHUNK_SIZE;
    AddToList( NewChunk -> FreeList, p );
    GenN[i] = NewChunk;
    NewChunk -> NextChunk = NULL;
  } /* for */
} /* Init Garbage Collector */


static void ReclaimObj( O )
     ObjectPtr O;
{
  switch ( O -> Class -> ClassKind ) {
  case StringClass:
    free( O -> Tag.String );
    break;
  case ArrayClass:
    free( O -> Tag.ArrayBody[0] );
    break;
  } /* switch */
} /* Reclaim */


static ObjectPtr NewGenSlot( O )
     ObjectPtr O;
{
  int NeedSize = O -> Class -> InstanceSize;
  int Padding = 0, ChunkCount;
  ObjectPtr p;
  ChunkPtr TryChunk, NewChunk, LastChunk, NewGen;
  int X = O -> Status.Gen + 1;

  for ( TryChunk = GenN[X]; TryChunk != NULL;
        LastChunk = TryChunk, TryChunk = TryChunk -> NextChunk ) 
    for ( p = TryChunk -> NEXT(FreeList);
	  p != TryChunk -> FreeList; p = NEXT(p) ) 
      if ( SIZE(p) == NeedSize )  {
	/* exact match, then just take it off the list and return it */
	DeleteFromList( TryChunk -> FreeList, p );
	TryChunk -> ObjectCount++;
	TryChunk -> BytesUsed += NeedSize;
	TryChunk -> BytesFree -= NeedSize;
	p -> Status.Pad = 0;
	return p;
      } else if ( SIZE(p) > NeedSize ) {
	/* if p is too big, add the rest back into the free list */
	char *tmp = (char *) p;  /* need a char ptr so we can add to it */
	ObjectPtr Rest = (ObjectPtr) (tmp + NeedSize);
	if ( tmp + NeedSize >= TryChunk -> ObjSpace + CHUNK_SIZE ) break;
	if ( SIZE(p) - NeedSize < sizeof( struct Object ) )  {
	  /* if the left over space is < an object, leave it with this obj. */
	  Padding = SIZE(p) - NeedSize;
	} else { /* else, there is enough left over for another object */
	  SIZE(Rest) = SIZE(p) - NeedSize;
	  AddToList( TryChunk -> FreeList, Rest );
	} /* else */
	DeleteFromList( TryChunk -> FreeList, p );
	TryChunk -> ObjectCount++;
	TryChunk -> BytesFree -= (NeedSize + Padding);
	TryChunk -> BytesUsed += (NeedSize + Padding);
	p -> Status.Pad = Padding;
	return p;
      } /* take what we need and leave the rest in the free list */

  GenNeedsColl[X] = TRUE;
  /* if we got here we need to allocate another chunk */
  NewChunk = (ChunkPtr) GetMemory( sizeof( struct Chunk ) );
  NewChunk -> ObjSpace = (char *) GetMemory( ACTUAL_CHUNK_SIZE );
  NewChunk -> ObjectCount = 1;
  NewChunk -> BytesUsed = NeedSize;
  NewChunk -> BytesFree = CHUNK_SIZE - NeedSize;
  NewChunk -> FreeList = (ObjectPtr) (NewChunk -> ObjSpace + CHUNK_SIZE);
  NewChunk -> NEXT(FreeList) = NewChunk -> FreeList;
  NewChunk -> PREV(FreeList) = NewChunk -> FreeList;
  GenChunkCount[X]++;
  /* add the new chunk to the end of the list of chunks for Gen1 */
#if 1
  LastChunk -> NextChunk = NewChunk;
  NewChunk -> NextChunk = NULL;
#else
  NewChunk -> NextChunk = GenN[X];
  GenN[X]; = NewChunk;
#endif
  /* take enough for our obj and leave the rest in the free list */
  p = (ObjectPtr) (NewChunk -> ObjSpace + NeedSize);
  SIZE(p) = CHUNK_SIZE - NeedSize;
  AddToList( NewChunk -> FreeList, p );
  p = (ObjectPtr) ( NewChunk -> ObjSpace );
  p -> Status.Pad = 0;
  return p;
}  /* New Gen Slot */  


ObjectPtr PromoteObj( O )
     ObjectPtr O;
{
  ObjectPtr NewLoc = NULL;
  int OldPadding;
  
  if ( O -> Status.Gen == OLDEST_GEN ) return O;
  LeastOnePromo = TRUE;
  O -> Status.Promo = TRUE;
  NewLoc = NewGenSlot( O );
  OldPadding = NewLoc -> Status.Pad;
  memcpy( NewLoc, O, O -> Class -> InstanceSize );
  NewLoc -> Status.Mark = MarkCount;
  NewLoc -> Status.Age = 0;
  NewLoc -> Status.Promo = OldPadding;
  NewLoc -> Status.Link = 0;
  NewLoc -> Status.Gen++;
  NewLoc -> Status.Test = FALSE;
  O -> Tag.InstVars[0] = NewLoc;
  return NewLoc;
} /* Promote Object */


void Mark()
{
  int i;
  ChunkPtr TryChunk;
  
  LiveObjCount = 0;
  MarkCount++;

#ifdef DEBUG
  printf( "\nMark: hwm = %d, ", hwm);
#endif

  MarkObject( true_object );
  MarkObject( false_object );
  MarkObject( NIL );

  for ( i = 0; i < hwm; i++ ) 
    if ( os[i] != NIL ) MarkObject( os[i] );

  if ( LeastOnePromo ) {
    /* adjust pointers for moved objects */
    if ( true_object -> Status.Promo )
      true_object = true_object -> Tag.InstVars[0];
    if ( false_object -> Status.Promo )
      false_object = false_object -> Tag.InstVars[0];
    if ( NIL -> Status.Promo )
      NIL = NIL -> Tag.InstVars[0];
    
    for ( i = 0; i < hwm; i++ ) {
      if ( os[i] -> Status.Promo ) os[i] = os[i] -> Tag.InstVars[0];
      TestObject( os[i] );
    } /* for */
    LeastOnePromo = FALSE;
  } /* if at least one promotion */

#ifdef DEBUG
  printf( "Live Obj. Count = %d, Oalloced = %d\n", LiveObjCount, OallocCount );
#endif

} /* Mark */


static void MarkObject( O )
     ObjectPtr O;
{
  int i;
  
  if ( ACTIVE(O) || O -> Status.Promo ) return;

  O -> Status.Age++;
  O -> Status.Test = FALSE;
  if ( O -> Status.Age > PROMO_AGE )
    O = PromoteObj( O );
  else
    O -> Status.Mark = MarkCount;
  LiveObjCount++;
  for ( i = 0; i < O -> Class -> InstanceCount; i++ )
    if ( O -> Tag.InstVars[i] != NIL ) MarkObject( O -> Tag.InstVars[i] );
} /* Mark Object */


TestObject( O ) 
     ObjectPtr O;
{
  int i;

  if ( O -> Status.Test ) return;

  O -> Status.Test = TRUE;
  for ( i = 0; i < O -> Class -> InstanceCount; i++ )  {
    if ( O -> Tag.InstVars[i] -> Status.Promo )
      O -> Tag.InstVars[i] = O -> Tag.InstVars[i] -> Tag.InstVars[0];
    TestObject( O -> Tag.InstVars[i] );
  }
} /* TestObject */



void SweepChunk( C )
     ChunkPtr C;
{
  char *p = C -> ObjSpace;
  ObjectPtr o;
  int MarkedCount = 0;
  int UsedBytes = 0;
  int FreeBytes = 0;
  int ReclaimedCount = 0;
 
#ifdef DEBUG
  printf( "Sweep: Bytes Used = %d, Bytes Free = %d,",
	  C -> BytesUsed, C -> BytesFree );
#endif

  /* Make new free list before we sweep */
  C -> NEXT(FreeList) = C -> FreeList;
  C -> PREV(FreeList) = C -> FreeList;

  /* The Sweep */
  while ( p < C -> ObjSpace + CHUNK_SIZE ) {
    o = (ObjectPtr) p;
    if ( IS_LINKED_OBJECT(o) || INACTIVE(o) ) {
      int Size;
      char *p2;
      ObjectPtr o2;
      int FreeHere;
      if ( IS_LINKED_OBJECT(o) ) {
	Size = SIZE(o);
	p2 = p + Size;
	o2 = (ObjectPtr) p2;
      } else {
	Size = o -> Class -> InstanceSize + o -> Status.Pad;
	ReclaimObj( o );
	ReclaimedCount++;
	p2 = p + Size;
	o2 = (ObjectPtr) p2;
      }
      FreeHere = Size;
      while ( p2 < C -> ObjSpace + CHUNK_SIZE && INACTIVE(o2) ) {
	int t;
	if ( IS_LINKED_OBJECT(o2) )
	  t = SIZE(o2);
	else {
	  t = o2 -> Class -> InstanceSize + o2 -> Status.Pad;
	  ReclaimObj( o2 );
	  ReclaimedCount++;
	}
	FreeHere += t;
	p2 += t;
	o2 = (ObjectPtr) p2;
      } /* while */
      /* Add this slot into the free list */
      SIZE(o) = FreeHere;
      o -> Status.Mark = MarkCount;
      AddToList( C -> FreeList, o );
      FreeBytes += FreeHere;
      p += FreeHere;
    } else {  /* this slot is active */
      MarkedCount++;
      UsedBytes += o -> Class -> InstanceSize + o -> Status.Pad;
      p += o -> Class -> InstanceSize + o -> Status.Pad;
    } /* else this slot is active */
  } /* while */

#ifdef DEBUG
  printf("\n       Total = %d, Active = %d,", C -> ObjectCount, MarkedCount);  
  printf( " UsedBytes = %d, FreeBytes = %d\n", UsedBytes, FreeBytes );
#endif
  C -> BytesFree = FreeBytes;
  C -> BytesUsed = UsedBytes;
  C -> ObjectCount -= ReclaimedCount;
} /* Sweep */
