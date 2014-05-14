
#include "main.h"

//${CONFIG_BEGIN}
#define CFG_BRL_OS_IMPLEMENTED 1
#define CFG_CONFIG debug
#define CFG_CPP_DOUBLE_PRECISION_FLOATS 1
#define CFG_CPP_GC_MODE 1
#define CFG_DEBUG 1
#define CFG_HOST macos
#define CFG_LANG cpp
#define CFG_REFLECTION_FILTER wizard.commands.*
#define CFG_SAFEMODE 0
#define CFG_TARGET stdcpp
//${CONFIG_END}

//${TRANSCODE_BEGIN}

#include <wctype.h>
#include <locale.h>

// C++ Monkey runtime.
//
// Placed into the public domain 24/02/2011.
// No warranty implied; use at your own risk.

//***** Monkey Types *****

typedef wchar_t Char;
template<class T> class Array;
class String;
class Object;

#if CFG_CPP_DOUBLE_PRECISION_FLOATS
typedef double Float;
#define FLOAT(X) X
#else
typedef float Float;
#define FLOAT(X) X##f
#endif

void dbg_error( const char *p );

#if !_MSC_VER
#define sprintf_s sprintf
#define sscanf_s sscanf
#endif

//***** GC Config *****

#define DEBUG_GC 0

// GC mode:
//
// 0 = disabled
// 1 = Incremental GC every OnWhatever
// 2 = Incremental GC every allocation
//
#ifndef CFG_CPP_GC_MODE
#define CFG_CPP_GC_MODE 1
#endif

//How many bytes alloced to trigger GC
//
#ifndef CFG_CPP_GC_TRIGGER
#define CFG_CPP_GC_TRIGGER 8*1024*1024
#endif

//GC_MODE 2 needs to track locals on a stack - this may need to be bumped if your app uses a LOT of locals, eg: is heavily recursive...
//
#ifndef CFG_CPP_GC_MAX_LOCALS
#define CFG_CPP_GC_MAX_LOCALS 8192
#endif

// ***** GC *****

#if _WIN32

int gc_micros(){
	static int f;
	static LARGE_INTEGER pcf;
	if( !f ){
		if( QueryPerformanceFrequency( &pcf ) && pcf.QuadPart>=1000000L ){
			pcf.QuadPart/=1000000L;
			f=1;
		}else{
			f=-1;
		}
	}
	if( f>0 ){
		LARGE_INTEGER pc;
		if( QueryPerformanceCounter( &pc ) ) return pc.QuadPart/pcf.QuadPart;
		f=-1;
	}
	return 0;// timeGetTime()*1000;
}

#elif __APPLE__

#include <mach/mach_time.h>

int gc_micros(){
	static int f;
	static mach_timebase_info_data_t timeInfo;
	if( !f ){
		mach_timebase_info( &timeInfo );
		timeInfo.denom*=1000L;
		f=1;
	}
	return mach_absolute_time()*timeInfo.numer/timeInfo.denom;
}

#else

int gc_micros(){
	return 0;
}

#endif

#define gc_mark_roots gc_mark

void gc_mark_roots();

struct gc_object;

gc_object *gc_object_alloc( int size );
void gc_object_free( gc_object *p );

struct gc_object{
	gc_object *succ;
	gc_object *pred;
	int flags;
	
	virtual ~gc_object(){
	}
	
	virtual void mark(){
	}
	
	void *operator new( size_t size ){
		return gc_object_alloc( size );
	}
	
	void operator delete( void *p ){
		gc_object_free( (gc_object*)p );
	}
};

gc_object gc_free_list;
gc_object gc_marked_list;
gc_object gc_unmarked_list;
gc_object gc_queued_list;	//doesn't really need to be doubly linked...

int gc_free_bytes;
int gc_marked_bytes;
int gc_alloced_bytes;
int gc_max_alloced_bytes;
int gc_new_bytes;
int gc_markbit=1;

gc_object *gc_cache[8];

int gc_ctor_nest;
gc_object *gc_locals[CFG_CPP_GC_MAX_LOCALS],**gc_locals_sp=gc_locals;

void gc_collect_all();
void gc_mark_queued( int n );

#define GC_CLEAR_LIST( LIST ) ((LIST).succ=(LIST).pred=&(LIST))

#define GC_LIST_IS_EMPTY( LIST ) ((LIST).succ==&(LIST))

#define GC_REMOVE_NODE( NODE ){\
(NODE)->pred->succ=(NODE)->succ;\
(NODE)->succ->pred=(NODE)->pred;}

#define GC_INSERT_NODE( NODE,SUCC ){\
(NODE)->pred=(SUCC)->pred;\
(NODE)->succ=(SUCC);\
(SUCC)->pred->succ=(NODE);\
(SUCC)->pred=(NODE);}

void gc_init1(){
	GC_CLEAR_LIST( gc_free_list );
	GC_CLEAR_LIST( gc_marked_list );
	GC_CLEAR_LIST( gc_unmarked_list);
	GC_CLEAR_LIST( gc_queued_list );
}

void gc_init2(){
	gc_mark_roots();
}

#if CFG_CPP_GC_MODE==2

struct gc_ctor{
	gc_ctor(){ ++gc_ctor_nest; }
	~gc_ctor(){ --gc_ctor_nest; }
};

struct gc_enter{
	gc_object **sp;
	gc_enter():sp(gc_locals_sp){
	}
	~gc_enter(){
	/*
		static int max_locals;
		int n=gc_locals_sp-gc_locals;
		if( n>max_locals ){
			max_locals=n;
			printf( "max_locals=%i\n",n );
		}
	*/
		gc_locals_sp=sp;
	}
};

#define GC_CTOR gc_ctor _c;
#define GC_ENTER gc_enter _e;

#else

struct gc_ctor{
};
struct gc_enter{
};

#define GC_CTOR
#define GC_ENTER

#endif

void gc_flush_free( int size ){

	int t=gc_free_bytes-size;
	if( t<0 ) t=0;
	
	while( gc_free_bytes>t ){
		gc_object *p=gc_free_list.succ;
		if( !p || p==&gc_free_list ){
//			printf( "GC_ERROR:p=%p gc_free_bytes=%i\n",p,gc_free_bytes );
//			fflush(stdout);
			gc_free_bytes=0;
			break;
		}
		GC_REMOVE_NODE(p);
		delete p;	//...to gc_free
	}
}


//Can be modified off thread!
static volatile int gc_ext_new_bytes;

#if _MSC_VER
#define atomic_add(P,V) InterlockedExchangeAdd((volatile unsigned int*)P,V)//(*(P)+=(V))
#define atomic_sub(P,V) InterlockedExchangeSubtract((volatile unsigned int*)P,V)//(*(P)-=(V))
#else
#define atomic_add(P,V) __sync_fetch_and_add(P,V)
#define atomic_sub(P,V) __sync_fetch_and_sub(P,V)
#endif

//Careful! May be called off thread!
//
void gc_ext_malloced( int size ){
	atomic_add( &gc_ext_new_bytes,size );
}

gc_object *gc_object_alloc( int size ){

	size=(size+7)&~7;
	
	gc_new_bytes+=size;
	
#if CFG_CPP_GC_MODE==2

	if( !gc_ctor_nest ){

#if DEBUG_GC
		int ms=gc_micros();
#endif
		if( gc_new_bytes+gc_ext_new_bytes>(CFG_CPP_GC_TRIGGER) ){
			atomic_sub( &gc_ext_new_bytes,gc_ext_new_bytes );
			gc_collect_all();
			gc_new_bytes=0;
		}else{
			gc_mark_queued( (long long)(gc_new_bytes)*(gc_alloced_bytes-gc_new_bytes)/(CFG_CPP_GC_TRIGGER)+gc_new_bytes );
		}

#if DEBUG_GC
		ms=gc_micros()-ms;
		if( ms>=100 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
#endif
	}
	
#endif

	gc_flush_free( size );

	gc_object *p;
	if( size<64 && (p=gc_cache[size>>3]) ){
		gc_cache[size>>3]=p->succ;
	}else{
		p=(gc_object*)malloc( size );
	}
	
	p->flags=size|gc_markbit;
	GC_INSERT_NODE( p,&gc_unmarked_list );

	gc_alloced_bytes+=size;
	if( gc_alloced_bytes>gc_max_alloced_bytes ) gc_max_alloced_bytes=gc_alloced_bytes;
	
#if CFG_CPP_GC_MODE==2
	*gc_locals_sp++=p;
#endif

	return p;
}

void gc_object_free( gc_object *p ){

	int size=p->flags & ~7;
	gc_free_bytes-=size;
	
	if( size<64 ){
		p->succ=gc_cache[size>>3];
		gc_cache[size>>3]=p;
	}else{
		free( p );
	}
}

template<class T> void gc_mark( T *t ){

	gc_object *p=dynamic_cast<gc_object*>(t);
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_marked_list );
		gc_marked_bytes+=(p->flags & ~7);
		p->mark();
	}
}

template<class T> void gc_mark_q( T *t ){

	gc_object *p=dynamic_cast<gc_object*>(t);
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_queued_list );
	}
}

template<class T> T *gc_retain( T *t ){
#if CFG_CPP_GC_MODE==2
	*gc_locals_sp++=dynamic_cast<gc_object*>( t );
#endif	
	return t;
}

template<class T,class V> void gc_assign( T *&lhs,V *rhs ){
	gc_object *p=dynamic_cast<gc_object*>(rhs);
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_queued_list );
	}
	lhs=rhs;
}

void gc_mark_locals(){
	for( gc_object **pp=gc_locals;pp!=gc_locals_sp;++pp ){
		gc_object *p=*pp;
		if( p && (p->flags & 3)==gc_markbit ){
			p->flags^=1;
			GC_REMOVE_NODE( p );
			GC_INSERT_NODE( p,&gc_marked_list );
			gc_marked_bytes+=(p->flags & ~7);
			p->mark();
		}
	}
}

void gc_mark_queued( int n ){
	while( gc_marked_bytes<n && !GC_LIST_IS_EMPTY( gc_queued_list ) ){
		gc_object *p=gc_queued_list.succ;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_marked_list );
		gc_marked_bytes+=(p->flags & ~7);
		p->mark();
	}
}

//returns reclaimed bytes
int gc_sweep(){

	int reclaimed_bytes=gc_alloced_bytes-gc_marked_bytes;
	
	if( reclaimed_bytes ){
	
		//append unmarked list to end of free list
		gc_object *head=gc_unmarked_list.succ;
		gc_object *tail=gc_unmarked_list.pred;
		gc_object *succ=&gc_free_list;
		gc_object *pred=succ->pred;
		head->pred=pred;
		tail->succ=succ;
		pred->succ=head;
		succ->pred=tail;
		
		gc_free_bytes+=reclaimed_bytes;
	}
	
	//move marked to unmarked.
	gc_unmarked_list=gc_marked_list;
	gc_unmarked_list.succ->pred=gc_unmarked_list.pred->succ=&gc_unmarked_list;
	
	//clear marked.
	GC_CLEAR_LIST( gc_marked_list );
	
	//adjust sizes
	gc_alloced_bytes=gc_marked_bytes;
	gc_marked_bytes=0;
	gc_markbit^=1;
	
	return reclaimed_bytes;
}

void gc_collect_all(){

//	printf( "Mark locals\n" );fflush( stdout );
	gc_mark_locals();

//	printf( "Mark queued\n" );fflush( stdout );
	gc_mark_queued( 0x7fffffff );

//	printf( "sweep\n" );fflush( stdout );	
	gc_sweep();

//	printf( "Mark roots\n" );fflush( stdout );
	gc_mark_roots();
}

void gc_collect(){

	if( gc_locals_sp!=gc_locals ){
//		printf( "GC_LOCALS error\n" );fflush( stdout );
		gc_locals_sp=gc_locals;
	}
	
#if CFG_CPP_GC_MODE==1

#if DEBUG_GC
	int ms=gc_micros();
#endif

	if( gc_new_bytes+gc_ext_new_bytes>(CFG_CPP_GC_TRIGGER) ){
		atomic_sub( &gc_ext_new_bytes,gc_ext_new_bytes );
		gc_collect_all();
		gc_new_bytes=0;
	}else{
		gc_mark_queued( (long long)(gc_new_bytes)*(gc_alloced_bytes-gc_new_bytes)/(CFG_CPP_GC_TRIGGER)+gc_new_bytes );
	}

#if DEBUG_GC
	ms=gc_micros()-ms;
	if( ms>=100 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
//	if( ms>=0 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
#endif

#endif

}

// ***** Array *****

template<class T> T *t_memcpy( T *dst,const T *src,int n ){
	memcpy( dst,src,n*sizeof(T) );
	return dst+n;
}

template<class T> T *t_memset( T *dst,int val,int n ){
	memset( dst,val,n*sizeof(T) );
	return dst+n;
}

template<class T> int t_memcmp( const T *x,const T *y,int n ){
	return memcmp( x,y,n*sizeof(T) );
}

template<class T> int t_strlen( const T *p ){
	const T *q=p++;
	while( *q++ ){}
	return q-p;
}

template<class T> T *t_create( int n,T *p ){
	t_memset( p,0,n );
	return p+n;
}

template<class T> T *t_create( int n,T *p,const T *q ){
	t_memcpy( p,q,n );
	return p+n;
}

template<class T> void t_destroy( int n,T *p ){
}

template<class T> void gc_mark_elements( int n,T *p ){
}

template<class T> void gc_mark_elements( int n,T **p ){
	for( int i=0;i<n;++i ) gc_mark( p[i] );
}

template<class T> class Array{
public:
	Array():rep( &nullRep ){
	}

	//Uses default...
//	Array( const Array<T> &t )...
	
	Array( int length ):rep( Rep::alloc( length ) ){
		t_create( rep->length,rep->data );
	}
	
	Array( const T *p,int length ):rep( Rep::alloc(length) ){
		t_create( rep->length,rep->data,p );
	}
	
	~Array(){
	}

	//Uses default...
//	Array &operator=( const Array &t )...
	
	int Length()const{ 
		return rep->length; 
	}
	
	T &At( int index ){
		if( index<0 || index>=rep->length ) dbg_error( "Array index out of range" );
		return rep->data[index]; 
	}
	
	const T &At( int index )const{
		if( index<0 || index>=rep->length ) dbg_error( "Array index out of range" );
		return rep->data[index]; 
	}
	
	T &operator[]( int index ){
		return rep->data[index]; 
	}

	const T &operator[]( int index )const{
		return rep->data[index]; 
	}
	
	Array Slice( int from,int term )const{
		int len=rep->length;
		if( from<0 ){ 
			from+=len;
			if( from<0 ) from=0;
		}else if( from>len ){
			from=len;
		}
		if( term<0 ){
			term+=len;
		}else if( term>len ){
			term=len;
		}
		if( term<=from ) return Array();
		return Array( rep->data+from,term-from );
	}

	Array Slice( int from )const{
		return Slice( from,rep->length );
	}
	
	Array Resize( int newlen )const{
		if( newlen<=0 ) return Array();
		int n=rep->length;
		if( newlen<n ) n=newlen;
		Rep *p=Rep::alloc( newlen );
		T *q=p->data;
		q=t_create( n,q,rep->data );
		q=t_create( (newlen-n),q );
		return Array( p );
	}
	
private:
	struct Rep : public gc_object{
		int length;
		T data[0];
		
		Rep():length(0){
			flags=3;
		}
		
		Rep( int length ):length(length){
		}
		
		~Rep(){
			t_destroy( length,data );
		}
		
		void mark(){
			gc_mark_elements( length,data );
		}
		
		static Rep *alloc( int length ){
			if( !length ) return &nullRep;
			void *p=gc_object_alloc( sizeof(Rep)+length*sizeof(T) );
			return ::new(p) Rep( length );
		}
		
	};
	Rep *rep;
	
	static Rep nullRep;
	
	template<class C> friend void gc_mark( Array<C> t );
	template<class C> friend void gc_mark_q( Array<C> t );
	template<class C> friend Array<C> gc_retain( Array<C> t );
	template<class C> friend void gc_assign( Array<C> &lhs,Array<C> rhs );
	template<class C> friend void gc_mark_elements( int n,Array<C> *p );
	
	Array( Rep *rep ):rep(rep){
	}
};

template<class T> typename Array<T>::Rep Array<T>::nullRep;

template<class T> Array<T> *t_create( int n,Array<T> *p ){
	for( int i=0;i<n;++i ) *p++=Array<T>();
	return p;
}

template<class T> Array<T> *t_create( int n,Array<T> *p,const Array<T> *q ){
	for( int i=0;i<n;++i ) *p++=*q++;
	return p;
}

template<class T> void gc_mark( Array<T> t ){
	gc_mark( t.rep );
}

template<class T> void gc_mark_q( Array<T> t ){
	gc_mark_q( t.rep );
}

template<class T> Array<T> gc_retain( Array<T> t ){
#if CFG_CPP_GC_MODE==2
	gc_retain( t.rep );
#endif
	return t;
}

template<class T> void gc_assign( Array<T> &lhs,Array<T> rhs ){
	gc_mark( rhs.rep );
	lhs=rhs;
}

template<class T> void gc_mark_elements( int n,Array<T> *p ){
	for( int i=0;i<n;++i ) gc_mark( p[i].rep );
}
		
// ***** String *****

static const char *_str_load_err;

class String{
public:
	String():rep( &nullRep ){
	}
	
	String( const String &t ):rep( t.rep ){
		rep->retain();
	}

	String( int n ){
		char buf[256];
		sprintf_s( buf,"%i",n );
		rep=Rep::alloc( t_strlen(buf) );
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
	}
	
	String( Float n ){
		char buf[256];
		
		//would rather use snprintf, but it's doing weird things in MingW.
		//
		sprintf_s( buf,"%.17lg",n );
		//
		char *p;
		for( p=buf;*p;++p ){
			if( *p=='.' || *p=='e' ) break;
		}
		if( !*p ){
			*p++='.';
			*p++='0';
			*p=0;
		}

		rep=Rep::alloc( t_strlen(buf) );
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
	}

	String( Char ch,int length ):rep( Rep::alloc(length) ){
		for( int i=0;i<length;++i ) rep->data[i]=ch;
	}

	String( const Char *p ):rep( Rep::alloc(t_strlen(p)) ){
		t_memcpy( rep->data,p,rep->length );
	}

	String( const Char *p,int length ):rep( Rep::alloc(length) ){
		t_memcpy( rep->data,p,rep->length );
	}
	
#if __OBJC__	
	String( NSString *nsstr ):rep( Rep::alloc([nsstr length]) ){
		unichar *buf=(unichar*)malloc( rep->length * sizeof(unichar) );
		[nsstr getCharacters:buf range:NSMakeRange(0,rep->length)];
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
		free( buf );
	}
#endif

#if __cplusplus_winrt
	String( Platform::String ^str ):rep( Rep::alloc(str->Length()) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=str->Data()[i];
	}
#endif

	~String(){
		rep->release();
	}
	
	template<class C> String( const C *p ):rep( Rep::alloc(t_strlen(p)) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=p[i];
	}
	
	template<class C> String( const C *p,int length ):rep( Rep::alloc(length) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=p[i];
	}
	
	String Copy()const{
		Rep *crep=Rep::alloc( rep->length );
		t_memcpy( crep->data,rep->data,rep->length );
		return String( crep );
	}
	
	int Length()const{
		return rep->length;
	}
	
	const Char *Data()const{
		return rep->data;
	}
	
	Char At( int index )const{
		if( index<0 || index>=rep->length ) dbg_error( "Character index out of range" );
		return rep->data[index]; 
	}
	
	Char operator[]( int index )const{
		return rep->data[index];
	}
	
	String &operator=( const String &t ){
		t.rep->retain();
		rep->release();
		rep=t.rep;
		return *this;
	}
	
	String &operator+=( const String &t ){
		return operator=( *this+t );
	}
	
	int Compare( const String &t )const{
		int n=rep->length<t.rep->length ? rep->length : t.rep->length;
		for( int i=0;i<n;++i ){
			if( int q=(int)(rep->data[i])-(int)(t.rep->data[i]) ) return q;
		}
		return rep->length-t.rep->length;
	}
	
	bool operator==( const String &t )const{
		return rep->length==t.rep->length && t_memcmp( rep->data,t.rep->data,rep->length )==0;
	}
	
	bool operator!=( const String &t )const{
		return rep->length!=t.rep->length || t_memcmp( rep->data,t.rep->data,rep->length )!=0;
	}
	
	bool operator<( const String &t )const{
		return Compare( t )<0;
	}
	
	bool operator<=( const String &t )const{
		return Compare( t )<=0;
	}
	
	bool operator>( const String &t )const{
		return Compare( t )>0;
	}
	
	bool operator>=( const String &t )const{
		return Compare( t )>=0;
	}
	
	String operator+( const String &t )const{
		if( !rep->length ) return t;
		if( !t.rep->length ) return *this;
		Rep *p=Rep::alloc( rep->length+t.rep->length );
		Char *q=p->data;
		q=t_memcpy( q,rep->data,rep->length );
		q=t_memcpy( q,t.rep->data,t.rep->length );
		return String( p );
	}
	
	int Find( String find,int start=0 )const{
		if( start<0 ) start=0;
		while( start+find.rep->length<=rep->length ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			++start;
		}
		return -1;
	}
	
	int FindLast( String find )const{
		int start=rep->length-find.rep->length;
		while( start>=0 ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			--start;
		}
		return -1;
	}
	
	int FindLast( String find,int start )const{
		if( start>rep->length-find.rep->length ) start=rep->length-find.rep->length;
		while( start>=0 ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			--start;
		}
		return -1;
	}
	
	String Trim()const{
		int i=0,i2=rep->length;
		while( i<i2 && rep->data[i]<=32 ) ++i;
		while( i2>i && rep->data[i2-1]<=32 ) --i2;
		if( i==0 && i2==rep->length ) return *this;
		return String( rep->data+i,i2-i );
	}

	Array<String> Split( String sep )const{
	
		if( !sep.rep->length ){
			Array<String> bits( rep->length );
			for( int i=0;i<rep->length;++i ){
				bits[i]=String( (Char)(*this)[i],1 );
			}
			return bits;
		}
		
		int i=0,i2,n=1;
		while( (i2=Find( sep,i ))!=-1 ){
			++n;
			i=i2+sep.rep->length;
		}
		Array<String> bits( n );
		if( n==1 ){
			bits[0]=*this;
			return bits;
		}
		i=0;n=0;
		while( (i2=Find( sep,i ))!=-1 ){
			bits[n++]=Slice( i,i2 );
			i=i2+sep.rep->length;
		}
		bits[n]=Slice( i );
		return bits;
	}

	String Join( Array<String> bits )const{
		if( bits.Length()==0 ) return String();
		if( bits.Length()==1 ) return bits[0];
		int newlen=rep->length * (bits.Length()-1);
		for( int i=0;i<bits.Length();++i ){
			newlen+=bits[i].rep->length;
		}
		Rep *p=Rep::alloc( newlen );
		Char *q=p->data;
		q=t_memcpy( q,bits[0].rep->data,bits[0].rep->length );
		for( int i=1;i<bits.Length();++i ){
			q=t_memcpy( q,rep->data,rep->length );
			q=t_memcpy( q,bits[i].rep->data,bits[i].rep->length );
		}
		return String( p );
	}

	String Replace( String find,String repl )const{
		int i=0,i2,newlen=0;
		while( (i2=Find( find,i ))!=-1 ){
			newlen+=(i2-i)+repl.rep->length;
			i=i2+find.rep->length;
		}
		if( !i ) return *this;
		newlen+=rep->length-i;
		Rep *p=Rep::alloc( newlen );
		Char *q=p->data;
		i=0;
		while( (i2=Find( find,i ))!=-1 ){
			q=t_memcpy( q,rep->data+i,i2-i );
			q=t_memcpy( q,repl.rep->data,repl.rep->length );
			i=i2+find.rep->length;
		}
		q=t_memcpy( q,rep->data+i,rep->length-i );
		return String( p );
	}

	String ToLower()const{
		for( int i=0;i<rep->length;++i ){
			Char t=towlower( rep->data[i] );
			if( t==rep->data[i] ) continue;
			Rep *p=Rep::alloc( rep->length );
			Char *q=p->data;
			t_memcpy( q,rep->data,i );
			for( q[i++]=t;i<rep->length;++i ){
				q[i]=towlower( rep->data[i] );
			}
			return String( p );
		}
		return *this;
	}

	String ToUpper()const{
		for( int i=0;i<rep->length;++i ){
			Char t=towupper( rep->data[i] );
			if( t==rep->data[i] ) continue;
			Rep *p=Rep::alloc( rep->length );
			Char *q=p->data;
			t_memcpy( q,rep->data,i );
			for( q[i++]=t;i<rep->length;++i ){
				q[i]=towupper( rep->data[i] );
			}
			return String( p );
		}
		return *this;
	}
	
	bool Contains( String sub )const{
		return Find( sub )!=-1;
	}

	bool StartsWith( String sub )const{
		return sub.rep->length<=rep->length && !t_memcmp( rep->data,sub.rep->data,sub.rep->length );
	}

	bool EndsWith( String sub )const{
		return sub.rep->length<=rep->length && !t_memcmp( rep->data+rep->length-sub.rep->length,sub.rep->data,sub.rep->length );
	}
	
	String Slice( int from,int term )const{
		int len=rep->length;
		if( from<0 ){
			from+=len;
			if( from<0 ) from=0;
		}else if( from>len ){
			from=len;
		}
		if( term<0 ){
			term+=len;
		}else if( term>len ){
			term=len;
		}
		if( term<from ) return String();
		if( from==0 && term==len ) return *this;
		return String( rep->data+from,term-from );
	}

	String Slice( int from )const{
		return Slice( from,rep->length );
	}
	
	Array<int> ToChars()const{
		Array<int> chars( rep->length );
		for( int i=0;i<rep->length;++i ) chars[i]=rep->data[i];
		return chars;
	}
	
	int ToInt()const{
		char buf[64];
		return atoi( ToCString<char>( buf,sizeof(buf) ) );
	}
	
	Float ToFloat()const{
		char buf[256];
		return atof( ToCString<char>( buf,sizeof(buf) ) );
	}

	template<class C> class CString{
		struct Rep{
			int refs;
			C data[1];
		};
		Rep *_rep;
		static Rep _nul;
	public:
		template<class T> CString( const T *data,int length ){
			_rep=(Rep*)malloc( length*sizeof(C)+sizeof(Rep) );
			_rep->refs=1;
			_rep->data[length]=0;
			for( int i=0;i<length;++i ){
				_rep->data[i]=(C)data[i];
			}
		}
		CString():_rep( new Rep ){
			_rep->refs=1;
		}
		CString( const CString &c ):_rep(c._rep){
			++_rep->refs;
		}
		~CString(){
			if( !--_rep->refs ) free( _rep );
		}
		CString &operator=( const CString &c ){
			++c._rep->refs;
			if( !--_rep->refs ) free( _rep );
			_rep=c._rep;
			return *this;
		}
		operator const C*()const{ 
			return _rep->data;
		}
	};
	
	template<class C> CString<C> ToCString()const{
		return CString<C>( rep->data,rep->length );
	}

	template<class C> C *ToCString( C *p,int length )const{
		if( --length>rep->length ) length=rep->length;
		for( int i=0;i<length;++i ) p[i]=rep->data[i];
		p[length]=0;
		return p;
	}
	
#if __OBJC__	
	NSString *ToNSString()const{
		return [NSString stringWithCharacters:ToCString<unichar>() length:rep->length];
	}
#endif

#if __cplusplus_winrt
	Platform::String ^ToWinRTString()const{
		return ref new Platform::String( rep->data,rep->length );
	}
#endif

	bool Save( FILE *fp ){
		std::vector<unsigned char> buf;
		Save( buf );
		return buf.size() ? fwrite( &buf[0],1,buf.size(),fp )==buf.size() : true;
	}
	
	void Save( std::vector<unsigned char> &buf ){
	
		Char *p=rep->data;
		Char *e=p+rep->length;
		
		while( p<e ){
			Char c=*p++;
			if( c<0x80 ){
				buf.push_back( c );
			}else if( c<0x800 ){
				buf.push_back( 0xc0 | (c>>6) );
				buf.push_back( 0x80 | (c & 0x3f) );
			}else{
				buf.push_back( 0xe0 | (c>>12) );
				buf.push_back( 0x80 | ((c>>6) & 0x3f) );
				buf.push_back( 0x80 | (c & 0x3f) );
			}
		}
	}
	
	static String FromChars( Array<int> chars ){
		int n=chars.Length();
		Rep *p=Rep::alloc( n );
		for( int i=0;i<n;++i ){
			p->data[i]=chars[i];
		}
		return String( p );
	}

	static String Load( FILE *fp ){
		unsigned char tmp[4096];
		std::vector<unsigned char> buf;
		for(;;){
			int n=fread( tmp,1,4096,fp );
			if( n>0 ) buf.insert( buf.end(),tmp,tmp+n );
			if( n!=4096 ) break;
		}
		return buf.size() ? String::Load( &buf[0],buf.size() ) : String();
	}
	
	static String Load( unsigned char *p,int n ){
	
		_str_load_err=0;
		
		unsigned char *e=p+n;
		std::vector<Char> chars;
		
		int t0=n>0 ? p[0] : -1;
		int t1=n>1 ? p[1] : -1;

		if( t0==0xfe && t1==0xff ){
			p+=2;
			while( p<e-1 ){
				int c=*p++;
				chars.push_back( (c<<8)|*p++ );
			}
		}else if( t0==0xff && t1==0xfe ){
			p+=2;
			while( p<e-1 ){
				int c=*p++;
				chars.push_back( (*p++<<8)|c );
			}
		}else{
			int t2=n>2 ? p[2] : -1;
			if( t0==0xef && t1==0xbb && t2==0xbf ) p+=3;
			unsigned char *q=p;
			bool fail=false;
			while( p<e ){
				unsigned int c=*p++;
				if( c & 0x80 ){
					if( (c & 0xe0)==0xc0 ){
						if( p>=e || (p[0] & 0xc0)!=0x80 ){
							fail=true;
							break;
						}
						c=((c & 0x1f)<<6) | (p[0] & 0x3f);
						p+=1;
					}else if( (c & 0xf0)==0xe0 ){
						if( p+1>=e || (p[0] & 0xc0)!=0x80 || (p[1] & 0xc0)!=0x80 ){
							fail=true;
							break;
						}
						c=((c & 0x0f)<<12) | ((p[0] & 0x3f)<<6) | (p[1] & 0x3f);
						p+=2;
					}else{
						fail=true;
						break;
					}
				}
				chars.push_back( c );
			}
			if( fail ){
				_str_load_err="Invalid UTF-8";
				return String( q,n );
			}
		}
		return chars.size() ? String( &chars[0],chars.size() ) : String();
	}

private:
	
	struct Rep{
		int refs;
		int length;
		Char data[0];
		
		Rep():refs(1),length(0){
		}
		
		Rep( int length ):refs(1),length(length){
		}
		
		void retain(){
//			atomic_add( &refs,1 );
			++refs;
		}
		
		void release(){
//			if( atomic_sub( &refs,1 )>1 || this==&nullRep ) return;
			if( --refs || this==&nullRep ) return;
			free( this );
		}

		static Rep *alloc( int length ){
			if( !length ) return &nullRep;
			void *p=malloc( sizeof(Rep)+length*sizeof(Char) );
			return new(p) Rep( length );
		}
	};
	Rep *rep;
	
	static Rep nullRep;
	
	String( Rep *rep ):rep(rep){
	}
};

String::Rep String::nullRep;

String *t_create( int n,String *p ){
	for( int i=0;i<n;++i ) new( &p[i] ) String();
	return p+n;
}

String *t_create( int n,String *p,const String *q ){
	for( int i=0;i<n;++i ) new( &p[i] ) String( q[i] );
	return p+n;
}

void t_destroy( int n,String *p ){
	for( int i=0;i<n;++i ) p[i].~String();
}

// ***** Object *****

String dbg_stacktrace();

class Object : public gc_object{
public:
	virtual bool Equals( Object *obj ){
		return this==obj;
	}
	
	virtual int Compare( Object *obj ){
		return (char*)this-(char*)obj;
	}
	
	virtual String debug(){
		return "+Object\n";
	}
};

class ThrowableObject : public Object{
#ifndef NDEBUG
public:
	String stackTrace;
	ThrowableObject():stackTrace( dbg_stacktrace() ){}
#endif
};

struct gc_interface{
	virtual ~gc_interface(){}
};

//***** Debugger *****

//#define Error bbError
//#define Print bbPrint

int bbPrint( String t );

#define dbg_stream stderr

#if _MSC_VER
#define dbg_typeof decltype
#else
#define dbg_typeof __typeof__
#endif 

struct dbg_func;
struct dbg_var_type;

static int dbg_suspend;
static int dbg_stepmode;

const char *dbg_info;
String dbg_exstack;

static void *dbg_var_buf[65536*3];
static void **dbg_var_ptr=dbg_var_buf;

static dbg_func *dbg_func_buf[1024];
static dbg_func **dbg_func_ptr=dbg_func_buf;

String dbg_type( bool *p ){
	return "Bool";
}

String dbg_type( int *p ){
	return "Int";
}

String dbg_type( Float *p ){
	return "Float";
}

String dbg_type( String *p ){
	return "String";
}

template<class T> String dbg_type( T *p ){
	return "Object";
}

template<class T> String dbg_type( Array<T> *p ){
	return dbg_type( &(*p)[0] )+"[]";
}

String dbg_value( bool *p ){
	return *p ? "True" : "False";
}

String dbg_value( int *p ){
	return String( *p );
}

String dbg_value( Float *p ){
	return String( *p );
}

String dbg_value( String *p ){
	String t=*p;
	if( t.Length()>100 ) t=t.Slice( 0,100 )+"...";
	t=t.Replace( "\"","~q" );
	t=t.Replace( "\t","~t" );
	t=t.Replace( "\n","~n" );
	t=t.Replace( "\r","~r" );
	return String("\"")+t+"\"";
}

template<class T> String dbg_value( T *t ){
	Object *p=dynamic_cast<Object*>( *t );
	char buf[64];
	sprintf_s( buf,"%p",p );
	return String("@") + (buf[0]=='0' && buf[1]=='x' ? buf+2 : buf );
}

template<class T> String dbg_value( Array<T> *p ){
	String t="[";
	int n=(*p).Length();
	for( int i=0;i<n;++i ){
		if( i ) t+=",";
		t+=dbg_value( &(*p)[i] );
	}
	return t+"]";
}

template<class T> String dbg_decl( const char *id,T *ptr ){
	return String( id )+":"+dbg_type(ptr)+"="+dbg_value(ptr)+"\n";
}

struct dbg_var_type{
	virtual String type( void *p )=0;
	virtual String value( void *p )=0;
};

template<class T> struct dbg_var_type_t : public dbg_var_type{

	String type( void *p ){
		return dbg_type( (T*)p );
	}
	
	String value( void *p ){
		return dbg_value( (T*)p );
	}
	
	static dbg_var_type_t<T> info;
};
template<class T> dbg_var_type_t<T> dbg_var_type_t<T>::info;

struct dbg_blk{
	void **var_ptr;
	
	dbg_blk():var_ptr(dbg_var_ptr){
		if( dbg_stepmode=='l' ) --dbg_suspend;
	}
	
	~dbg_blk(){
		if( dbg_stepmode=='l' ) ++dbg_suspend;
		dbg_var_ptr=var_ptr;
	}
};

struct dbg_func : public dbg_blk{
	const char *id;
	const char *info;

	dbg_func( const char *p ):id(p),info(dbg_info){
		*dbg_func_ptr++=this;
		if( dbg_stepmode=='s' ) --dbg_suspend;
	}
	
	~dbg_func(){
		if( dbg_stepmode=='s' ) ++dbg_suspend;
		--dbg_func_ptr;
		dbg_info=info;
	}
};

int dbg_print( String t ){
	static char *buf;
	static int len;
	int n=t.Length();
	if( n+100>len ){
		len=n+100;
		free( buf );
		buf=(char*)malloc( len );
	}
	buf[n]='\n';
	for( int i=0;i<n;++i ) buf[i]=t[i];
	fwrite( buf,n+1,1,dbg_stream );
	fflush( dbg_stream );
	return 0;
}

void dbg_callstack(){

	void **var_ptr=dbg_var_buf;
	dbg_func **func_ptr=dbg_func_buf;
	
	while( var_ptr!=dbg_var_ptr ){
		while( func_ptr!=dbg_func_ptr && var_ptr==(*func_ptr)->var_ptr ){
			const char *id=(*func_ptr++)->id;
			const char *info=func_ptr!=dbg_func_ptr ? (*func_ptr)->info : dbg_info;
			fprintf( dbg_stream,"+%s;%s\n",id,info );
		}
		void *vp=*var_ptr++;
		const char *nm=(const char*)*var_ptr++;
		dbg_var_type *ty=(dbg_var_type*)*var_ptr++;
		dbg_print( String(nm)+":"+ty->type(vp)+"="+ty->value(vp) );
	}
	while( func_ptr!=dbg_func_ptr ){
		const char *id=(*func_ptr++)->id;
		const char *info=func_ptr!=dbg_func_ptr ? (*func_ptr)->info : dbg_info;
		fprintf( dbg_stream,"+%s;%s\n",id,info );
	}
}

String dbg_stacktrace(){
	if( !dbg_info || !dbg_info[0] ) return "";
	String str=String( dbg_info )+"\n";
	dbg_func **func_ptr=dbg_func_ptr;
	if( func_ptr==dbg_func_buf ) return str;
	while( --func_ptr!=dbg_func_buf ){
		str+=String( (*func_ptr)->info )+"\n";
	}
	return str;
}

void dbg_throw( const char *err ){
	dbg_exstack=dbg_stacktrace();
	throw err;
}

void dbg_stop(){

#if TARGET_OS_IPHONE
	dbg_throw( "STOP" );
#endif

	fprintf( dbg_stream,"{{~~%s~~}}\n",dbg_info );
	dbg_callstack();
	dbg_print( "" );
	
	for(;;){

		char buf[256];
		char *e=fgets( buf,256,stdin );
		if( !e ) exit( -1 );
		
		e=strchr( buf,'\n' );
		if( !e ) exit( -1 );
		
		*e=0;
		
		Object *p;
		
		switch( buf[0] ){
		case '?':
			break;
		case 'r':	//run
			dbg_suspend=0;		
			dbg_stepmode=0;
			return;
		case 's':	//step
			dbg_suspend=1;
			dbg_stepmode='s';
			return;
		case 'e':	//enter func
			dbg_suspend=1;
			dbg_stepmode='e';
			return;
		case 'l':	//leave block
			dbg_suspend=0;
			dbg_stepmode='l';
			return;
		case '@':	//dump object
			p=0;
			sscanf_s( buf+1,"%p",&p );
			if( p ){
				dbg_print( p->debug() );
			}else{
				dbg_print( "" );
			}
			break;
		case 'q':	//quit!
			exit( 0 );
			break;			
		default:
			printf( "????? %s ?????",buf );fflush( stdout );
			exit( -1 );
		}
	}
}

void dbg_error( const char *err ){

#if TARGET_OS_IPHONE
	dbg_throw( err );
#endif

	for(;;){
		bbPrint( String("Monkey Runtime Error : ")+err );
		bbPrint( dbg_stacktrace() );
		dbg_stop();
	}
}

#define DBG_INFO(X) dbg_info=(X);if( dbg_suspend>0 ) dbg_stop();

#define DBG_ENTER(P) dbg_func _dbg_func(P);

#define DBG_BLOCK() dbg_blk _dbg_blk;

#define DBG_GLOBAL( ID,NAME )	//TODO!

#define DBG_LOCAL( ID,NAME )\
*dbg_var_ptr++=&ID;\
*dbg_var_ptr++=(void*)NAME;\
*dbg_var_ptr++=&dbg_var_type_t<dbg_typeof(ID)>::info;

//**** main ****

int argc;
const char **argv;

Float D2R=0.017453292519943295f;
Float R2D=57.29577951308232f;

int bbPrint( String t ){

	static std::vector<unsigned char> buf;
	buf.clear();
	t.Save( buf );
	buf.push_back( '\n' );
	buf.push_back( 0 );
	
#if __cplusplus_winrt	//winrt?

#if CFG_WINRT_PRINT_ENABLED
	OutputDebugStringA( (const char*)&buf[0] );
#endif

#elif _WIN32			//windows?

	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );

#elif __APPLE__			//macos/ios?

	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );
	
#elif __linux			//linux?

#if CFG_ANDROID_NDK_PRINT_ENABLED
	LOGI( (const char*)&buf[0] );
#else
	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );
#endif

#endif

	return 0;
}

class BBExitApp{
};

int bbError( String err ){
	if( !err.Length() ){
#if __cplusplus_winrt
		throw BBExitApp();
#else
		exit( 0 );
#endif
	}
	dbg_error( err.ToCString<char>() );
	return 0;
}

int bbDebugLog( String t ){
	bbPrint( t );
	return 0;
}

int bbDebugStop(){
	dbg_stop();
	return 0;
}

int bbInit();
int bbMain();

#if _MSC_VER

static void _cdecl seTranslator( unsigned int ex,EXCEPTION_POINTERS *p ){

	switch( ex ){
	case EXCEPTION_ACCESS_VIOLATION:dbg_error( "Memory access violation" );
	case EXCEPTION_ILLEGAL_INSTRUCTION:dbg_error( "Illegal instruction" );
	case EXCEPTION_INT_DIVIDE_BY_ZERO:dbg_error( "Integer divide by zero" );
	case EXCEPTION_STACK_OVERFLOW:dbg_error( "Stack overflow" );
	}
	dbg_error( "Unknown exception" );
}

#else

void sighandler( int sig  ){
	switch( sig ){
	case SIGSEGV:dbg_error( "Memory access violation" );
	case SIGILL:dbg_error( "Illegal instruction" );
	case SIGFPE:dbg_error( "Floating point exception" );
#if !_WIN32
	case SIGBUS:dbg_error( "Bus error" );
#endif	
	}
	dbg_error( "Unknown signal" );
}

#endif

//entry point call by target main()...
//
int bb_std_main( int argc,const char **argv ){

	::argc=argc;
	::argv=argv;
	
#if _MSC_VER

	_set_se_translator( seTranslator );

#else
	
	signal( SIGSEGV,sighandler );
	signal( SIGILL,sighandler );
	signal( SIGFPE,sighandler );
#if !_WIN32
	signal( SIGBUS,sighandler );
#endif

#endif

	if( !setlocale( LC_CTYPE,"en_US.UTF-8" ) ){
		setlocale( LC_CTYPE,"" );
	}

	gc_init1();

	bbInit();
	
	gc_init2();

	bbMain();

	return 0;
}


//***** game.h *****

struct BBGameEvent{
	enum{
		None=0,
		KeyDown=1,KeyUp=2,KeyChar=3,
		MouseDown=4,MouseUp=5,MouseMove=6,
		TouchDown=7,TouchUp=8,TouchMove=9,
		MotionAccel=10
	};
};

class BBGameDelegate : public Object{
public:
	virtual void StartGame(){}
	virtual void SuspendGame(){}
	virtual void ResumeGame(){}
	virtual void UpdateGame(){}
	virtual void RenderGame(){}
	virtual void KeyEvent( int event,int data ){}
	virtual void MouseEvent( int event,int data,Float x,Float y ){}
	virtual void TouchEvent( int event,int data,Float x,Float y ){}
	virtual void MotionEvent( int event,int data,Float x,Float y,Float z ){}
	virtual void DiscardGraphics(){}
};

struct BBDisplayMode : public Object{
	int width;
	int height;
	int format;
	int hertz;
	int flags;
	BBDisplayMode( int width=0,int height=0,int format=0,int hertz=0,int flags=0 ):width(width),height(height),format(format),hertz(hertz),flags(flags){}
};

class BBGame{
public:
	BBGame();
	virtual ~BBGame(){}
	
	// ***** Extern *****
	static BBGame *Game(){ return _game; }
	
	virtual void SetDelegate( BBGameDelegate *delegate );
	virtual BBGameDelegate *Delegate(){ return _delegate; }
	
	virtual void SetKeyboardEnabled( bool enabled );
	virtual bool KeyboardEnabled();
	
	virtual void SetUpdateRate( int updateRate );
	virtual int UpdateRate();
	
	virtual bool Started(){ return _started; }
	virtual bool Suspended(){ return _suspended; }
	
	virtual int Millisecs();
	virtual void GetDate( Array<int> date );
	virtual int SaveState( String state );
	virtual String LoadState();
	virtual String LoadString( String path );
	virtual bool PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons );
	virtual void OpenUrl( String url );
	virtual void SetMouseVisible( bool visible );
	
	virtual int GetDeviceWidth(){ return 0; }
	virtual int GetDeviceHeight(){ return 0; }
	virtual void SetDeviceWindow( int width,int height,int flags ){}
	virtual Array<BBDisplayMode*> GetDisplayModes(){ return Array<BBDisplayMode*>(); }
	virtual BBDisplayMode *GetDesktopMode(){ return 0; }
	virtual void SetSwapInterval( int interval ){}

	// ***** Native *****	
	virtual String PathToFilePath( String path );
	virtual FILE *OpenFile( String path,String mode );
	virtual unsigned char *LoadData( String path,int *plength );
	virtual unsigned char *LoadImageData( String path,int *width,int *height,int *depth ){ return 0; }
	virtual unsigned char *LoadAudioData( String path,int *length,int *channels,int *format,int *hertz ){ return 0; }
	
	//***** Internal *****
	virtual void Die( ThrowableObject *ex );
	virtual void gc_collect();
	virtual void StartGame();
	virtual void SuspendGame();
	virtual void ResumeGame();
	virtual void UpdateGame();
	virtual void RenderGame();
	virtual void KeyEvent( int ev,int data );
	virtual void MouseEvent( int ev,int data,float x,float y );
	virtual void TouchEvent( int ev,int data,float x,float y );
	virtual void MotionEvent( int ev,int data,float x,float y,float z );
	virtual void DiscardGraphics();
	
protected:

	static BBGame *_game;

	BBGameDelegate *_delegate;
	bool _keyboardEnabled;
	int _updateRate;
	bool _started;
	bool _suspended;
};

//***** game.cpp *****

BBGame *BBGame::_game;

BBGame::BBGame():
_delegate( 0 ),
_keyboardEnabled( false ),
_updateRate( 0 ),
_started( false ),
_suspended( false ){
	_game=this;
}

void BBGame::SetDelegate( BBGameDelegate *delegate ){
	_delegate=delegate;
}

void BBGame::SetKeyboardEnabled( bool enabled ){
	_keyboardEnabled=enabled;
}

bool BBGame::KeyboardEnabled(){
	return _keyboardEnabled;
}

void BBGame::SetUpdateRate( int updateRate ){
	_updateRate=updateRate;
}

int BBGame::UpdateRate(){
	return _updateRate;
}

int BBGame::Millisecs(){
	return 0;
}

void BBGame::GetDate( Array<int> date ){
	int n=date.Length();
	if( n>0 ){
		time_t t=time( 0 );
		
#if _MSC_VER
		struct tm tii;
		struct tm *ti=&tii;
		localtime_s( ti,&t );
#else
		struct tm *ti=localtime( &t );
#endif

		date[0]=ti->tm_year+1900;
		if( n>1 ){ 
			date[1]=ti->tm_mon+1;
			if( n>2 ){
				date[2]=ti->tm_mday;
				if( n>3 ){
					date[3]=ti->tm_hour;
					if( n>4 ){
						date[4]=ti->tm_min;
						if( n>5 ){
							date[5]=ti->tm_sec;
							if( n>6 ){
								date[6]=0;
							}
						}
					}
				}
			}
		}
	}
}

int BBGame::SaveState( String state ){
	if( FILE *f=OpenFile( "./.monkeystate","wb" ) ){
		bool ok=state.Save( f );
		fclose( f );
		return ok ? 0 : -2;
	}
	return -1;
}

String BBGame::LoadState(){
	if( FILE *f=OpenFile( "./.monkeystate","rb" ) ){
		String str=String::Load( f );
		fclose( f );
		return str;
	}
	return "";
}

String BBGame::LoadString( String path ){
	if( FILE *fp=OpenFile( path,"rb" ) ){
		String str=String::Load( fp );
		fclose( fp );
		return str;
	}
	return "";
}

bool BBGame::PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons ){
	return false;
}

void BBGame::OpenUrl( String url ){
}

void BBGame::SetMouseVisible( bool visible ){
}

//***** C++ Game *****

String BBGame::PathToFilePath( String path ){
	return path;
}

FILE *BBGame::OpenFile( String path,String mode ){
	path=PathToFilePath( path );
	if( path=="" ) return 0;
	
#if __cplusplus_winrt
	path=path.Replace( "/","\\" );
	FILE *f;
	if( _wfopen_s( &f,path.ToCString<wchar_t>(),mode.ToCString<wchar_t>() ) ) return 0;
	return f;
#elif _WIN32
	return _wfopen( path.ToCString<wchar_t>(),mode.ToCString<wchar_t>() );
#else
	return fopen( path.ToCString<char>(),mode.ToCString<char>() );
#endif
}

unsigned char *BBGame::LoadData( String path,int *plength ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;

	const int BUF_SZ=4096;
	std::vector<void*> tmps;
	int length=0;
	
	for(;;){
		void *p=malloc( BUF_SZ );
		int n=fread( p,1,BUF_SZ,f );
		tmps.push_back( p );
		length+=n;
		if( n!=BUF_SZ ) break;
	}
	fclose( f );
	
	unsigned char *data=(unsigned char*)malloc( length );
	unsigned char *p=data;
	
	int sz=length;
	for( int i=0;i<tmps.size();++i ){
		int n=sz>BUF_SZ ? BUF_SZ : sz;
		memcpy( p,tmps[i],n );
		free( tmps[i] );
		sz-=n;
		p+=n;
	}
	
	*plength=length;
	
	gc_ext_malloced( length );
	
	return data;
}

//***** INTERNAL *****

void BBGame::Die( ThrowableObject *ex ){
	bbPrint( "Monkey Runtime Error : Uncaught Monkey Exception" );
#ifndef NDEBUG
	bbPrint( ex->stackTrace );
#endif
	exit( -1 );
}

void BBGame::gc_collect(){
	gc_mark( _delegate );
	::gc_collect();
}

void BBGame::StartGame(){

	if( _started ) return;
	_started=true;
	
	try{
		_delegate->StartGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::SuspendGame(){

	if( !_started || _suspended ) return;
	_suspended=true;
	
	try{
		_delegate->SuspendGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::ResumeGame(){

	if( !_started || !_suspended ) return;
	_suspended=false;
	
	try{
		_delegate->ResumeGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::UpdateGame(){

	if( !_started || _suspended ) return;
	
	try{
		_delegate->UpdateGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::RenderGame(){

	if( !_started ) return;
	
	try{
		_delegate->RenderGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::KeyEvent( int ev,int data ){

	if( !_started ) return;
	
	try{
		_delegate->KeyEvent( ev,data );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::MouseEvent( int ev,int data,float x,float y ){

	if( !_started ) return;
	
	try{
		_delegate->MouseEvent( ev,data,x,y );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::TouchEvent( int ev,int data,float x,float y ){

	if( !_started ) return;
	
	try{
		_delegate->TouchEvent( ev,data,x,y );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::MotionEvent( int ev,int data,float x,float y,float z ){

	if( !_started ) return;
	
	try{
		_delegate->MotionEvent( ev,data,x,y,z );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::DiscardGraphics(){

	if( !_started ) return;
	
	try{
		_delegate->DiscardGraphics();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

// Stdcpp trans.system runtime.
//
// Placed into the public domain 24/02/2011.
// No warranty implied; use as your own risk.

#if _WIN32

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

typedef WCHAR OS_CHAR;
typedef struct _stat stat_t;

#define mkdir( X,Y ) _wmkdir( X )
#define rmdir _wrmdir
#define remove _wremove
#define rename _wrename
#define stat _wstat
#define _fopen _wfopen
#define putenv _wputenv
#define getenv _wgetenv
#define system _wsystem
#define chdir _wchdir
#define getcwd _wgetcwd
#define realpath(X,Y) _wfullpath( Y,X,PATH_MAX )	//Note: first args SWAPPED to be posix-like!
#define opendir _wopendir
#define readdir _wreaddir
#define closedir _wclosedir
#define DIR _WDIR
#define dirent _wdirent

#elif __APPLE__

typedef char OS_CHAR;
typedef struct stat stat_t;

#define _fopen fopen

#elif __linux

/*
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
*/

typedef char OS_CHAR;
typedef struct stat stat_t;

#define _fopen fopen

#endif

static String _appPath;
static Array<String> _appArgs;

static String::CString<char> C_STR( const String &t ){
	return t.ToCString<char>();
}

static String::CString<OS_CHAR> OS_STR( const String &t ){
	return t.ToCString<OS_CHAR>();
}

String HostOS(){
#if _WIN32
	return "winnt";
#elif __APPLE__
	return "macos";
#elif __linux
	return "linux";
#else
	return "";
#endif
}

String RealPath( String path ){
	std::vector<OS_CHAR> buf( PATH_MAX+1 );
	if( realpath( OS_STR( path ),&buf[0] ) ){}
	buf[buf.size()-1]=0;
	for( int i=0;i<PATH_MAX && buf[i];++i ){
		if( buf[i]=='\\' ) buf[i]='/';
		
	}
	return String( &buf[0] );
}

String AppPath(){

	if( _appPath.Length() ) return _appPath;
	
#if _WIN32

	OS_CHAR buf[PATH_MAX+1];
	GetModuleFileNameW( GetModuleHandleW(0),buf,PATH_MAX );
	buf[PATH_MAX]=0;
	_appPath=String( buf );
	
#elif __APPLE__

	char buf[PATH_MAX];
	uint32_t size=sizeof( buf );
	_NSGetExecutablePath( buf,&size );
	buf[PATH_MAX-1]=0;
	_appPath=String( buf );
	
#elif __linux

	char lnk[PATH_MAX],buf[PATH_MAX];
	pid_t pid=getpid();
	sprintf( lnk,"/proc/%i/exe",pid );
	int i=readlink( lnk,buf,PATH_MAX );
	if( i>0 && i<PATH_MAX ){
		buf[i]=0;
		_appPath=String( buf );
	}

#endif

	_appPath=RealPath( _appPath );
	return _appPath;
}

Array<String> AppArgs(){
	if( _appArgs.Length() ) return _appArgs;
	_appArgs=Array<String>( argc );
	for( int i=0;i<argc;++i ){
		_appArgs[i]=String( argv[i] );
	}
	return _appArgs;
}
	
int FileType( String path ){
	stat_t st;
	if( stat( OS_STR(path),&st ) ) return 0;
	switch( st.st_mode & S_IFMT ){
	case S_IFREG : return 1;
	case S_IFDIR : return 2;
	}
	return 0;
}

int FileSize( String path ){
	stat_t st;
	if( stat( OS_STR(path),&st ) ) return -1;
	return st.st_size;
}

int FileTime( String path ){
	stat_t st;
	if( stat( OS_STR(path),&st ) ) return -1;
	return st.st_mtime;
}

String LoadString( String path ){
	if( FILE *fp=_fopen( OS_STR(path),OS_STR("rb") ) ){
		String str=String::Load( fp );
		if( _str_load_err ){
			bbPrint( String( _str_load_err )+" in file: "+path );
		}
		fclose( fp );
		return str;
	}
	return "";
}
	
int SaveString( String str,String path ){
	if( FILE *fp=_fopen( OS_STR(path),OS_STR("wb") ) ){
		bool ok=str.Save( fp );
		fclose( fp );
		return ok ? 0 : -2;
	}else{
//		printf( "FOPEN 'wb' for SaveString '%s' failed\n",C_STR( path ) );
		fflush( stdout );
	}
	return -1;
}

Array<String> LoadDir( String path ){
	std::vector<String> files;
	
#if _WIN32

	WIN32_FIND_DATAW filedata;
	HANDLE handle=FindFirstFileW( OS_STR(path+"/*"),&filedata );
	if( handle!=INVALID_HANDLE_VALUE ){
		do{
			String f=filedata.cFileName;
			if( f=="." || f==".." ) continue;
			files.push_back( f );
		}while( FindNextFileW( handle,&filedata ) );
		FindClose( handle );
	}else{
//		printf( "FindFirstFileW for LoadDir(%s) failed\n",C_STR(path) );
		fflush( stdout );
	}
	
#else

	if( DIR *dir=opendir( OS_STR(path) ) ){
		while( dirent *ent=readdir( dir ) ){
			String f=ent->d_name;
			if( f=="." || f==".." ) continue;
			files.push_back( f );
		}
		closedir( dir );
	}else{
//		printf( "opendir for LoadDir(%s) failed\n",C_STR(path) );
		fflush( stdout );
	}

#endif

	return files.size() ? Array<String>( &files[0],files.size() ) : Array<String>();
}
	
int CopyFile( String srcpath,String dstpath ){

#if _WIN32

	if( CopyFileW( OS_STR(srcpath),OS_STR(dstpath),FALSE ) ) return 1;
	return 0;
	
#elif __APPLE__

	// Would like to use COPY_ALL here, but it breaks trans on MacOS - produces weird 'pch out of date' error with copied projects.
	//
	// Ranlib strikes back!
	//
	if( copyfile( OS_STR(srcpath),OS_STR(dstpath),0,COPYFILE_DATA )>=0 ) return 1;
	return 0;
	
#else

	int err=-1;
	if( FILE *srcp=_fopen( OS_STR( srcpath ),OS_STR( "rb" ) ) ){
		err=-2;
		if( FILE *dstp=_fopen( OS_STR( dstpath ),OS_STR( "wb" ) ) ){
			err=0;
			char buf[1024];
			while( int n=fread( buf,1,1024,srcp ) ){
				if( fwrite( buf,1,n,dstp )!=n ){
					err=-3;
					break;
				}
			}
			fclose( dstp );
		}else{
//			printf( "FOPEN 'wb' for CopyFile(%s,%s) failed\n",C_STR(srcpath),C_STR(dstpath) );
			fflush( stdout );
		}
		fclose( srcp );
	}else{
//		printf( "FOPEN 'rb' for CopyFile(%s,%s) failed\n",C_STR(srcpath),C_STR(dstpath) );
		fflush( stdout );
	}
	return err==0;
	
#endif
}

int ChangeDir( String path ){
	return chdir( OS_STR(path) );
}

String CurrentDir(){
	std::vector<OS_CHAR> buf( PATH_MAX+1 );
	if( getcwd( &buf[0],buf.size() ) ){}
	buf[buf.size()-1]=0;
	return String( &buf[0] );
}

int CreateDir( String path ){
	mkdir( OS_STR( path ),0777 );
	return FileType(path)==2;
}

int DeleteDir( String path ){
	rmdir( OS_STR(path) );
	return FileType(path)==0;
}

int DeleteFile( String path ){
	remove( OS_STR(path) );
	return FileType(path)==0;
}

int SetEnv( String name,String value ){
#if _WIN32
	return putenv( OS_STR( name+"="+value ) );
#else
	if( value.Length() ) return setenv( OS_STR( name ),OS_STR( value ),1 );
	unsetenv( OS_STR( name ) );
	return 0;
#endif
}

String GetEnv( String name ){
	if( OS_CHAR *p=getenv( OS_STR(name) ) ) return String( p );
	return "";
}

int Execute( String cmd ){

#if _WIN32

	cmd=String("cmd /S /C \"")+cmd+"\"";

	PROCESS_INFORMATION pi={0};
	STARTUPINFOW si={sizeof(si)};

	if( !CreateProcessW( 0,(WCHAR*)(const OS_CHAR*)OS_STR(cmd),0,0,1,CREATE_DEFAULT_ERROR_MODE,0,0,&si,&pi ) ) return -1;

	WaitForSingleObject( pi.hProcess,INFINITE );

	int res=GetExitCodeProcess( pi.hProcess,(DWORD*)&res ) ? res : -1;

	CloseHandle( pi.hProcess );
	CloseHandle( pi.hThread );

	return res;

#else

	return system( OS_STR(cmd) );

#endif
}

int ExitApp( int retcode ){
	exit( retcode );
	return 0;
}

class c_BoolObject;
class c_IntObject;
class c_FloatObject;
class c_StringObject;
class c_Command;
class c_AmazonAds;
class c_App;
class c_Android;
class c_File;
class c_Map;
class c_StringMap;
class c_Node;
class c_Dir;
class c_AmazonPayment;
class c_AndroidAntKey;
class c_AndroidBass;
class c_AndroidChartboost;
class c_AndroidIcons;
class c_AndroidLocalytics;
class c_List;
class c_StringList;
class c_Node2;
class c_HeadNode;
class c_Enumerator;
class c_AndroidRevmob;
class c_AndroidVersion;
class c_List2;
class c_IntList;
class c_Node3;
class c_HeadNode2;
class c_Enumerator2;
class c_AndroidVungle;
class c_GooglePayment;
class c_IosAddLanguage;
class c_Ios;
class c_IosAppirater;
class c_IosBundleId;
class c_IosChartboost;
class c_IosCompressPngFiles;
class c_IosDeploymentTarget;
class c_IosFlurry;
class c_IosFlurryAds;
class c_IosFramework;
class c_IosHideStatusBar;
class c_IosIcons;
class c_IosInterfaceOrientation;
class c_IosLaunchImage;
class c_IosPatchCodeSigningIdentity;
class c_IosProductName;
class c_IosRevmob;
class c_IosVersion;
class c_IosVungle;
class c_SamsungPayment;
class c_ArrayObject;
class c_ClassInfo;
class c_R45;
class c_R46;
class c_R52;
class c_R62;
class c_R72;
class c_R81;
class c_R82;
class c_UnknownClass;
class c_R84;
class c_R86;
class c_R88;
class c_R90;
class c_R92;
class c_R101;
class c_R103;
class c_R106;
class c_R108;
class c_R110;
class c_R112;
class c_R114;
class c_R117;
class c_R119;
class c_R121;
class c_R126;
class c_R128;
class c_R130;
class c_R132;
class c_R134;
class c_R139;
class c_R150;
class c_R159;
class c_R161;
class c_R163;
class c_R166;
class c_R168;
class c_R170;
class c_R174;
class c_R176;
class c_FunctionInfo;
class c_R33;
class c_R34;
class c_R35;
class c_R36;
class c_R37;
class c_R38;
class c_R39;
class c_R40;
class c_R41;
class c_R42;
class c_R43;
class c_R44;
class c__GetClass;
class c___GetClass;
class c_Map2;
class c_StringMap2;
class c_Node4;
class c_MapKeys;
class c_KeyEnumerator;
class c_MapValues;
class c_ValueEnumerator;
class c_ConstInfo;
class c_Stack;
class c_FieldInfo;
class c_Stack2;
class c_GlobalInfo;
class c_Stack3;
class c_MethodInfo;
class c_Stack4;
class c_Stack5;
class c_R47;
class c_R49;
class c_R50;
class c_R48;
class c_R51;
class c_R53;
class c_R56;
class c_R57;
class c_R58;
class c_R59;
class c_R60;
class c_R54;
class c_R55;
class c_R61;
class c_R63;
class c_R66;
class c_R67;
class c_R68;
class c_R69;
class c_R70;
class c_R64;
class c_R65;
class c_R71;
class c_R73;
class c_R77;
class c_R78;
class c_R79;
class c_R74;
class c_R75;
class c_R76;
class c_R80;
class c_R83;
class c_R85;
class c_R87;
class c_R89;
class c_R91;
class c_R93;
class c_R94;
class c_R95;
class c_R96;
class c_R97;
class c_R98;
class c_R99;
class c_R100;
class c_R102;
class c_R104;
class c_R105;
class c_R107;
class c_R109;
class c_R111;
class c_R113;
class c_R115;
class c_R116;
class c_R118;
class c_R120;
class c_R122;
class c_R123;
class c_R124;
class c_R125;
class c_R127;
class c_R129;
class c_R131;
class c_R133;
class c_R135;
class c_R136;
class c_R137;
class c_R138;
class c_R140;
class c_R141;
class c_R142;
class c_R143;
class c_R144;
class c_R145;
class c_R146;
class c_R147;
class c_R148;
class c_R149;
class c_R151;
class c_R152;
class c_R153;
class c_R154;
class c_R155;
class c_R156;
class c_R157;
class c_R158;
class c_R160;
class c_R162;
class c_R164;
class c_R165;
class c_R167;
class c_R169;
class c_R171;
class c_R172;
class c_R173;
class c_R175;
class c_R177;
class c_R179;
class c_R178;
class c_R180;
class c_BoolObject : public Object{
	public:
	bool m_value;
	c_BoolObject();
	c_BoolObject* m_new(bool);
	bool p_ToBool();
	bool p_Equals(c_BoolObject*);
	c_BoolObject* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_BoolObject**p){return "BoolObject";}
class c_IntObject : public Object{
	public:
	int m_value;
	c_IntObject();
	c_IntObject* m_new(int);
	c_IntObject* m_new2(Float);
	int p_ToInt();
	Float p_ToFloat();
	String p_ToString();
	bool p_Equals2(c_IntObject*);
	int p_Compare(c_IntObject*);
	c_IntObject* m_new3();
	void mark();
	String debug();
};
String dbg_type(c_IntObject**p){return "IntObject";}
class c_FloatObject : public Object{
	public:
	Float m_value;
	c_FloatObject();
	c_FloatObject* m_new(int);
	c_FloatObject* m_new2(Float);
	int p_ToInt();
	Float p_ToFloat();
	String p_ToString();
	bool p_Equals3(c_FloatObject*);
	int p_Compare2(c_FloatObject*);
	c_FloatObject* m_new3();
	void mark();
	String debug();
};
String dbg_type(c_FloatObject**p){return "FloatObject";}
class c_StringObject : public Object{
	public:
	String m_value;
	c_StringObject();
	c_StringObject* m_new(int);
	c_StringObject* m_new2(Float);
	c_StringObject* m_new3(String);
	String p_ToString();
	bool p_Equals4(c_StringObject*);
	int p_Compare3(c_StringObject*);
	c_StringObject* m_new4();
	void mark();
	String debug();
};
String dbg_type(c_StringObject**p){return "StringObject";}
Object* bb_boxes_BoxBool(bool);
Object* bb_boxes_BoxInt(int);
Object* bb_boxes_BoxFloat(Float);
Object* bb_boxes_BoxString(String);
bool bb_boxes_UnboxBool(Object*);
int bb_boxes_UnboxInt(Object*);
Float bb_boxes_UnboxFloat(Object*);
String bb_boxes_UnboxString(Object*);
class c_Command : public virtual gc_interface{
	public:
	virtual void p_Run(c_App*)=0;
};
class c_AmazonAds : public Object,public virtual c_Command{
	public:
	c_AmazonAds();
	void p_PatchReceiver(c_App*);
	void p_CopyLibs(c_App*);
	void p_PatchLayout(c_App*);
	void p_Run(c_App*);
	c_AmazonAds* m_new();
	void mark();
	String debug();
};
String dbg_type(c_AmazonAds**p){return "AmazonAds";}
class c_App : public Object{
	public:
	c_StringMap* m_openFiles;
	c_StringMap2* m_commands;
	c_App();
	String p_GetTargetDir();
	c_File* p_TargetFile(String);
	void p_LogWarning(String);
	c_Dir* p_TargetDir(String);
	String p_GetCommandRaw();
	String p_GetCommandDataDir();
	c_File* p_SourceFile(String);
	void p_LogInfo(String);
	Array<String > p_GetAdditionArguments();
	void p_LogError(String);
	c_Dir* p_SourceDir(String);
	String p_GetShortName(String);
	void p_LoadPatchCommands();
	void p_PrintHelp();
	void p_CheckNumberOfArguments();
	void p_CheckTargetDirExists();
	String p_FixCase(String);
	String p_GetCommand();
	void p_ExecuteCommand(String);
	void p_SaveOpenFiles();
	void p_PrintInvalidCommandError(String);
	c_App* m_new();
	void mark();
	String debug();
};
String dbg_type(c_App**p){return "App";}
class c_Android : public Object{
	public:
	c_Android();
	static c_App* m_app;
	static c_File* m_GetManifest();
	static void m_AddPermission(String);
	static void m_EnsureLibsFolder();
	void mark();
	String debug();
};
String dbg_type(c_Android**p){return "Android";}
class c_File : public Object{
	public:
	String m_path;
	bool m_loaded;
	String m__data;
	bool m_dirty;
	c_File();
	c_File* m_new(String);
	c_File* m_new2();
	String p_data();
	void p_data2(String);
	bool p_Contains(String);
	void p_Replace(String,String);
	void p_InsertBefore(String,String);
	String p_GetPath();
	void p_CopyTo(String);
	void p_CopyTo2(c_File*);
	void p_InsertAfter(String,String);
	void p_Append(String);
	bool p_Exists();
	String p_GetContentBetween(String,String);
	String p_Get();
	Array<int > p_FindLines(String);
	void p_ReplaceLine(int,String);
	String p_GetLine(int);
	void p_RemoveLine(int);
	void p_Save();
	void p_InsertAfterLine(int,String);
	bool p_ContainsBetween(String,String,String);
	void p_ReplaceBetween(String,String,String,String);
	void p_InsertAfterBetween(String,String,String,String);
	void p_Remove();
	void p_Set(String);
	String p_GetBasename();
	void mark();
	String debug();
};
String dbg_type(c_File**p){return "File";}
class c_Map : public Object{
	public:
	c_Node* m_root;
	c_Map();
	c_Map* m_new();
	virtual int p_Compare4(String,String)=0;
	c_Node* p_FindNode(String);
	bool p_Contains(String);
	int p_RotateLeft(c_Node*);
	int p_RotateRight(c_Node*);
	int p_InsertFixup(c_Node*);
	bool p_Set2(String,c_File*);
	c_File* p_Get2(String);
	c_MapValues* p_Values();
	c_Node* p_FirstNode();
	void mark();
	String debug();
};
String dbg_type(c_Map**p){return "Map";}
class c_StringMap : public c_Map{
	public:
	c_StringMap();
	c_StringMap* m_new();
	int p_Compare4(String,String);
	void mark();
	String debug();
};
String dbg_type(c_StringMap**p){return "StringMap";}
class c_Node : public Object{
	public:
	String m_key;
	c_Node* m_right;
	c_Node* m_left;
	c_File* m_value;
	int m_color;
	c_Node* m_parent;
	c_Node();
	c_Node* m_new(String,c_File*,int,c_Node*);
	c_Node* m_new2();
	c_Node* p_NextNode();
	void mark();
	String debug();
};
String dbg_type(c_Node**p){return "Node";}
bool bb_helperos_FileExists(String);
class c_Dir : public Object{
	public:
	String m_path;
	c_Dir* m_parent;
	c_Dir();
	c_Dir* m_new(String);
	c_Dir* m_new2();
	bool p_Exists();
	void p_Create();
	String p_GetPath();
	c_Dir* p_Parent();
	void p_Remove2(bool);
	void p_CopyTo3(c_Dir*,bool,bool,bool);
	void mark();
	String debug();
};
String dbg_type(c_Dir**p){return "Dir";}
bool bb_helperos_DirExists(String);
String bb_os_ExtractDir(String);
class c_AmazonPayment : public Object,public virtual c_Command{
	public:
	c_AmazonPayment();
	void p_PatchReceiver(c_App*);
	void p_CopyLibs(c_App*);
	void p_PrintDeveloperHints(c_App*);
	void p_Run(c_App*);
	c_AmazonPayment* m_new();
	void mark();
	String debug();
};
String dbg_type(c_AmazonPayment**p){return "AmazonPayment";}
class c_AndroidAntKey : public Object,public virtual c_Command{
	public:
	c_AndroidAntKey();
	String p_GetArgument(c_App*,int,String);
	void p_Run(c_App*);
	c_AndroidAntKey* m_new();
	void mark();
	String debug();
};
String dbg_type(c_AndroidAntKey**p){return "AndroidAntKey";}
class c_AndroidBass : public Object,public virtual c_Command{
	public:
	c_AndroidBass();
	void p_CopyLibs(c_App*);
	void p_Run(c_App*);
	c_AndroidBass* m_new();
	void mark();
	String debug();
};
String dbg_type(c_AndroidBass**p){return "AndroidBass";}
class c_AndroidChartboost : public Object,public virtual c_Command{
	public:
	c_AndroidChartboost();
	void p_CopyLibs(c_App*);
	void p_Run(c_App*);
	c_AndroidChartboost* m_new();
	void mark();
	String debug();
};
String dbg_type(c_AndroidChartboost**p){return "AndroidChartboost";}
class c_AndroidIcons : public Object,public virtual c_Command{
	public:
	c_App* m_app;
	Array<String > m_VALID_TYPES;
	c_AndroidIcons();
	String p_GetType();
	bool p_IsValidType();
	String p_GetFilename();
	bool p_IsValidFilename();
	void p_CheckArgs();
	String p_GetShortType();
	c_File* p_GetFile();
	void p_Run(c_App*);
	c_AndroidIcons* m_new();
	void mark();
	String debug();
};
String dbg_type(c_AndroidIcons**p){return "AndroidIcons";}
class c_AndroidLocalytics : public Object,public virtual c_Command{
	public:
	c_AndroidLocalytics();
	void p_CopyAndroidBillingLibrary(c_App*);
	void p_PatchBuildXml(c_App*);
	void p_Run(c_App*);
	c_AndroidLocalytics* m_new();
	void mark();
	String debug();
};
String dbg_type(c_AndroidLocalytics**p){return "AndroidLocalytics";}
class c_List : public Object{
	public:
	c_Node2* m__head;
	c_List();
	c_List* m_new();
	c_Node2* p_AddLast(String);
	c_List* m_new2(Array<String >);
	bool p_IsEmpty();
	String p_RemoveFirst();
	virtual bool p_Equals5(String,String);
	c_Node2* p_Find(String,c_Node2*);
	c_Node2* p_Find2(String);
	void p_RemoveFirst2(String);
	int p_Count();
	c_Enumerator* p_ObjectEnumerator();
	Array<String > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_List**p){return "List";}
class c_StringList : public c_List{
	public:
	c_StringList();
	c_StringList* m_new(Array<String >);
	c_StringList* m_new2();
	bool p_Equals5(String,String);
	void mark();
	String debug();
};
String dbg_type(c_StringList**p){return "StringList";}
class c_Node2 : public Object{
	public:
	c_Node2* m__succ;
	c_Node2* m__pred;
	String m__data;
	c_Node2();
	c_Node2* m_new(c_Node2*,c_Node2*,String);
	c_Node2* m_new2();
	int p_Remove();
	void mark();
	String debug();
};
String dbg_type(c_Node2**p){return "Node";}
class c_HeadNode : public c_Node2{
	public:
	c_HeadNode();
	c_HeadNode* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode**p){return "HeadNode";}
class c_Enumerator : public Object{
	public:
	c_List* m__list;
	c_Node2* m__curr;
	c_Enumerator();
	c_Enumerator* m_new(c_List*);
	c_Enumerator* m_new2();
	bool p_HasNext();
	String p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator**p){return "Enumerator";}
Array<String > bb_os_LoadDir(String,bool,bool);
int bb_os_DeleteDir(String,bool);
int bb_os_CopyDir(String,String,bool,bool);
class c_AndroidRevmob : public Object,public virtual c_Command{
	public:
	c_AndroidRevmob();
	void p_PatchActivity(c_App*);
	void p_PatchPermissions();
	void p_CopyLibs(c_App*);
	void p_PatchLayout(c_App*);
	void p_Run(c_App*);
	c_AndroidRevmob* m_new();
	void mark();
	String debug();
};
String dbg_type(c_AndroidRevmob**p){return "AndroidRevmob";}
class c_AndroidVersion : public Object,public virtual c_Command{
	public:
	c_AndroidVersion();
	void p_Run(c_App*);
	c_AndroidVersion* m_new();
	void mark();
	String debug();
};
String dbg_type(c_AndroidVersion**p){return "AndroidVersion";}
class c_List2 : public Object{
	public:
	c_Node3* m__head;
	c_List2();
	c_List2* m_new();
	c_Node3* p_AddLast2(int);
	c_List2* m_new2(Array<int >);
	int p_Count();
	c_Enumerator2* p_ObjectEnumerator();
	Array<int > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_List2**p){return "List";}
class c_IntList : public c_List2{
	public:
	c_IntList();
	c_IntList* m_new(Array<int >);
	c_IntList* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_IntList**p){return "IntList";}
class c_Node3 : public Object{
	public:
	c_Node3* m__succ;
	c_Node3* m__pred;
	int m__data;
	c_Node3();
	c_Node3* m_new(c_Node3*,c_Node3*,int);
	c_Node3* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Node3**p){return "Node";}
class c_HeadNode2 : public c_Node3{
	public:
	c_HeadNode2();
	c_HeadNode2* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode2**p){return "HeadNode";}
class c_Enumerator2 : public Object{
	public:
	c_List2* m__list;
	c_Node3* m__curr;
	c_Enumerator2();
	c_Enumerator2* m_new(c_List2*);
	c_Enumerator2* m_new2();
	bool p_HasNext();
	int p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator2**p){return "Enumerator";}
class c_AndroidVungle : public Object,public virtual c_Command{
	public:
	c_AndroidVungle();
	void p_CopyLibs(c_App*);
	void p_PatchActivity(c_App*);
	void p_Run(c_App*);
	c_AndroidVungle* m_new();
	void mark();
	String debug();
};
String dbg_type(c_AndroidVungle**p){return "AndroidVungle";}
class c_GooglePayment : public Object,public virtual c_Command{
	public:
	c_GooglePayment();
	void p_PatchServiceAndReceiver(c_App*);
	void p_CopyAndroidBillingLibrary(c_App*);
	void p_PatchAndroidBillingLibray(c_App*);
	void p_PatchBuildXml(c_App*);
	void p_Run(c_App*);
	c_GooglePayment* m_new();
	void mark();
	String debug();
};
String dbg_type(c_GooglePayment**p){return "GooglePayment";}
class c_IosAddLanguage : public Object,public virtual c_Command{
	public:
	c_IosAddLanguage();
	static String m_GetLang(c_App*);
	void p_Run(c_App*);
	static void m_CopyImage(c_App*,int,String);
	static void m_AddImage(c_App*,String);
	c_IosAddLanguage* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosAddLanguage**p){return "IosAddLanguage";}
class c_Ios : public Object{
	public:
	c_Ios();
	static c_App* m_app;
	static c_File* m_GetProject();
	static String m_IntToHex(int);
	static String m_GenerateRandomId();
	static String m_GenerateUniqueId();
	static void m_AddPbxBuildFile(String,String,String,bool,String);
	static void m_AddPbxFileReference(String,String);
	static void m_AddPbxFileReferenceFile(String,String,String,String);
	static void m_AddIconPBXGroup(String,String);
	static void m_AddIconPBXResourcesBuildPhase(String,String);
	static void m_EnsurePBXVariantGroupSection();
	static void m_AddPBXVariantGroup(String,String,Array<String >,Array<String >);
	static void m_AddIconPBXBuildFile(String,String,String);
	static void m_AddIconPBXFileReference(String,String);
	static bool m_ContainsFramework(String);
	static void m_AddPbxFileReferenceSdk(String,String);
	static void m_AddPbxFrameworkBuildPhase(String,String);
	static void m_AddPbxGroupChild(String,String,String);
	static void m_AddFramework(String,bool);
	static void m_AddPbxFileReferenceLProj(String,String,String);
	static void m_AddKnownRegion(String);
	static void m_RegisterPxbGroup(String,String);
	static void m_AddPbxGroup(String,String,String);
	static void m_AddPbxResource(String,String);
	static c_File* m_GetPlist();
	static int m_GetKeyLine(c_File*,String);
	static void m_ValidateValueLine(c_File*,int);
	static void m_UpdatePlistSetting(String,String);
	static void m_AddPbxFileReferencePath(String,String);
	static void m_EnsureSearchPathWithSRCROOT(String);
	static void m_AddFrameworkFromPath(String,bool);
	static void m_UpdateDeploymentTarget(String);
	static c_File* m_GetMainSource();
	static String m_ExtractSettingKey(String);
	static String m_GetProjectSetting(String);
	static void m_UpdateProjectSetting(String,String);
	void mark();
	String debug();
};
String dbg_type(c_Ios**p){return "Ios";}
extern int bb_random_Seed;
Float bb_random_Rnd();
Float bb_random_Rnd2(Float,Float);
Float bb_random_Rnd3(Float);
class c_IosAppirater : public Object,public virtual c_Command{
	public:
	c_IosAppirater();
	static void m_CopyFramework(c_App*);
	static Array<String > m_GetRegions();
	static void m_AddRegionFiles(c_App*);
	static void m_AddSourceFiles(c_App*);
	void p_Run(c_App*);
	c_IosAppirater* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosAppirater**p){return "IosAppirater";}
class c_IosBundleId : public Object,public virtual c_Command{
	public:
	c_IosBundleId();
	static String m_GetBundleId(c_App*);
	void p_Run(c_App*);
	c_IosBundleId* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosBundleId**p){return "IosBundleId";}
class c_IosChartboost : public Object,public virtual c_Command{
	public:
	c_IosChartboost();
	void p_ConvertToFrameworkDir(c_App*);
	void p_CopyFramework(c_App*);
	void p_Run(c_App*);
	c_IosChartboost* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosChartboost**p){return "IosChartboost";}
class c_IosCompressPngFiles : public Object,public virtual c_Command{
	public:
	c_App* m_app;
	c_IosCompressPngFiles();
	bool p_IsEnabled();
	void p_RemoveOldSettings();
	void p_AddSettings(bool);
	void p_Run(c_App*);
	c_IosCompressPngFiles* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosCompressPngFiles**p){return "IosCompressPngFiles";}
class c_IosDeploymentTarget : public Object,public virtual c_Command{
	public:
	c_IosDeploymentTarget();
	String p_GetTarget(c_App*);
	void p_Run(c_App*);
	c_IosDeploymentTarget* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosDeploymentTarget**p){return "IosDeploymentTarget";}
class c_IosFlurry : public Object,public virtual c_Command{
	public:
	c_IosFlurry();
	void p_ConvertToFrameworkDir(c_App*);
	void p_CopyFramework(c_App*);
	void p_Run(c_App*);
	c_IosFlurry* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosFlurry**p){return "IosFlurry";}
class c_IosFlurryAds : public Object,public virtual c_Command{
	public:
	c_IosFlurryAds();
	void p_ConvertToFrameworkDir(c_App*);
	void p_CopyFramework(c_App*);
	void p_Run(c_App*);
	c_IosFlurryAds* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosFlurryAds**p){return "IosFlurryAds";}
class c_IosFramework : public Object,public virtual c_Command{
	public:
	c_IosFramework();
	void p_Run(c_App*);
	c_IosFramework* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosFramework**p){return "IosFramework";}
class c_IosHideStatusBar : public Object,public virtual c_Command{
	public:
	c_App* m_app;
	c_IosHideStatusBar();
	void p_RemoveOldSettings();
	bool p_IsEnabled();
	void p_AddSettings2();
	void p_Run(c_App*);
	c_IosHideStatusBar* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosHideStatusBar**p){return "IosHideStatusBar";}
class c_IosIcons : public Object,public virtual c_Command{
	public:
	c_App* m_app;
	c_IosIcons();
	bool p_IsPrerendered();
	Array<c_File* > p_GetNewIcons();
	void p_CheckNewIconsExists(Array<c_File* >);
	void p_RemovePrerenderedFlag();
	Array<String > p_RemoveKeyWithValues(String);
	String p_ExtractFileName(String);
	void p_RemoveFileDefinition(String);
	void p_RemoveFilePhysical(String);
	void p_ParseRowsAndRemoveFiles(Array<String >);
	void p_RemoveIcons();
	void p_AddPrerenderedFlag(bool);
	void p_AddIconsPlist(Array<c_File* >,bool);
	void p_AddIconsDefinitions(Array<c_File* >);
	void p_AddIcons(Array<c_File* >,bool);
	void p_CopyIconFiles(Array<c_File* >);
	void p_Run(c_App*);
	c_IosIcons* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosIcons**p){return "IosIcons";}
class c_IosInterfaceOrientation : public Object,public virtual c_Command{
	public:
	c_App* m_app;
	c_IosInterfaceOrientation();
	void p_RemoveKeyWithValues2(int);
	void p_RemoveOldSettings();
	void p_AddOrientationBothPlist();
	String p_GetOrientation();
	String p_AddOrientationBoth();
	String p_AddOrientationLandscape();
	String p_AddOrientationPortrait();
	void p_Run(c_App*);
	c_IosInterfaceOrientation* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosInterfaceOrientation**p){return "IosInterfaceOrientation";}
class c_IosLaunchImage : public Object,public virtual c_Command{
	public:
	c_IosLaunchImage();
	static String m_GetMode(c_App*);
	static void m_CopyImage(c_App*,int,String);
	static void m_AddImage(c_App*,String);
	static void m_ProcessIPhone(c_App*);
	static void m_ProcessIPadLandscape(c_App*);
	static void m_ProcessIPadPortrait(c_App*);
	void p_Run(c_App*);
	c_IosLaunchImage* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosLaunchImage**p){return "IosLaunchImage";}
class c_IosPatchCodeSigningIdentity : public Object,public virtual c_Command{
	public:
	c_App* m_app;
	c_IosPatchCodeSigningIdentity();
	void p_Run(c_App*);
	c_IosPatchCodeSigningIdentity* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosPatchCodeSigningIdentity**p){return "IosPatchCodeSigningIdentity";}
class c_IosProductName : public Object,public virtual c_Command{
	public:
	c_App* m_app;
	c_IosProductName();
	String p_GetNewName();
	void p_Run(c_App*);
	c_IosProductName* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosProductName**p){return "IosProductName";}
class c_IosRevmob : public Object,public virtual c_Command{
	public:
	c_IosRevmob();
	void p_CopyFramework(c_App*);
	void p_Run(c_App*);
	c_IosRevmob* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosRevmob**p){return "IosRevmob";}
class c_IosVersion : public Object,public virtual c_Command{
	public:
	c_IosVersion();
	static String m_GetVersion(c_App*);
	void p_Run(c_App*);
	c_IosVersion* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosVersion**p){return "IosVersion";}
class c_IosVungle : public Object,public virtual c_Command{
	public:
	c_IosVungle();
	void p_AddLibZ();
	void p_AddOrientationPortrait();
	void p_CopyFramework(c_App*);
	void p_Run(c_App*);
	c_IosVungle* m_new();
	void mark();
	String debug();
};
String dbg_type(c_IosVungle**p){return "IosVungle";}
class c_SamsungPayment : public Object,public virtual c_Command{
	public:
	c_SamsungPayment();
	void p_CopyLibs(c_App*);
	void p_PatchBuildXml(c_App*);
	void p_Run(c_App*);
	c_SamsungPayment* m_new();
	void mark();
	String debug();
};
String dbg_type(c_SamsungPayment**p){return "SamsungPayment";}
class c_ArrayObject : public Object{
	public:
	Array<String > m_value;
	c_ArrayObject();
	c_ArrayObject* m_new(Array<String >);
	Array<String > p_ToArray();
	c_ArrayObject* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_ArrayObject**p){return "ArrayObject";}
class c_ClassInfo : public Object{
	public:
	String m__name;
	int m__attrs;
	c_ClassInfo* m__sclass;
	Array<c_ClassInfo* > m__ifaces;
	Array<c_ConstInfo* > m__rconsts;
	Array<c_ConstInfo* > m__consts;
	Array<c_FieldInfo* > m__rfields;
	Array<c_FieldInfo* > m__fields;
	Array<c_GlobalInfo* > m__rglobals;
	Array<c_GlobalInfo* > m__globals;
	Array<c_MethodInfo* > m__rmethods;
	Array<c_MethodInfo* > m__methods;
	Array<c_FunctionInfo* > m__rfunctions;
	Array<c_FunctionInfo* > m__functions;
	Array<c_FunctionInfo* > m__ctors;
	c_ClassInfo();
	c_ClassInfo* m_new(String,int,c_ClassInfo*,Array<c_ClassInfo* >);
	c_ClassInfo* m_new2();
	virtual int p_Init();
	String p_Name();
	virtual Object* p_NewInstance();
	int p_InitR();
	void mark();
	String debug();
};
String dbg_type(c_ClassInfo**p){return "ClassInfo";}
extern Array<c_ClassInfo* > bb_reflection__classes;
class c_R45 : public c_ClassInfo{
	public:
	c_R45();
	c_R45* m_new();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R45**p){return "R45";}
class c_R46 : public c_ClassInfo{
	public:
	c_R46();
	c_R46* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R46**p){return "R46";}
extern c_ClassInfo* bb_reflection__boolClass;
class c_R52 : public c_ClassInfo{
	public:
	c_R52();
	c_R52* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R52**p){return "R52";}
extern c_ClassInfo* bb_reflection__intClass;
class c_R62 : public c_ClassInfo{
	public:
	c_R62();
	c_R62* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R62**p){return "R62";}
extern c_ClassInfo* bb_reflection__floatClass;
class c_R72 : public c_ClassInfo{
	public:
	c_R72();
	c_R72* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R72**p){return "R72";}
extern c_ClassInfo* bb_reflection__stringClass;
class c_R81 : public c_ClassInfo{
	public:
	c_R81();
	c_R81* m_new();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R81**p){return "R81";}
class c_R82 : public c_ClassInfo{
	public:
	c_R82();
	c_R82* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R82**p){return "R82";}
class c_UnknownClass : public c_ClassInfo{
	public:
	c_UnknownClass();
	c_UnknownClass* m_new();
	void mark();
	String debug();
};
String dbg_type(c_UnknownClass**p){return "UnknownClass";}
extern c_ClassInfo* bb_reflection__unknownClass;
class c_R84 : public c_ClassInfo{
	public:
	c_R84();
	c_R84* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R84**p){return "R84";}
class c_R86 : public c_ClassInfo{
	public:
	c_R86();
	c_R86* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R86**p){return "R86";}
class c_R88 : public c_ClassInfo{
	public:
	c_R88();
	c_R88* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R88**p){return "R88";}
class c_R90 : public c_ClassInfo{
	public:
	c_R90();
	c_R90* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R90**p){return "R90";}
class c_R92 : public c_ClassInfo{
	public:
	c_R92();
	c_R92* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R92**p){return "R92";}
class c_R101 : public c_ClassInfo{
	public:
	c_R101();
	c_R101* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R101**p){return "R101";}
class c_R103 : public c_ClassInfo{
	public:
	c_R103();
	c_R103* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R103**p){return "R103";}
class c_R106 : public c_ClassInfo{
	public:
	c_R106();
	c_R106* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R106**p){return "R106";}
class c_R108 : public c_ClassInfo{
	public:
	c_R108();
	c_R108* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R108**p){return "R108";}
class c_R110 : public c_ClassInfo{
	public:
	c_R110();
	c_R110* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R110**p){return "R110";}
class c_R112 : public c_ClassInfo{
	public:
	c_R112();
	c_R112* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R112**p){return "R112";}
class c_R114 : public c_ClassInfo{
	public:
	c_R114();
	c_R114* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R114**p){return "R114";}
class c_R117 : public c_ClassInfo{
	public:
	c_R117();
	c_R117* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R117**p){return "R117";}
class c_R119 : public c_ClassInfo{
	public:
	c_R119();
	c_R119* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R119**p){return "R119";}
class c_R121 : public c_ClassInfo{
	public:
	c_R121();
	c_R121* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R121**p){return "R121";}
class c_R126 : public c_ClassInfo{
	public:
	c_R126();
	c_R126* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R126**p){return "R126";}
class c_R128 : public c_ClassInfo{
	public:
	c_R128();
	c_R128* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R128**p){return "R128";}
class c_R130 : public c_ClassInfo{
	public:
	c_R130();
	c_R130* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R130**p){return "R130";}
class c_R132 : public c_ClassInfo{
	public:
	c_R132();
	c_R132* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R132**p){return "R132";}
class c_R134 : public c_ClassInfo{
	public:
	c_R134();
	c_R134* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R134**p){return "R134";}
class c_R139 : public c_ClassInfo{
	public:
	c_R139();
	c_R139* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R139**p){return "R139";}
class c_R150 : public c_ClassInfo{
	public:
	c_R150();
	c_R150* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R150**p){return "R150";}
class c_R159 : public c_ClassInfo{
	public:
	c_R159();
	c_R159* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R159**p){return "R159";}
class c_R161 : public c_ClassInfo{
	public:
	c_R161();
	c_R161* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R161**p){return "R161";}
class c_R163 : public c_ClassInfo{
	public:
	c_R163();
	c_R163* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R163**p){return "R163";}
class c_R166 : public c_ClassInfo{
	public:
	c_R166();
	c_R166* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R166**p){return "R166";}
class c_R168 : public c_ClassInfo{
	public:
	c_R168();
	c_R168* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R168**p){return "R168";}
class c_R170 : public c_ClassInfo{
	public:
	c_R170();
	c_R170* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R170**p){return "R170";}
class c_R174 : public c_ClassInfo{
	public:
	c_R174();
	c_R174* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R174**p){return "R174";}
class c_R176 : public c_ClassInfo{
	public:
	c_R176();
	c_R176* m_new();
	Object* p_NewInstance();
	int p_Init();
	void mark();
	String debug();
};
String dbg_type(c_R176**p){return "R176";}
class c_FunctionInfo : public Object{
	public:
	String m__name;
	int m__attrs;
	c_ClassInfo* m__retType;
	Array<c_ClassInfo* > m__argTypes;
	c_FunctionInfo();
	c_FunctionInfo* m_new(String,int,c_ClassInfo*,Array<c_ClassInfo* >);
	c_FunctionInfo* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_FunctionInfo**p){return "FunctionInfo";}
extern Array<c_FunctionInfo* > bb_reflection__functions;
class c_R33 : public c_FunctionInfo{
	public:
	c_R33();
	c_R33* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R33**p){return "R33";}
class c_R34 : public c_FunctionInfo{
	public:
	c_R34();
	c_R34* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R34**p){return "R34";}
class c_R35 : public c_FunctionInfo{
	public:
	c_R35();
	c_R35* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R35**p){return "R35";}
class c_R36 : public c_FunctionInfo{
	public:
	c_R36();
	c_R36* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R36**p){return "R36";}
class c_R37 : public c_FunctionInfo{
	public:
	c_R37();
	c_R37* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R37**p){return "R37";}
class c_R38 : public c_FunctionInfo{
	public:
	c_R38();
	c_R38* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R38**p){return "R38";}
class c_R39 : public c_FunctionInfo{
	public:
	c_R39();
	c_R39* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R39**p){return "R39";}
class c_R40 : public c_FunctionInfo{
	public:
	c_R40();
	c_R40* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R40**p){return "R40";}
class c_R41 : public c_FunctionInfo{
	public:
	c_R41();
	c_R41* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R41**p){return "R41";}
class c_R42 : public c_FunctionInfo{
	public:
	c_R42();
	c_R42* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R42**p){return "R42";}
class c_R43 : public c_FunctionInfo{
	public:
	c_R43();
	c_R43* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R43**p){return "R43";}
class c_R44 : public c_FunctionInfo{
	public:
	c_R44();
	c_R44* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R44**p){return "R44";}
class c__GetClass : public Object{
	public:
	c__GetClass();
	c__GetClass* m_new();
	void mark();
	String debug();
};
String dbg_type(c__GetClass**p){return "_GetClass";}
class c___GetClass : public c__GetClass{
	public:
	c___GetClass();
	c___GetClass* m_new();
	void mark();
	String debug();
};
String dbg_type(c___GetClass**p){return "__GetClass";}
extern c__GetClass* bb_reflection__getClass;
int bb_reflection___init();
extern int bb_reflection__init;
Array<c_ClassInfo* > bb_reflection_GetClasses();
class c_Map2 : public Object{
	public:
	c_Node4* m_root;
	c_Map2();
	c_Map2* m_new();
	virtual int p_Compare4(String,String)=0;
	int p_RotateLeft2(c_Node4*);
	int p_RotateRight2(c_Node4*);
	int p_InsertFixup2(c_Node4*);
	bool p_Add(String,c_ClassInfo*);
	c_MapKeys* p_Keys();
	c_Node4* p_FirstNode();
	c_Node4* p_FindNode(String);
	c_ClassInfo* p_Get2(String);
	void mark();
	String debug();
};
String dbg_type(c_Map2**p){return "Map";}
class c_StringMap2 : public c_Map2{
	public:
	c_StringMap2();
	c_StringMap2* m_new();
	int p_Compare4(String,String);
	void mark();
	String debug();
};
String dbg_type(c_StringMap2**p){return "StringMap";}
class c_Node4 : public Object{
	public:
	String m_key;
	c_Node4* m_right;
	c_Node4* m_left;
	c_ClassInfo* m_value;
	int m_color;
	c_Node4* m_parent;
	c_Node4();
	c_Node4* m_new(String,c_ClassInfo*,int,c_Node4*);
	c_Node4* m_new2();
	c_Node4* p_NextNode();
	void mark();
	String debug();
};
String dbg_type(c_Node4**p){return "Node";}
class c_MapKeys : public Object{
	public:
	c_Map2* m_map;
	c_MapKeys();
	c_MapKeys* m_new(c_Map2*);
	c_MapKeys* m_new2();
	c_KeyEnumerator* p_ObjectEnumerator();
	void mark();
	String debug();
};
String dbg_type(c_MapKeys**p){return "MapKeys";}
class c_KeyEnumerator : public Object{
	public:
	c_Node4* m_node;
	c_KeyEnumerator();
	c_KeyEnumerator* m_new(c_Node4*);
	c_KeyEnumerator* m_new2();
	bool p_HasNext();
	String p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_KeyEnumerator**p){return "KeyEnumerator";}
class c_MapValues : public Object{
	public:
	c_Map* m_map;
	c_MapValues();
	c_MapValues* m_new(c_Map*);
	c_MapValues* m_new2();
	c_ValueEnumerator* p_ObjectEnumerator();
	void mark();
	String debug();
};
String dbg_type(c_MapValues**p){return "MapValues";}
class c_ValueEnumerator : public Object{
	public:
	c_Node* m_node;
	c_ValueEnumerator();
	c_ValueEnumerator* m_new(c_Node*);
	c_ValueEnumerator* m_new2();
	bool p_HasNext();
	c_File* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_ValueEnumerator**p){return "ValueEnumerator";}
int bbMain();
class c_ConstInfo : public Object{
	public:
	String m__name;
	int m__attrs;
	c_ClassInfo* m__type;
	Object* m__value;
	c_ConstInfo();
	c_ConstInfo* m_new(String,int,c_ClassInfo*,Object*);
	c_ConstInfo* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_ConstInfo**p){return "ConstInfo";}
class c_Stack : public Object{
	public:
	Array<c_ConstInfo* > m_data;
	int m_length;
	c_Stack();
	c_Stack* m_new();
	c_Stack* m_new2(Array<c_ConstInfo* >);
	void p_Push(c_ConstInfo*);
	void p_Push2(Array<c_ConstInfo* >,int,int);
	void p_Push3(Array<c_ConstInfo* >,int);
	Array<c_ConstInfo* > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_Stack**p){return "Stack";}
class c_FieldInfo : public Object{
	public:
	String m__name;
	int m__attrs;
	c_ClassInfo* m__type;
	c_FieldInfo();
	c_FieldInfo* m_new(String,int,c_ClassInfo*);
	c_FieldInfo* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_FieldInfo**p){return "FieldInfo";}
class c_Stack2 : public Object{
	public:
	Array<c_FieldInfo* > m_data;
	int m_length;
	c_Stack2();
	c_Stack2* m_new();
	c_Stack2* m_new2(Array<c_FieldInfo* >);
	void p_Push4(c_FieldInfo*);
	void p_Push5(Array<c_FieldInfo* >,int,int);
	void p_Push6(Array<c_FieldInfo* >,int);
	Array<c_FieldInfo* > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_Stack2**p){return "Stack";}
class c_GlobalInfo : public Object{
	public:
	c_GlobalInfo();
	void mark();
	String debug();
};
String dbg_type(c_GlobalInfo**p){return "GlobalInfo";}
class c_Stack3 : public Object{
	public:
	Array<c_GlobalInfo* > m_data;
	int m_length;
	c_Stack3();
	c_Stack3* m_new();
	c_Stack3* m_new2(Array<c_GlobalInfo* >);
	void p_Push7(c_GlobalInfo*);
	void p_Push8(Array<c_GlobalInfo* >,int,int);
	void p_Push9(Array<c_GlobalInfo* >,int);
	Array<c_GlobalInfo* > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_Stack3**p){return "Stack";}
class c_MethodInfo : public Object{
	public:
	String m__name;
	int m__attrs;
	c_ClassInfo* m__retType;
	Array<c_ClassInfo* > m__argTypes;
	c_MethodInfo();
	c_MethodInfo* m_new(String,int,c_ClassInfo*,Array<c_ClassInfo* >);
	c_MethodInfo* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_MethodInfo**p){return "MethodInfo";}
class c_Stack4 : public Object{
	public:
	Array<c_MethodInfo* > m_data;
	int m_length;
	c_Stack4();
	c_Stack4* m_new();
	c_Stack4* m_new2(Array<c_MethodInfo* >);
	void p_Push10(c_MethodInfo*);
	void p_Push11(Array<c_MethodInfo* >,int,int);
	void p_Push12(Array<c_MethodInfo* >,int);
	Array<c_MethodInfo* > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_Stack4**p){return "Stack";}
class c_Stack5 : public Object{
	public:
	Array<c_FunctionInfo* > m_data;
	int m_length;
	c_Stack5();
	c_Stack5* m_new();
	c_Stack5* m_new2(Array<c_FunctionInfo* >);
	void p_Push13(c_FunctionInfo*);
	void p_Push14(Array<c_FunctionInfo* >,int,int);
	void p_Push15(Array<c_FunctionInfo* >,int);
	Array<c_FunctionInfo* > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_Stack5**p){return "Stack";}
class c_R47 : public c_FieldInfo{
	public:
	c_R47();
	c_R47* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R47**p){return "R47";}
class c_R49 : public c_MethodInfo{
	public:
	c_R49();
	c_R49* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R49**p){return "R49";}
class c_R50 : public c_MethodInfo{
	public:
	c_R50();
	c_R50* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R50**p){return "R50";}
class c_R48 : public c_FunctionInfo{
	public:
	c_R48();
	c_R48* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R48**p){return "R48";}
class c_R51 : public c_FunctionInfo{
	public:
	c_R51();
	c_R51* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R51**p){return "R51";}
class c_R53 : public c_FieldInfo{
	public:
	c_R53();
	c_R53* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R53**p){return "R53";}
class c_R56 : public c_MethodInfo{
	public:
	c_R56();
	c_R56* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R56**p){return "R56";}
class c_R57 : public c_MethodInfo{
	public:
	c_R57();
	c_R57* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R57**p){return "R57";}
class c_R58 : public c_MethodInfo{
	public:
	c_R58();
	c_R58* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R58**p){return "R58";}
class c_R59 : public c_MethodInfo{
	public:
	c_R59();
	c_R59* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R59**p){return "R59";}
class c_R60 : public c_MethodInfo{
	public:
	c_R60();
	c_R60* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R60**p){return "R60";}
class c_R54 : public c_FunctionInfo{
	public:
	c_R54();
	c_R54* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R54**p){return "R54";}
class c_R55 : public c_FunctionInfo{
	public:
	c_R55();
	c_R55* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R55**p){return "R55";}
class c_R61 : public c_FunctionInfo{
	public:
	c_R61();
	c_R61* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R61**p){return "R61";}
class c_R63 : public c_FieldInfo{
	public:
	c_R63();
	c_R63* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R63**p){return "R63";}
class c_R66 : public c_MethodInfo{
	public:
	c_R66();
	c_R66* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R66**p){return "R66";}
class c_R67 : public c_MethodInfo{
	public:
	c_R67();
	c_R67* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R67**p){return "R67";}
class c_R68 : public c_MethodInfo{
	public:
	c_R68();
	c_R68* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R68**p){return "R68";}
class c_R69 : public c_MethodInfo{
	public:
	c_R69();
	c_R69* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R69**p){return "R69";}
class c_R70 : public c_MethodInfo{
	public:
	c_R70();
	c_R70* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R70**p){return "R70";}
class c_R64 : public c_FunctionInfo{
	public:
	c_R64();
	c_R64* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R64**p){return "R64";}
class c_R65 : public c_FunctionInfo{
	public:
	c_R65();
	c_R65* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R65**p){return "R65";}
class c_R71 : public c_FunctionInfo{
	public:
	c_R71();
	c_R71* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R71**p){return "R71";}
class c_R73 : public c_FieldInfo{
	public:
	c_R73();
	c_R73* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R73**p){return "R73";}
class c_R77 : public c_MethodInfo{
	public:
	c_R77();
	c_R77* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R77**p){return "R77";}
class c_R78 : public c_MethodInfo{
	public:
	c_R78();
	c_R78* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R78**p){return "R78";}
class c_R79 : public c_MethodInfo{
	public:
	c_R79();
	c_R79* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R79**p){return "R79";}
class c_R74 : public c_FunctionInfo{
	public:
	c_R74();
	c_R74* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R74**p){return "R74";}
class c_R75 : public c_FunctionInfo{
	public:
	c_R75();
	c_R75* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R75**p){return "R75";}
class c_R76 : public c_FunctionInfo{
	public:
	c_R76();
	c_R76* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R76**p){return "R76";}
class c_R80 : public c_FunctionInfo{
	public:
	c_R80();
	c_R80* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R80**p){return "R80";}
class c_R83 : public c_FunctionInfo{
	public:
	c_R83();
	c_R83* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R83**p){return "R83";}
class c_R85 : public c_FunctionInfo{
	public:
	c_R85();
	c_R85* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R85**p){return "R85";}
class c_R87 : public c_FunctionInfo{
	public:
	c_R87();
	c_R87* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R87**p){return "R87";}
class c_R89 : public c_FunctionInfo{
	public:
	c_R89();
	c_R89* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R89**p){return "R89";}
class c_R91 : public c_FunctionInfo{
	public:
	c_R91();
	c_R91* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R91**p){return "R91";}
class c_R93 : public c_FieldInfo{
	public:
	c_R93();
	c_R93* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R93**p){return "R93";}
class c_R94 : public c_MethodInfo{
	public:
	c_R94();
	c_R94* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R94**p){return "R94";}
class c_R95 : public c_MethodInfo{
	public:
	c_R95();
	c_R95* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R95**p){return "R95";}
class c_R96 : public c_MethodInfo{
	public:
	c_R96();
	c_R96* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R96**p){return "R96";}
class c_R97 : public c_MethodInfo{
	public:
	c_R97();
	c_R97* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R97**p){return "R97";}
class c_R98 : public c_MethodInfo{
	public:
	c_R98();
	c_R98* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R98**p){return "R98";}
class c_R99 : public c_MethodInfo{
	public:
	c_R99();
	c_R99* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R99**p){return "R99";}
class c_R100 : public c_FunctionInfo{
	public:
	c_R100();
	c_R100* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R100**p){return "R100";}
class c_R102 : public c_FunctionInfo{
	public:
	c_R102();
	c_R102* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R102**p){return "R102";}
class c_R104 : public c_MethodInfo{
	public:
	c_R104();
	c_R104* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R104**p){return "R104";}
class c_R105 : public c_FunctionInfo{
	public:
	c_R105();
	c_R105* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R105**p){return "R105";}
class c_R107 : public c_FunctionInfo{
	public:
	c_R107();
	c_R107* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R107**p){return "R107";}
class c_R109 : public c_FunctionInfo{
	public:
	c_R109();
	c_R109* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R109**p){return "R109";}
class c_R111 : public c_FunctionInfo{
	public:
	c_R111();
	c_R111* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R111**p){return "R111";}
class c_R113 : public c_FunctionInfo{
	public:
	c_R113();
	c_R113* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R113**p){return "R113";}
class c_R115 : public c_FunctionInfo{
	public:
	c_R115();
	c_R115* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R115**p){return "R115";}
class c_R116 : public c_FunctionInfo{
	public:
	c_R116();
	c_R116* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R116**p){return "R116";}
class c_R118 : public c_FunctionInfo{
	public:
	c_R118();
	c_R118* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R118**p){return "R118";}
class c_R120 : public c_FunctionInfo{
	public:
	c_R120();
	c_R120* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R120**p){return "R120";}
class c_R122 : public c_MethodInfo{
	public:
	c_R122();
	c_R122* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R122**p){return "R122";}
class c_R123 : public c_MethodInfo{
	public:
	c_R123();
	c_R123* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R123**p){return "R123";}
class c_R124 : public c_MethodInfo{
	public:
	c_R124();
	c_R124* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R124**p){return "R124";}
class c_R125 : public c_FunctionInfo{
	public:
	c_R125();
	c_R125* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R125**p){return "R125";}
class c_R127 : public c_FunctionInfo{
	public:
	c_R127();
	c_R127* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R127**p){return "R127";}
class c_R129 : public c_FunctionInfo{
	public:
	c_R129();
	c_R129* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R129**p){return "R129";}
class c_R131 : public c_FunctionInfo{
	public:
	c_R131();
	c_R131* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R131**p){return "R131";}
class c_R133 : public c_FunctionInfo{
	public:
	c_R133();
	c_R133* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R133**p){return "R133";}
class c_R135 : public c_MethodInfo{
	public:
	c_R135();
	c_R135* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R135**p){return "R135";}
class c_R136 : public c_MethodInfo{
	public:
	c_R136();
	c_R136* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R136**p){return "R136";}
class c_R137 : public c_MethodInfo{
	public:
	c_R137();
	c_R137* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R137**p){return "R137";}
class c_R138 : public c_FunctionInfo{
	public:
	c_R138();
	c_R138* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R138**p){return "R138";}
class c_R140 : public c_MethodInfo{
	public:
	c_R140();
	c_R140* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R140**p){return "R140";}
class c_R141 : public c_MethodInfo{
	public:
	c_R141();
	c_R141* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R141**p){return "R141";}
class c_R142 : public c_MethodInfo{
	public:
	c_R142();
	c_R142* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R142**p){return "R142";}
class c_R143 : public c_MethodInfo{
	public:
	c_R143();
	c_R143* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R143**p){return "R143";}
class c_R144 : public c_MethodInfo{
	public:
	c_R144();
	c_R144* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R144**p){return "R144";}
class c_R145 : public c_MethodInfo{
	public:
	c_R145();
	c_R145* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R145**p){return "R145";}
class c_R146 : public c_MethodInfo{
	public:
	c_R146();
	c_R146* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R146**p){return "R146";}
class c_R147 : public c_MethodInfo{
	public:
	c_R147();
	c_R147* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R147**p){return "R147";}
class c_R148 : public c_MethodInfo{
	public:
	c_R148();
	c_R148* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R148**p){return "R148";}
class c_R149 : public c_FunctionInfo{
	public:
	c_R149();
	c_R149* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R149**p){return "R149";}
class c_R151 : public c_MethodInfo{
	public:
	c_R151();
	c_R151* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R151**p){return "R151";}
class c_R152 : public c_MethodInfo{
	public:
	c_R152();
	c_R152* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R152**p){return "R152";}
class c_R153 : public c_MethodInfo{
	public:
	c_R153();
	c_R153* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R153**p){return "R153";}
class c_R154 : public c_MethodInfo{
	public:
	c_R154();
	c_R154* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R154**p){return "R154";}
class c_R155 : public c_MethodInfo{
	public:
	c_R155();
	c_R155* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R155**p){return "R155";}
class c_R156 : public c_MethodInfo{
	public:
	c_R156();
	c_R156* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R156**p){return "R156";}
class c_R157 : public c_MethodInfo{
	public:
	c_R157();
	c_R157* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R157**p){return "R157";}
class c_R158 : public c_FunctionInfo{
	public:
	c_R158();
	c_R158* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R158**p){return "R158";}
class c_R160 : public c_FunctionInfo{
	public:
	c_R160();
	c_R160* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R160**p){return "R160";}
class c_R162 : public c_FunctionInfo{
	public:
	c_R162();
	c_R162* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R162**p){return "R162";}
class c_R164 : public c_MethodInfo{
	public:
	c_R164();
	c_R164* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R164**p){return "R164";}
class c_R165 : public c_FunctionInfo{
	public:
	c_R165();
	c_R165* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R165**p){return "R165";}
class c_R167 : public c_FunctionInfo{
	public:
	c_R167();
	c_R167* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R167**p){return "R167";}
class c_R169 : public c_FunctionInfo{
	public:
	c_R169();
	c_R169* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R169**p){return "R169";}
class c_R171 : public c_MethodInfo{
	public:
	c_R171();
	c_R171* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R171**p){return "R171";}
class c_R172 : public c_MethodInfo{
	public:
	c_R172();
	c_R172* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R172**p){return "R172";}
class c_R173 : public c_FunctionInfo{
	public:
	c_R173();
	c_R173* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R173**p){return "R173";}
class c_R175 : public c_FunctionInfo{
	public:
	c_R175();
	c_R175* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R175**p){return "R175";}
class c_R177 : public c_FieldInfo{
	public:
	c_R177();
	c_R177* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R177**p){return "R177";}
class c_R179 : public c_MethodInfo{
	public:
	c_R179();
	c_R179* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R179**p){return "R179";}
class c_R178 : public c_FunctionInfo{
	public:
	c_R178();
	c_R178* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R178**p){return "R178";}
class c_R180 : public c_FunctionInfo{
	public:
	c_R180();
	c_R180* m_new();
	void mark();
	String debug();
};
String dbg_type(c_R180**p){return "R180";}
c_BoolObject::c_BoolObject(){
	m_value=false;
}
c_BoolObject* c_BoolObject::m_new(bool t_value){
	DBG_ENTER("BoolObject.new")
	c_BoolObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<11>");
	this->m_value=t_value;
	return this;
}
bool c_BoolObject::p_ToBool(){
	DBG_ENTER("BoolObject.ToBool")
	c_BoolObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<15>");
	return m_value;
}
bool c_BoolObject::p_Equals(c_BoolObject* t_box){
	DBG_ENTER("BoolObject.Equals")
	c_BoolObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<19>");
	bool t_=m_value==t_box->m_value;
	return t_;
}
c_BoolObject* c_BoolObject::m_new2(){
	DBG_ENTER("BoolObject.new")
	c_BoolObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<7>");
	return this;
}
void c_BoolObject::mark(){
	Object::mark();
}
String c_BoolObject::debug(){
	String t="(BoolObject)\n";
	t+=dbg_decl("value",&m_value);
	return t;
}
c_IntObject::c_IntObject(){
	m_value=0;
}
c_IntObject* c_IntObject::m_new(int t_value){
	DBG_ENTER("IntObject.new")
	c_IntObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<27>");
	this->m_value=t_value;
	return this;
}
c_IntObject* c_IntObject::m_new2(Float t_value){
	DBG_ENTER("IntObject.new")
	c_IntObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<31>");
	this->m_value=int(t_value);
	return this;
}
int c_IntObject::p_ToInt(){
	DBG_ENTER("IntObject.ToInt")
	c_IntObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<35>");
	return m_value;
}
Float c_IntObject::p_ToFloat(){
	DBG_ENTER("IntObject.ToFloat")
	c_IntObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<39>");
	Float t_=Float(m_value);
	return t_;
}
String c_IntObject::p_ToString(){
	DBG_ENTER("IntObject.ToString")
	c_IntObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<43>");
	String t_=String(m_value);
	return t_;
}
bool c_IntObject::p_Equals2(c_IntObject* t_box){
	DBG_ENTER("IntObject.Equals")
	c_IntObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<47>");
	bool t_=m_value==t_box->m_value;
	return t_;
}
int c_IntObject::p_Compare(c_IntObject* t_box){
	DBG_ENTER("IntObject.Compare")
	c_IntObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<51>");
	int t_=m_value-t_box->m_value;
	return t_;
}
c_IntObject* c_IntObject::m_new3(){
	DBG_ENTER("IntObject.new")
	c_IntObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<23>");
	return this;
}
void c_IntObject::mark(){
	Object::mark();
}
String c_IntObject::debug(){
	String t="(IntObject)\n";
	t+=dbg_decl("value",&m_value);
	return t;
}
c_FloatObject::c_FloatObject(){
	m_value=FLOAT(.0);
}
c_FloatObject* c_FloatObject::m_new(int t_value){
	DBG_ENTER("FloatObject.new")
	c_FloatObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<59>");
	this->m_value=Float(t_value);
	return this;
}
c_FloatObject* c_FloatObject::m_new2(Float t_value){
	DBG_ENTER("FloatObject.new")
	c_FloatObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<63>");
	this->m_value=t_value;
	return this;
}
int c_FloatObject::p_ToInt(){
	DBG_ENTER("FloatObject.ToInt")
	c_FloatObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<67>");
	int t_=int(m_value);
	return t_;
}
Float c_FloatObject::p_ToFloat(){
	DBG_ENTER("FloatObject.ToFloat")
	c_FloatObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<71>");
	return m_value;
}
String c_FloatObject::p_ToString(){
	DBG_ENTER("FloatObject.ToString")
	c_FloatObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<75>");
	String t_=String(m_value);
	return t_;
}
bool c_FloatObject::p_Equals3(c_FloatObject* t_box){
	DBG_ENTER("FloatObject.Equals")
	c_FloatObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<79>");
	bool t_=m_value==t_box->m_value;
	return t_;
}
int c_FloatObject::p_Compare2(c_FloatObject* t_box){
	DBG_ENTER("FloatObject.Compare")
	c_FloatObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<83>");
	if(m_value<t_box->m_value){
		DBG_BLOCK();
		return -1;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<84>");
	int t_=((m_value>t_box->m_value)?1:0);
	return t_;
}
c_FloatObject* c_FloatObject::m_new3(){
	DBG_ENTER("FloatObject.new")
	c_FloatObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<55>");
	return this;
}
void c_FloatObject::mark(){
	Object::mark();
}
String c_FloatObject::debug(){
	String t="(FloatObject)\n";
	t+=dbg_decl("value",&m_value);
	return t;
}
c_StringObject::c_StringObject(){
	m_value=String();
}
c_StringObject* c_StringObject::m_new(int t_value){
	DBG_ENTER("StringObject.new")
	c_StringObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<92>");
	this->m_value=String(t_value);
	return this;
}
c_StringObject* c_StringObject::m_new2(Float t_value){
	DBG_ENTER("StringObject.new")
	c_StringObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<96>");
	this->m_value=String(t_value);
	return this;
}
c_StringObject* c_StringObject::m_new3(String t_value){
	DBG_ENTER("StringObject.new")
	c_StringObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<100>");
	this->m_value=t_value;
	return this;
}
String c_StringObject::p_ToString(){
	DBG_ENTER("StringObject.ToString")
	c_StringObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<104>");
	return m_value;
}
bool c_StringObject::p_Equals4(c_StringObject* t_box){
	DBG_ENTER("StringObject.Equals")
	c_StringObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<108>");
	bool t_=m_value==t_box->m_value;
	return t_;
}
int c_StringObject::p_Compare3(c_StringObject* t_box){
	DBG_ENTER("StringObject.Compare")
	c_StringObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<112>");
	int t_=m_value.Compare(t_box->m_value);
	return t_;
}
c_StringObject* c_StringObject::m_new4(){
	DBG_ENTER("StringObject.new")
	c_StringObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<88>");
	return this;
}
void c_StringObject::mark(){
	Object::mark();
}
String c_StringObject::debug(){
	String t="(StringObject)\n";
	t+=dbg_decl("value",&m_value);
	return t;
}
Object* bb_boxes_BoxBool(bool t_value){
	DBG_ENTER("BoxBool")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<139>");
	Object* t_=((new c_BoolObject)->m_new(t_value));
	return t_;
}
Object* bb_boxes_BoxInt(int t_value){
	DBG_ENTER("BoxInt")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<143>");
	Object* t_=((new c_IntObject)->m_new(t_value));
	return t_;
}
Object* bb_boxes_BoxFloat(Float t_value){
	DBG_ENTER("BoxFloat")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<147>");
	Object* t_=((new c_FloatObject)->m_new2(t_value));
	return t_;
}
Object* bb_boxes_BoxString(String t_value){
	DBG_ENTER("BoxString")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<151>");
	Object* t_=((new c_StringObject)->m_new3(t_value));
	return t_;
}
bool bb_boxes_UnboxBool(Object* t_box){
	DBG_ENTER("UnboxBool")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<155>");
	bool t_=dynamic_cast<c_BoolObject*>(t_box)->m_value;
	return t_;
}
int bb_boxes_UnboxInt(Object* t_box){
	DBG_ENTER("UnboxInt")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<159>");
	int t_=dynamic_cast<c_IntObject*>(t_box)->m_value;
	return t_;
}
Float bb_boxes_UnboxFloat(Object* t_box){
	DBG_ENTER("UnboxFloat")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<163>");
	Float t_=dynamic_cast<c_FloatObject*>(t_box)->m_value;
	return t_;
}
String bb_boxes_UnboxString(Object* t_box){
	DBG_ENTER("UnboxString")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<167>");
	String t_=dynamic_cast<c_StringObject*>(t_box)->m_value;
	return t_;
}
c_AmazonAds::c_AmazonAds(){
}
void c_AmazonAds::p_PatchReceiver(c_App* t_app){
	DBG_ENTER("AmazonAds.PatchReceiver")
	c_AmazonAds *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<30>");
	String t_match=String(L"</application>",14);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<31>");
	String t_patchStr=String(L"<activity android:name=\"com.amazon.device.ads.AdActivity\" android:configChanges=\"keyboardHidden|orientation|screenSize\"/>",121);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<32>");
	c_File* t_target=c_Android::m_GetManifest();
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<34>");
	if(t_target->p_Contains(String(L"com.amazon.device.ads.AdActivity",32))){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<38>");
	if(!t_target->p_Contains(t_match)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<39>");
		t_app->p_LogWarning(String(L"Unable to add required activity to AndroidManifest.xml",54));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<40>");
		t_app->p_LogWarning(String(L"Please add the following activity manually:",43));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<41>");
		t_app->p_LogWarning(String(L"    ",4)+t_patchStr);
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<43>");
		t_target->p_InsertBefore(t_match,t_patchStr);
	}
}
void c_AmazonAds::p_CopyLibs(c_App* t_app){
	DBG_ENTER("AmazonAds.CopyLibs")
	c_AmazonAds *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<48>");
	c_Android::m_EnsureLibsFolder();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<50>");
	c_File* t_src=t_app->p_SourceFile(String(L"amazon-ads-5.1.14.jar",21));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<51>");
	c_File* t_dst=t_app->p_TargetFile(String(L"libs/amazon-ads-5.1.14.jar",26));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<53>");
	t_src->p_CopyTo2(t_dst);
}
void c_AmazonAds::p_PatchLayout(c_App* t_app){
	DBG_ENTER("AmazonAds.PatchLayout")
	c_AmazonAds *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<57>");
	String t_match=String(L"android:id=\"@+id/amazonAdsView\"",31);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<58>");
	c_File* t_target=t_app->p_TargetFile(String(L"templates/res/layout/main.xml",29));
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<60>");
	if(t_target->p_Contains(t_match)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<62>");
	String t_topSearch=String(L"<?xml version=\"1.0\" encoding=\"utf-8\"?>",38);
	DBG_LOCAL(t_topSearch,"topSearch")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<68>");
	String t_topValue=String(L"<FrameLayout\n\tandroid:id=\"@+id/mainframe\"\n\tandroid:layout_width=\"fill_parent\"\n\txmlns:android=\"http://schemas.android.com/apk/res/android\"\n\tandroid:layout_height=\"fill_parent\" >",176);
	DBG_LOCAL(t_topValue,"topValue")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<70>");
	String t_bottomSearch=String(L"</LinearLayout>",15);
	DBG_LOCAL(t_bottomSearch,"bottomSearch")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<82>");
	String t_bottomValue=String(L"\t<RelativeLayout\n\t\t\tandroid:layout_width=\"fill_parent\"\n\t\t\tandroid:layout_height=\"fill_parent\" >\n\t\t<LinearLayout\n\t\t\tandroid:id=\"@+id/amazonAdsView\"\n\t\t\tandroid:layout_width=\"match_parent\"\n\t\t\tandroid:layout_height=\"wrap_content\"\n\t\t\tandroid:layout_alignParentBottom=\"true\" >\n\t\t</LinearLayout>\n\t</RelativeLayout>\n</FrameLayout>\n",323);
	DBG_LOCAL(t_bottomValue,"bottomValue")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<84>");
	if(t_target->p_Contains(t_topSearch) && t_target->p_Contains(t_bottomSearch)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<85>");
		t_target->p_InsertAfter(t_topSearch,t_topValue);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<86>");
		t_target->p_InsertAfter(t_bottomSearch,t_bottomValue);
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<88>");
		t_app->p_LogWarning(String(L"Unable to add AmazonAds layout elements",39));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<89>");
		t_app->p_LogWarning(String(L"A layout with @id/amazonAdsView is required!",44));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<91>");
		t_app->p_LogWarning(String(L"I've tried to add this:\n",24)+t_topValue+String(L"\n",1)+String(L"%MONKEY_VIEW%\n",14)+t_bottomValue);
	}
}
void c_AmazonAds::p_Run(c_App* t_app){
	DBG_ENTER("AmazonAds.Run")
	c_AmazonAds *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<17>");
	c_Android::m_AddPermission(String(L"android.permission.ACCESS_COARSE_LOCATION",41));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<18>");
	c_Android::m_AddPermission(String(L"android.permission.ACCESS_FINE_LOCATION",39));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<19>");
	c_Android::m_AddPermission(String(L"android.permission.ACCESS_NETWORK_STATE",39));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<20>");
	c_Android::m_AddPermission(String(L"android.permission.ACCESS_WIFI_STATE",36));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<21>");
	c_Android::m_AddPermission(String(L"android.permission.INTERNET",27));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<22>");
	p_PatchReceiver(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<23>");
	p_CopyLibs(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<24>");
	p_PatchLayout(t_app);
}
c_AmazonAds* c_AmazonAds::m_new(){
	DBG_ENTER("AmazonAds.new")
	c_AmazonAds *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonads.monkey<13>");
	return this;
}
void c_AmazonAds::mark(){
	Object::mark();
}
String c_AmazonAds::debug(){
	String t="(AmazonAds)\n";
	return t;
}
c_App::c_App(){
	m_openFiles=(new c_StringMap)->m_new();
	m_commands=(new c_StringMap2)->m_new();
}
String c_App::p_GetTargetDir(){
	DBG_ENTER("App.GetTargetDir")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<160>");
	String t_=AppArgs().At(2)+String(L"/",1);
	return t_;
}
c_File* c_App::p_TargetFile(String t_filename){
	DBG_ENTER("App.TargetFile")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_filename,"filename")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<44>");
	if(!m_openFiles->p_Contains(t_filename)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<45>");
		m_openFiles->p_Set2(t_filename,(new c_File)->m_new(p_GetTargetDir()+t_filename));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<48>");
	c_File* t_=m_openFiles->p_Get2(t_filename);
	return t_;
}
void c_App::p_LogWarning(String t_text){
	DBG_ENTER("App.LogWarning")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<78>");
	bbPrint(String(L"WARN: ",6)+t_text);
}
c_Dir* c_App::p_TargetDir(String t_path){
	DBG_ENTER("App.TargetDir")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<56>");
	c_Dir* t_=(new c_Dir)->m_new(p_GetTargetDir()+t_path);
	return t_;
}
String c_App::p_GetCommandRaw(){
	DBG_ENTER("App.GetCommandRaw")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<156>");
	String t_=AppArgs().At(1);
	return t_;
}
String c_App::p_GetCommandDataDir(){
	DBG_ENTER("App.GetCommandDataDir")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<164>");
	String t_baseDir=RealPath(bb_os_ExtractDir(AppPath())+String(L"/../../",7))+String(L"/",1);
	DBG_LOCAL(t_baseDir,"baseDir")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<165>");
	String t_dataDir=t_baseDir+String(L"wizard.data/commands/",21);
	DBG_LOCAL(t_dataDir,"dataDir")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<166>");
	String t_=t_dataDir+p_GetCommandRaw().ToLower()+String(L"/",1);
	return t_;
}
c_File* c_App::p_SourceFile(String t_path){
	DBG_ENTER("App.SourceFile")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<52>");
	c_File* t_=(new c_File)->m_new(p_GetCommandDataDir()+t_path);
	return t_;
}
void c_App::p_LogInfo(String t_text){
	DBG_ENTER("App.LogInfo")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<74>");
	bbPrint(String(L"INFO: ",6)+t_text);
}
Array<String > c_App::p_GetAdditionArguments(){
	DBG_ENTER("App.GetAdditionArguments")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<87>");
	Array<String > t_=AppArgs().Slice(3);
	return t_;
}
void c_App::p_LogError(String t_text){
	DBG_ENTER("App.LogError")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<82>");
	bbPrint(String(L"ERRO: ",6)+t_text);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<83>");
	ExitApp(2);
}
c_Dir* c_App::p_SourceDir(String t_path){
	DBG_ENTER("App.SourceDir")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<60>");
	c_Dir* t_=(new c_Dir)->m_new(p_GetCommandDataDir()+t_path);
	return t_;
}
String c_App::p_GetShortName(String t_longName){
	DBG_ENTER("App.GetShortName")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_longName,"longName")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<129>");
	String t_lastPart=String();
	DBG_LOCAL(t_lastPart,"lastPart")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<130>");
	Array<String > t_=t_longName.Split(String(L".",1));
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		String t_parts=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_parts,"parts")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<131>");
		t_lastPart=t_parts;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<133>");
	return t_lastPart;
}
void c_App::p_LoadPatchCommands(){
	DBG_ENTER("App.LoadPatchCommands")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<121>");
	Array<c_ClassInfo* > t_=bb_reflection_GetClasses();
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_ClassInfo* t_classInfo=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_classInfo,"classInfo")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<122>");
		if(t_classInfo->p_Name().Contains(String(L"wizard.commands.",16))){
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<123>");
			m_commands->p_Add(p_GetShortName(t_classInfo->p_Name()),t_classInfo);
		}
	}
}
void c_App::p_PrintHelp(){
	DBG_ENTER("App.PrintHelp")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<64>");
	bbPrint(String(L"Usage: wizard COMMAND TARGETDIR [COMMAND SPECIFIC OPTIONS]",58));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<65>");
	bbPrint(String());
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<67>");
	bbPrint(String(L"Commands:",9));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<68>");
	c_KeyEnumerator* t_=m_commands->p_Keys()->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		String t_command=t_->p_NextObject();
		DBG_LOCAL(t_command,"command")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<69>");
		bbPrint(String(L"  ",2)+t_command);
	}
}
void c_App::p_CheckNumberOfArguments(){
	DBG_ENTER("App.CheckNumberOfArguments")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<137>");
	if(AppArgs().Length()<=2){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<138>");
		p_PrintHelp();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<139>");
		bbPrint(String());
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<140>");
		p_LogError(String(L"Invalid number of arguments",27));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<141>");
		ExitApp(2);
	}
}
void c_App::p_CheckTargetDirExists(){
	DBG_ENTER("App.CheckTargetDirExists")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<146>");
	if(!bb_helperos_DirExists(p_GetTargetDir())){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<147>");
		p_LogError(String(L"Given targetdir ",16)+p_GetTargetDir()+String(L" does not exists",16));
	}
}
String c_App::p_FixCase(String t_command){
	DBG_ENTER("App.FixCase")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_command,"command")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<99>");
	c_KeyEnumerator* t_=m_commands->p_Keys()->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		String t_checkCommand=t_->p_NextObject();
		DBG_LOCAL(t_checkCommand,"checkCommand")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<100>");
		if(t_checkCommand.ToLower()==t_command.ToLower()){
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<101>");
			return t_checkCommand;
		}
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<105>");
	return String();
}
String c_App::p_GetCommand(){
	DBG_ENTER("App.GetCommand")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<152>");
	String t_=p_FixCase(p_GetCommandRaw());
	return t_;
}
void c_App::p_ExecuteCommand(String t_command){
	DBG_ENTER("App.ExecuteCommand")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_command,"command")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<115>");
	c_ClassInfo* t_info=m_commands->p_Get2(t_command);
	DBG_LOCAL(t_info,"info")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<116>");
	c_Command* t_obj=dynamic_cast<c_Command*>(t_info->p_NewInstance());
	DBG_LOCAL(t_obj,"obj")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<117>");
	t_obj->p_Run(this);
}
void c_App::p_SaveOpenFiles(){
	DBG_ENTER("App.SaveOpenFiles")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<93>");
	c_ValueEnumerator* t_=m_openFiles->p_Values()->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_File* t_f=t_->p_NextObject();
		DBG_LOCAL(t_f,"f")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<94>");
		t_f->p_Save();
	}
}
void c_App::p_PrintInvalidCommandError(String t_command){
	DBG_ENTER("App.PrintInvalidCommandError")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_command,"command")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<109>");
	p_PrintHelp();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<110>");
	bbPrint(String());
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<111>");
	p_LogError(t_command+String(L" is not a avalid command",24));
}
c_App* c_App::m_new(){
	DBG_ENTER("App.new")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<28>");
	p_LoadPatchCommands();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<29>");
	p_CheckNumberOfArguments();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<30>");
	p_CheckTargetDirExists();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<32>");
	if((p_GetCommand()).Length()!=0){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<33>");
		gc_assign(c_Ios::m_app,this);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<34>");
		gc_assign(c_Android::m_app,this);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<36>");
		p_ExecuteCommand(p_GetCommand());
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<37>");
		p_SaveOpenFiles();
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/app.monkey<39>");
		p_PrintInvalidCommandError(p_GetCommandRaw());
	}
	return this;
}
void c_App::mark(){
	Object::mark();
	gc_mark_q(m_openFiles);
	gc_mark_q(m_commands);
}
String c_App::debug(){
	String t="(App)\n";
	t+=dbg_decl("commands",&m_commands);
	t+=dbg_decl("openFiles",&m_openFiles);
	return t;
}
c_Android::c_Android(){
}
c_App* c_Android::m_app;
c_File* c_Android::m_GetManifest(){
	DBG_ENTER("Android.GetManifest")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<14>");
	c_File* t_=m_app->p_TargetFile(String(L"templates/AndroidManifest.xml",29));
	return t_;
}
void c_Android::m_AddPermission(String t_permission){
	DBG_ENTER("Android.AddPermission")
	DBG_LOCAL(t_permission,"permission")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<18>");
	String t_addBefore=String(L"<application",12);
	DBG_LOCAL(t_addBefore,"addBefore")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<19>");
	String t_searchStr=String(L"uses-permission android:name=\"",30)+t_permission+String(L"\"",1);
	DBG_LOCAL(t_searchStr,"searchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<20>");
	String t_patchStr=String(L"<",1)+t_searchStr+String(L" />",3);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<21>");
	c_File* t_target=m_GetManifest();
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<23>");
	if(t_target->p_Contains(t_searchStr)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<25>");
	if(!t_target->p_Contains(t_addBefore)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<26>");
		m_app->p_LogWarning(String(L"Unable to add required permission to AndroidManifest.xml",56));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<27>");
		m_app->p_LogWarning(String(L"Please add the following permission manually:",45));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<28>");
		m_app->p_LogWarning(String(L"    ",4)+t_patchStr);
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<30>");
		t_target->p_InsertBefore(t_addBefore,t_patchStr);
	}
}
void c_Android::m_EnsureLibsFolder(){
	DBG_ENTER("Android.EnsureLibsFolder")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/android.monkey<35>");
	m_app->p_TargetDir(String(L"libs",4))->p_Create();
}
void c_Android::mark(){
	Object::mark();
}
String c_Android::debug(){
	String t="(Android)\n";
	t+=dbg_decl("app",&c_Android::m_app);
	return t;
}
c_File::c_File(){
	m_path=String();
	m_loaded=false;
	m__data=String();
	m_dirty=false;
}
c_File* c_File::m_new(String t_path){
	DBG_ENTER("File.new")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<21>");
	this->m_path=t_path;
	return this;
}
c_File* c_File::m_new2(){
	DBG_ENTER("File.new")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<10>");
	return this;
}
String c_File::p_data(){
	DBG_ENTER("File.data")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<180>");
	if(!m_loaded && bb_helperos_FileExists(m_path)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<181>");
		m_loaded=true;
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<182>");
		m__data=LoadString(m_path);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<185>");
	return m__data;
}
void c_File::p_data2(String t_newData){
	DBG_ENTER("File.data")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_newData,"newData")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<189>");
	m_dirty=true;
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<190>");
	m__data=t_newData;
}
bool c_File::p_Contains(String t_match){
	DBG_ENTER("File.Contains")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<56>");
	bool t_=p_data().Contains(t_match);
	return t_;
}
void c_File::p_Replace(String t_match,String t_text){
	DBG_ENTER("File.Replace")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_match,"match")
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<108>");
	if(!p_Contains(t_match)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<109>");
		bbError(String(L"Unable to find '",16)+t_match+String(L"' within ",9)+m_path);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<112>");
	p_data2(p_data().Replace(t_match,t_text));
}
void c_File::p_InsertBefore(String t_match,String t_text){
	DBG_ENTER("File.InsertBefore")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_match,"match")
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<120>");
	p_Replace(t_match,t_text+String(L"\n",1)+t_match);
}
String c_File::p_GetPath(){
	DBG_ENTER("File.GetPath")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<90>");
	return m_path;
}
void c_File::p_CopyTo(String t_dst){
	DBG_ENTER("File.CopyTo")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<103>");
	if(!bb_helperos_FileExists(m_path)){
		DBG_BLOCK();
		bbError(String(L"File doesnt exists: ",20)+m_path);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<104>");
	CopyFile(m_path,t_dst);
}
void c_File::p_CopyTo2(c_File* t_dst){
	DBG_ENTER("File.CopyTo")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<99>");
	p_CopyTo(t_dst->p_GetPath());
}
void c_File::p_InsertAfter(String t_match,String t_text){
	DBG_ENTER("File.InsertAfter")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_match,"match")
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<116>");
	p_Replace(t_match,t_match+String(L"\n",1)+t_text);
}
void c_File::p_Append(String t_text){
	DBG_ENTER("File.Append")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<174>");
	p_data2(p_data()+t_text);
}
bool c_File::p_Exists(){
	DBG_ENTER("File.Exists")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<82>");
	bool t_=bb_helperos_FileExists(m_path);
	return t_;
}
String c_File::p_GetContentBetween(String t_strStart,String t_strEnd){
	DBG_ENTER("File.GetContentBetween")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_strStart,"strStart")
	DBG_LOCAL(t_strEnd,"strEnd")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<72>");
	int t_posStart=p_data().Find(t_strStart,0);
	DBG_LOCAL(t_posStart,"posStart")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<73>");
	int t_posEnd=p_data().Find(t_strEnd,t_posStart);
	DBG_LOCAL(t_posEnd,"posEnd")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<75>");
	if(t_posStart==-1 || t_posEnd==-1){
		DBG_BLOCK();
		return String();
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<76>");
	t_posEnd+=t_strEnd.Length()+1;
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<78>");
	String t_=p_data().Slice(t_posStart,t_posEnd);
	return t_;
}
String c_File::p_Get(){
	DBG_ENTER("File.Get")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<170>");
	String t_=p_data();
	return t_;
}
Array<int > c_File::p_FindLines(String t_match){
	DBG_ENTER("File.FindLines")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<36>");
	Array<String > t_lines=p_data().Split(String(L"\n",1));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<37>");
	c_IntList* t_result=(new c_IntList)->m_new2();
	DBG_LOCAL(t_result,"result")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<39>");
	int t_i=1;
	DBG_LOCAL(t_i,"i")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<40>");
	Array<String > t_=t_lines;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		String t_line=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_line,"line")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<41>");
		if(t_line.Contains(t_match)){
			DBG_BLOCK();
			t_result->p_AddLast2(t_i);
		}
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<42>");
		t_i+=1;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<45>");
	Array<int > t_3=t_result->p_ToArray();
	return t_3;
}
void c_File::p_ReplaceLine(int t_line,String t_text){
	DBG_ENTER("File.ReplaceLine")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_line,"line")
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<124>");
	Array<String > t_dataArr=p_data().Split(String(L"\n",1));
	DBG_LOCAL(t_dataArr,"dataArr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<125>");
	t_dataArr.At(t_line-1)=t_text;
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<126>");
	p_data2(String(L"\n",1).Join(t_dataArr));
}
String c_File::p_GetLine(int t_line){
	DBG_ENTER("File.GetLine")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_line,"line")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<32>");
	String t_=p_data().Split(String(L"\n",1)).At(t_line-1);
	return t_;
}
void c_File::p_RemoveLine(int t_line){
	DBG_ENTER("File.RemoveLine")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_line,"line")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<49>");
	Array<String > t_dataArr=p_data().Split(String(L"\n",1));
	DBG_LOCAL(t_dataArr,"dataArr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<52>");
	p_data2(String(L"\n",1).Join(t_dataArr.Slice(0,t_line-1))+String(L"\n",1)+String(L"\n",1).Join(t_dataArr.Slice(t_line)));
}
void c_File::p_Save(){
	DBG_ENTER("File.Save")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<25>");
	if(m_dirty){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<26>");
		m_dirty=false;
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<27>");
		SaveString(p_data(),m_path);
	}
}
void c_File::p_InsertAfterLine(int t_line,String t_text){
	DBG_ENTER("File.InsertAfterLine")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_line,"line")
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<130>");
	p_ReplaceLine(t_line,p_GetLine(t_line)+String(L"\n",1)+t_text);
}
bool c_File::p_ContainsBetween(String t_match,String t_strStart,String t_strEnd){
	DBG_ENTER("File.ContainsBetween")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_match,"match")
	DBG_LOCAL(t_strStart,"strStart")
	DBG_LOCAL(t_strEnd,"strEnd")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<60>");
	int t_posStart=p_data().Find(t_strStart,0);
	DBG_LOCAL(t_posStart,"posStart")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<61>");
	int t_posEnd=p_data().Find(t_strEnd,t_posStart);
	DBG_LOCAL(t_posEnd,"posEnd")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<63>");
	if(t_posStart==-1 || t_posEnd==-1){
		DBG_BLOCK();
		return false;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<66>");
	t_posEnd+=t_strEnd.Length()+1;
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<68>");
	bool t_=p_data().Slice(t_posStart,t_posEnd).Contains(t_match);
	return t_;
}
void c_File::p_ReplaceBetween(String t_match,String t_text,String t_strStart,String t_strEnd){
	DBG_ENTER("File.ReplaceBetween")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_match,"match")
	DBG_LOCAL(t_text,"text")
	DBG_LOCAL(t_strStart,"strStart")
	DBG_LOCAL(t_strEnd,"strEnd")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<138>");
	if(!p_ContainsBetween(t_match,t_strStart,t_strEnd)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<142>");
		bbError(String(L"Unable to find '",16)+t_match+String(L"' within ",9)+m_path+String(L"\n",1)+String(L"and the given range that:\n",26)+String(L"starts with: ",13)+t_strStart+String(L"\n",1)+String(L"  ends with: ",13)+t_strEnd);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<145>");
	int t_posStart=p_data().Find(t_strStart,0);
	DBG_LOCAL(t_posStart,"posStart")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<146>");
	int t_posEnd=p_data().Find(t_strEnd,t_posStart);
	DBG_LOCAL(t_posEnd,"posEnd")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<149>");
	t_posEnd+=t_strEnd.Length()+1;
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<151>");
	String t_replaceData=p_data().Slice(t_posStart,t_posEnd);
	DBG_LOCAL(t_replaceData,"replaceData")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<152>");
	t_replaceData=t_replaceData.Replace(t_match,t_text);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<154>");
	p_data2(p_data().Slice(0,t_posStart)+t_replaceData+p_data().Slice(t_posEnd));
}
void c_File::p_InsertAfterBetween(String t_match,String t_text,String t_strStart,String t_strEnd){
	DBG_ENTER("File.InsertAfterBetween")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_match,"match")
	DBG_LOCAL(t_text,"text")
	DBG_LOCAL(t_strStart,"strStart")
	DBG_LOCAL(t_strEnd,"strEnd")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<158>");
	p_ReplaceBetween(t_match,t_match+String(L"\n",1)+t_text,t_strStart,t_strEnd);
}
void c_File::p_Remove(){
	DBG_ENTER("File.Remove")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<86>");
	DeleteFile(m_path);
}
void c_File::p_Set(String t_text){
	DBG_ENTER("File.Set")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<166>");
	p_data2(t_text);
}
String c_File::p_GetBasename(){
	DBG_ENTER("File.GetBasename")
	c_File *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<94>");
	Array<String > t_parts=p_GetPath().Split(String(L"/",1));
	DBG_LOCAL(t_parts,"parts")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/file.monkey<95>");
	String t_=t_parts.At(t_parts.Length()-1);
	return t_;
}
void c_File::mark(){
	Object::mark();
}
String c_File::debug(){
	String t="(File)\n";
	t+=dbg_decl("path",&m_path);
	t+=dbg_decl("_data",&m__data);
	t+=dbg_decl("dirty",&m_dirty);
	t+=dbg_decl("loaded",&m_loaded);
	return t;
}
c_Map::c_Map(){
	m_root=0;
}
c_Map* c_Map::m_new(){
	DBG_ENTER("Map.new")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<7>");
	return this;
}
c_Node* c_Map::p_FindNode(String t_key){
	DBG_ENTER("Map.FindNode")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<157>");
	c_Node* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<159>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<160>");
		int t_cmp=p_Compare4(t_key,t_node->m_key);
		DBG_LOCAL(t_cmp,"cmp")
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<161>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<162>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<163>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<164>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<166>");
				return t_node;
			}
		}
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<169>");
	return t_node;
}
bool c_Map::p_Contains(String t_key){
	DBG_ENTER("Map.Contains")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<25>");
	bool t_=p_FindNode(t_key)!=0;
	return t_;
}
int c_Map::p_RotateLeft(c_Node* t_node){
	DBG_ENTER("Map.RotateLeft")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<251>");
	c_Node* t_child=t_node->m_right;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<252>");
	gc_assign(t_node->m_right,t_child->m_left);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<253>");
	if((t_child->m_left)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<254>");
		gc_assign(t_child->m_left->m_parent,t_node);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<256>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<257>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<258>");
		if(t_node==t_node->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<259>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<261>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<264>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<266>");
	gc_assign(t_child->m_left,t_node);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<267>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map::p_RotateRight(c_Node* t_node){
	DBG_ENTER("Map.RotateRight")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<271>");
	c_Node* t_child=t_node->m_left;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<272>");
	gc_assign(t_node->m_left,t_child->m_right);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<273>");
	if((t_child->m_right)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<274>");
		gc_assign(t_child->m_right->m_parent,t_node);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<276>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<277>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<278>");
		if(t_node==t_node->m_parent->m_right){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<279>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<281>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<284>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<286>");
	gc_assign(t_child->m_right,t_node);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<287>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map::p_InsertFixup(c_Node* t_node){
	DBG_ENTER("Map.InsertFixup")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<212>");
	while(((t_node->m_parent)!=0) && t_node->m_parent->m_color==-1 && ((t_node->m_parent->m_parent)!=0)){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<213>");
		if(t_node->m_parent==t_node->m_parent->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<214>");
			c_Node* t_uncle=t_node->m_parent->m_parent->m_right;
			DBG_LOCAL(t_uncle,"uncle")
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<215>");
			if(((t_uncle)!=0) && t_uncle->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<216>");
				t_node->m_parent->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<217>");
				t_uncle->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<218>");
				t_uncle->m_parent->m_color=-1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<219>");
				t_node=t_uncle->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<221>");
				if(t_node==t_node->m_parent->m_right){
					DBG_BLOCK();
					DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<222>");
					t_node=t_node->m_parent;
					DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<223>");
					p_RotateLeft(t_node);
				}
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<225>");
				t_node->m_parent->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<226>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<227>");
				p_RotateRight(t_node->m_parent->m_parent);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<230>");
			c_Node* t_uncle2=t_node->m_parent->m_parent->m_left;
			DBG_LOCAL(t_uncle2,"uncle")
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<231>");
			if(((t_uncle2)!=0) && t_uncle2->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<232>");
				t_node->m_parent->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<233>");
				t_uncle2->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<234>");
				t_uncle2->m_parent->m_color=-1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<235>");
				t_node=t_uncle2->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<237>");
				if(t_node==t_node->m_parent->m_left){
					DBG_BLOCK();
					DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<238>");
					t_node=t_node->m_parent;
					DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<239>");
					p_RotateRight(t_node);
				}
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<241>");
				t_node->m_parent->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<242>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<243>");
				p_RotateLeft(t_node->m_parent->m_parent);
			}
		}
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<247>");
	m_root->m_color=1;
	return 0;
}
bool c_Map::p_Set2(String t_key,c_File* t_value){
	DBG_ENTER("Map.Set")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<29>");
	c_Node* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<30>");
	c_Node* t_parent=0;
	int t_cmp=0;
	DBG_LOCAL(t_parent,"parent")
	DBG_LOCAL(t_cmp,"cmp")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<32>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<33>");
		t_parent=t_node;
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<34>");
		t_cmp=p_Compare4(t_key,t_node->m_key);
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<35>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<36>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<37>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<38>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<40>");
				gc_assign(t_node->m_value,t_value);
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<41>");
				return false;
			}
		}
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<45>");
	t_node=(new c_Node)->m_new(t_key,t_value,-1,t_parent);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<47>");
	if((t_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<48>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<49>");
			gc_assign(t_parent->m_right,t_node);
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<51>");
			gc_assign(t_parent->m_left,t_node);
		}
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<53>");
		p_InsertFixup(t_node);
	}else{
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<55>");
		gc_assign(m_root,t_node);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<57>");
	return true;
}
c_File* c_Map::p_Get2(String t_key){
	DBG_ENTER("Map.Get")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<101>");
	c_Node* t_node=p_FindNode(t_key);
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<102>");
	if((t_node)!=0){
		DBG_BLOCK();
		return t_node->m_value;
	}
	return 0;
}
c_MapValues* c_Map::p_Values(){
	DBG_ENTER("Map.Values")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<117>");
	c_MapValues* t_=(new c_MapValues)->m_new(this);
	return t_;
}
c_Node* c_Map::p_FirstNode(){
	DBG_ENTER("Map.FirstNode")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<125>");
	if(!((m_root)!=0)){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<127>");
	c_Node* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<128>");
	while((t_node->m_left)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<129>");
		t_node=t_node->m_left;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<131>");
	return t_node;
}
void c_Map::mark(){
	Object::mark();
	gc_mark_q(m_root);
}
String c_Map::debug(){
	String t="(Map)\n";
	t+=dbg_decl("root",&m_root);
	return t;
}
c_StringMap::c_StringMap(){
}
c_StringMap* c_StringMap::m_new(){
	DBG_ENTER("StringMap.new")
	c_StringMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<551>");
	c_Map::m_new();
	return this;
}
int c_StringMap::p_Compare4(String t_lhs,String t_rhs){
	DBG_ENTER("StringMap.Compare")
	c_StringMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<554>");
	int t_=t_lhs.Compare(t_rhs);
	return t_;
}
void c_StringMap::mark(){
	c_Map::mark();
}
String c_StringMap::debug(){
	String t="(StringMap)\n";
	t=c_Map::debug()+t;
	return t;
}
c_Node::c_Node(){
	m_key=String();
	m_right=0;
	m_left=0;
	m_value=0;
	m_color=0;
	m_parent=0;
}
c_Node* c_Node::m_new(String t_key,c_File* t_value,int t_color,c_Node* t_parent){
	DBG_ENTER("Node.new")
	c_Node *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_color,"color")
	DBG_LOCAL(t_parent,"parent")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<364>");
	this->m_key=t_key;
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<365>");
	gc_assign(this->m_value,t_value);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<366>");
	this->m_color=t_color;
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<367>");
	gc_assign(this->m_parent,t_parent);
	return this;
}
c_Node* c_Node::m_new2(){
	DBG_ENTER("Node.new")
	c_Node *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<361>");
	return this;
}
c_Node* c_Node::p_NextNode(){
	DBG_ENTER("Node.NextNode")
	c_Node *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<385>");
	c_Node* t_node=0;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<386>");
	if((m_right)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<387>");
		t_node=m_right;
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<388>");
		while((t_node->m_left)!=0){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<389>");
			t_node=t_node->m_left;
		}
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<391>");
		return t_node;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<393>");
	t_node=this;
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<394>");
	c_Node* t_parent=this->m_parent;
	DBG_LOCAL(t_parent,"parent")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<395>");
	while(((t_parent)!=0) && t_node==t_parent->m_right){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<396>");
		t_node=t_parent;
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<397>");
		t_parent=t_parent->m_parent;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<399>");
	return t_parent;
}
void c_Node::mark(){
	Object::mark();
	gc_mark_q(m_right);
	gc_mark_q(m_left);
	gc_mark_q(m_value);
	gc_mark_q(m_parent);
}
String c_Node::debug(){
	String t="(Node)\n";
	t+=dbg_decl("key",&m_key);
	t+=dbg_decl("value",&m_value);
	t+=dbg_decl("color",&m_color);
	t+=dbg_decl("parent",&m_parent);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("right",&m_right);
	return t;
}
bool bb_helperos_FileExists(String t_name){
	DBG_ENTER("FileExists")
	DBG_LOCAL(t_name,"name")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/helperos.monkey<10>");
	if(FileType(t_name)==1){
		DBG_BLOCK();
		return true;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/helperos.monkey<11>");
	return false;
}
c_Dir::c_Dir(){
	m_path=String();
	m_parent=0;
}
c_Dir* c_Dir::m_new(String t_path){
	DBG_ENTER("Dir.new")
	c_Dir *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/dir.monkey<19>");
	this->m_path=RealPath(t_path);
	return this;
}
c_Dir* c_Dir::m_new2(){
	DBG_ENTER("Dir.new")
	c_Dir *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/dir.monkey<10>");
	return this;
}
bool c_Dir::p_Exists(){
	DBG_ENTER("Dir.Exists")
	c_Dir *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/dir.monkey<44>");
	bool t_=bb_helperos_DirExists(m_path);
	return t_;
}
void c_Dir::p_Create(){
	DBG_ENTER("Dir.Create")
	c_Dir *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/dir.monkey<36>");
	if(!p_Exists()){
		DBG_BLOCK();
		CreateDir(m_path);
	}
}
String c_Dir::p_GetPath(){
	DBG_ENTER("Dir.GetPath")
	c_Dir *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/dir.monkey<48>");
	String t_=m_path+String(L"/",1);
	return t_;
}
c_Dir* c_Dir::p_Parent(){
	DBG_ENTER("Dir.Parent")
	c_Dir *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/dir.monkey<28>");
	if(!((m_parent)!=0)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/dir.monkey<29>");
		gc_assign(m_parent,(new c_Dir)->m_new(bb_os_ExtractDir(m_path)));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/dir.monkey<32>");
	return m_parent;
}
void c_Dir::p_Remove2(bool t_recursive){
	DBG_ENTER("Dir.Remove")
	c_Dir *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_recursive,"recursive")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/dir.monkey<40>");
	bb_os_DeleteDir(m_path,t_recursive);
}
void c_Dir::p_CopyTo3(c_Dir* t_dstDir,bool t_recursive,bool t_hidden,bool t_remove){
	DBG_ENTER("Dir.CopyTo")
	c_Dir *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_dstDir,"dstDir")
	DBG_LOCAL(t_recursive,"recursive")
	DBG_LOCAL(t_hidden,"hidden")
	DBG_LOCAL(t_remove,"remove")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/dir.monkey<23>");
	if(t_remove && t_dstDir->p_Exists()){
		DBG_BLOCK();
		t_dstDir->p_Remove2(true);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/dir.monkey<24>");
	bb_os_CopyDir(m_path,t_dstDir->p_GetPath(),t_recursive,t_hidden);
}
void c_Dir::mark(){
	Object::mark();
	gc_mark_q(m_parent);
}
String c_Dir::debug(){
	String t="(Dir)\n";
	t+=dbg_decl("path",&m_path);
	t+=dbg_decl("parent",&m_parent);
	return t;
}
bool bb_helperos_DirExists(String t_name){
	DBG_ENTER("DirExists")
	DBG_LOCAL(t_name,"name")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/helperos.monkey<15>");
	if(FileType(t_name)==2){
		DBG_BLOCK();
		return true;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/helperos.monkey<16>");
	return false;
}
String bb_os_ExtractDir(String t_path){
	DBG_ENTER("ExtractDir")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<141>");
	int t_i=t_path.FindLast(String(L"/",1));
	DBG_LOCAL(t_i,"i")
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<142>");
	if(t_i==-1){
		DBG_BLOCK();
		t_i=t_path.FindLast(String(L"\\",1));
	}
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<143>");
	if(t_i!=-1){
		DBG_BLOCK();
		String t_=t_path.Slice(0,t_i);
		return t_;
	}
	return String();
}
c_AmazonPayment::c_AmazonPayment(){
}
void c_AmazonPayment::p_PatchReceiver(c_App* t_app){
	DBG_ENTER("AmazonPayment.PatchReceiver")
	c_AmazonPayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<26>");
	String t_match=String(L"</application>",14);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<33>");
	String t_patchStr=String(L"<receiver android:name=\"com.amazon.inapp.purchasing.ResponseReceiver\" >\n\t<intent-filter>\n\t<action android:name=\"com.amazon.inapp.purchasing.NOTIFY\"\n\t\tandroid:permission=\"com.amazon.inapp.purchasing.Permission.NOTIFY\" />\n\t</intent-filter></receiver>",248);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<34>");
	c_File* t_target=c_Android::m_GetManifest();
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<36>");
	if(t_target->p_Contains(String(L"com.amazon.inapp.purchasing.ResponseReceiver",44))){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<40>");
	if(!t_target->p_Contains(t_match)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<41>");
		t_app->p_LogWarning(String(L"Unable to add required activity to AndroidManifest.xml",54));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<42>");
		t_app->p_LogWarning(String(L"Please add the following activity manually:",43));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<43>");
		t_app->p_LogWarning(String(L"    ",4)+t_patchStr);
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<45>");
		t_target->p_InsertBefore(t_match,t_patchStr);
	}
}
void c_AmazonPayment::p_CopyLibs(c_App* t_app){
	DBG_ENTER("AmazonPayment.CopyLibs")
	c_AmazonPayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<50>");
	c_Android::m_EnsureLibsFolder();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<52>");
	c_File* t_src=t_app->p_SourceFile(String(L"in-app-purchasing-1.0.3.jar",27));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<53>");
	c_File* t_dst=t_app->p_TargetFile(String(L"libs/in-app-purchasing-1.0.3.jar",32));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<55>");
	t_src->p_CopyTo2(t_dst);
}
void c_AmazonPayment::p_PrintDeveloperHints(c_App* t_app){
	DBG_ENTER("AmazonPayment.PrintDeveloperHints")
	c_AmazonPayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<59>");
	c_File* t_tester=t_app->p_SourceFile(String(L"AmazonSDKTester.apk",19));
	DBG_LOCAL(t_tester,"tester")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<60>");
	c_File* t_json=t_app->p_SourceFile(String(L"amazon.sdktester.json",21));
	DBG_LOCAL(t_json,"json")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<62>");
	t_app->p_LogInfo(String(L"Monkey interface can be found here: http://goo.gl/JGfIm",55));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<63>");
	t_app->p_LogInfo(String());
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<64>");
	t_app->p_LogInfo(String(L"And if you want to test the IAP process please note this files:",63));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<65>");
	t_app->p_LogInfo(String(L"1) Amazon SDK Tester App",24));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<66>");
	t_app->p_LogInfo(String(L"   => ",6)+t_tester->p_GetPath());
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<67>");
	t_app->p_LogInfo(String(L"2) Example SDKTester JSON file",30));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<68>");
	t_app->p_LogInfo(String(L"   => ",6)+t_json->p_GetPath());
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<69>");
	t_app->p_LogInfo(String());
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<70>");
	t_app->p_LogInfo(String(L"Unsure how or where to push the json file?",42));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<71>");
	t_app->p_LogInfo(String(L"   => adb push amazon.sdktester.json /mnt/sdcard/amazon.sdktester.json",70));
}
void c_AmazonPayment::p_Run(c_App* t_app){
	DBG_ENTER("AmazonPayment.Run")
	c_AmazonPayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<17>");
	c_Android::m_AddPermission(String(L"android.permission.INTERNET",27));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<18>");
	p_PatchReceiver(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<19>");
	p_CopyLibs(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<20>");
	p_PrintDeveloperHints(t_app);
}
c_AmazonPayment* c_AmazonPayment::m_new(){
	DBG_ENTER("AmazonPayment.new")
	c_AmazonPayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/amazonpayment.monkey<13>");
	return this;
}
void c_AmazonPayment::mark(){
	Object::mark();
}
String c_AmazonPayment::debug(){
	String t="(AmazonPayment)\n";
	return t;
}
c_AndroidAntKey::c_AndroidAntKey(){
}
String c_AndroidAntKey::p_GetArgument(c_App* t_app,int t_idx,String t_name){
	DBG_ENTER("AndroidAntKey.GetArgument")
	c_AndroidAntKey *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_LOCAL(t_idx,"idx")
	DBG_LOCAL(t_name,"name")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidantkey.monkey<31>");
	if(t_app->p_GetAdditionArguments().Length()<t_idx){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidantkey.monkey<32>");
		t_app->p_LogError(String(t_idx)+String(L" argument ",10)+t_name+String(L" is missing",11));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidantkey.monkey<33>");
		return String();
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidantkey.monkey<36>");
	String t_=t_app->p_GetAdditionArguments().At(t_idx-1);
	return t_;
}
void c_AndroidAntKey::p_Run(c_App* t_app){
	DBG_ENTER("AndroidAntKey.Run")
	c_AndroidAntKey *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidantkey.monkey<15>");
	c_File* t_file=t_app->p_TargetFile(String(L"ant.properties",14));
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidantkey.monkey<18>");
	String t_[]={String(L"key.store",9),String(L"key.store.password",18),String(L"key.alias",9),String(L"key.alias.password",18)};
	Array<String > t_fields=Array<String >(t_,4);
	DBG_LOCAL(t_fields,"fields")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidantkey.monkey<20>");
	for(int t_i=1;t_i<=t_fields.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidantkey.monkey<21>");
		String t_name=t_fields.At(t_i-1);
		DBG_LOCAL(t_name,"name")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidantkey.monkey<22>");
		if(!t_file->p_Contains(t_name+String(L"=",1))){
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidantkey.monkey<23>");
			t_file->p_Append(t_name+String(L"=",1)+p_GetArgument(t_app,t_i,t_name)+String(L"\n",1));
		}
	}
}
c_AndroidAntKey* c_AndroidAntKey::m_new(){
	DBG_ENTER("AndroidAntKey.new")
	c_AndroidAntKey *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidantkey.monkey<13>");
	return this;
}
void c_AndroidAntKey::mark(){
	Object::mark();
}
String c_AndroidAntKey::debug(){
	String t="(AndroidAntKey)\n";
	return t;
}
c_AndroidBass::c_AndroidBass(){
}
void c_AndroidBass::p_CopyLibs(c_App* t_app){
	DBG_ENTER("AndroidBass.CopyLibs")
	c_AndroidBass *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidbass.monkey<22>");
	c_Android::m_EnsureLibsFolder();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidbass.monkey<24>");
	t_app->p_TargetDir(String(L"libs/armeabi",12))->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidbass.monkey<25>");
	t_app->p_SourceFile(String(L"libs/armeabi/libbass.so",23))->p_CopyTo2(t_app->p_TargetFile(String(L"libs/armeabi/libbass.so",23)));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidbass.monkey<27>");
	t_app->p_TargetDir(String(L"libs/armeabi-v7a",16))->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidbass.monkey<28>");
	t_app->p_SourceFile(String(L"libs/armeabi-v7a/libbass.so",27))->p_CopyTo2(t_app->p_TargetFile(String(L"libs/armeabi-v7a/libbass.so",27)));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidbass.monkey<30>");
	t_app->p_TargetDir(String(L"src/com/un4seen",15))->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidbass.monkey<31>");
	t_app->p_TargetDir(String(L"src/com/un4seen/bass",20))->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidbass.monkey<32>");
	t_app->p_SourceFile(String(L"src/com/un4seen/bass/BASS.java",30))->p_CopyTo2(t_app->p_TargetFile(String(L"src/com/un4seen/bass/BASS.java",30)));
}
void c_AndroidBass::p_Run(c_App* t_app){
	DBG_ENTER("AndroidBass.Run")
	c_AndroidBass *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidbass.monkey<16>");
	p_CopyLibs(t_app);
}
c_AndroidBass* c_AndroidBass::m_new(){
	DBG_ENTER("AndroidBass.new")
	c_AndroidBass *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidbass.monkey<13>");
	return this;
}
void c_AndroidBass::mark(){
	Object::mark();
}
String c_AndroidBass::debug(){
	String t="(AndroidBass)\n";
	return t;
}
c_AndroidChartboost::c_AndroidChartboost(){
}
void c_AndroidChartboost::p_CopyLibs(c_App* t_app){
	DBG_ENTER("AndroidChartboost.CopyLibs")
	c_AndroidChartboost *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidchartboost.monkey<28>");
	c_Android::m_EnsureLibsFolder();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidchartboost.monkey<30>");
	c_File* t_src=t_app->p_SourceFile(String(L"Chartboost-3.1.5/chartboost.jar",31));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidchartboost.monkey<31>");
	c_File* t_dst=t_app->p_TargetFile(String(L"libs/chartboost-3.1.5.jar",25));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidchartboost.monkey<33>");
	t_src->p_CopyTo2(t_dst);
}
void c_AndroidChartboost::p_Run(c_App* t_app){
	DBG_ENTER("AndroidChartboost.Run")
	c_AndroidChartboost *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidchartboost.monkey<17>");
	c_Android::m_AddPermission(String(L"android.permission.WRITE_EXTERNAL_STORAGE",41));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidchartboost.monkey<18>");
	c_Android::m_AddPermission(String(L"android.permission.ACCESS_NETWORK_STATE",39));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidchartboost.monkey<19>");
	c_Android::m_AddPermission(String(L"android.permission.ACCESS_WIFI_STATE",36));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidchartboost.monkey<20>");
	c_Android::m_AddPermission(String(L"android.permission.INTERNET",27));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidchartboost.monkey<22>");
	p_CopyLibs(t_app);
}
c_AndroidChartboost* c_AndroidChartboost::m_new(){
	DBG_ENTER("AndroidChartboost.new")
	c_AndroidChartboost *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidchartboost.monkey<13>");
	return this;
}
void c_AndroidChartboost::mark(){
	Object::mark();
}
String c_AndroidChartboost::debug(){
	String t="(AndroidChartboost)\n";
	return t;
}
c_AndroidIcons::c_AndroidIcons(){
	m_app=0;
	String t_[]={String(L"low",3),String(L"medium",6),String(L"high",4),String(L"extra-high",10)};
	m_VALID_TYPES=Array<String >(t_,4);
}
String c_AndroidIcons::p_GetType(){
	DBG_ENTER("AndroidIcons.GetType")
	c_AndroidIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<60>");
	if(m_app->p_GetAdditionArguments().Length()<1){
		DBG_BLOCK();
		return String();
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<61>");
	String t_=m_app->p_GetAdditionArguments().At(0).ToLower();
	return t_;
}
bool c_AndroidIcons::p_IsValidType(){
	DBG_ENTER("AndroidIcons.IsValidType")
	c_AndroidIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<46>");
	String t_given=p_GetType();
	DBG_LOCAL(t_given,"given")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<47>");
	Array<String > t_=m_VALID_TYPES;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		String t_check=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_check,"check")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<48>");
		if(t_given==t_check){
			DBG_BLOCK();
			return true;
		}
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<51>");
	return false;
}
String c_AndroidIcons::p_GetFilename(){
	DBG_ENTER("AndroidIcons.GetFilename")
	c_AndroidIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<81>");
	if(m_app->p_GetAdditionArguments().Length()<2){
		DBG_BLOCK();
		return String();
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<82>");
	String t_=m_app->p_GetAdditionArguments().At(1);
	return t_;
}
bool c_AndroidIcons::p_IsValidFilename(){
	DBG_ENTER("AndroidIcons.IsValidFilename")
	c_AndroidIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<55>");
	c_File* t_file=(new c_File)->m_new(p_GetFilename());
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<56>");
	bool t_=t_file->p_Exists();
	return t_;
}
void c_AndroidIcons::p_CheckArgs(){
	DBG_ENTER("AndroidIcons.CheckArgs")
	c_AndroidIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<32>");
	if(!p_IsValidType()){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<33>");
		m_app->p_LogInfo(String(L"First argument must be one of:",30));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<34>");
		Array<String > t_=m_VALID_TYPES;
		int t_2=0;
		while(t_2<t_.Length()){
			DBG_BLOCK();
			String t_type=t_.At(t_2);
			t_2=t_2+1;
			DBG_LOCAL(t_type,"type")
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<35>");
			m_app->p_LogInfo(String(L"- ",2)+t_type);
		}
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<37>");
		m_app->p_LogError(String(L"Invalid first argument",22));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<40>");
	if(!p_IsValidFilename()){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<41>");
		m_app->p_LogError(String(L"Second argument must be a valid filename",40));
	}
}
String c_AndroidIcons::p_GetShortType(){
	DBG_ENTER("AndroidIcons.GetShortType")
	c_AndroidIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<65>");
	String t_1=p_GetType();
	DBG_LOCAL(t_1,"1")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<66>");
	if(t_1==String(L"low",3)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<67>");
		return String(L"l",1);
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<68>");
		if(t_1==String(L"medium",6)){
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<69>");
			return String(L"m",1);
		}else{
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<70>");
			if(t_1==String(L"high",4)){
				DBG_BLOCK();
				DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<71>");
				return String(L"h",1);
			}else{
				DBG_BLOCK();
				DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<72>");
				if(t_1==String(L"extra-high",10)){
					DBG_BLOCK();
					DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<73>");
					return String(L"xh",2);
				}else{
					DBG_BLOCK();
					DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<75>");
					m_app->p_LogError(String(L"No short version of the given type defined",42));
					DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<76>");
					return String();
				}
			}
		}
	}
}
c_File* c_AndroidIcons::p_GetFile(){
	DBG_ENTER("AndroidIcons.GetFile")
	c_AndroidIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<86>");
	c_File* t_=(new c_File)->m_new(p_GetFilename());
	return t_;
}
void c_AndroidIcons::p_Run(c_App* t_app){
	DBG_ENTER("AndroidIcons.Run")
	c_AndroidIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<21>");
	gc_assign(this->m_app,t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<22>");
	p_CheckArgs();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<24>");
	c_Dir* t_dir=t_app->p_TargetDir(String(L"res/drawable-",13)+p_GetShortType()+String(L"dpi",3));
	DBG_LOCAL(t_dir,"dir")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<25>");
	t_dir->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<26>");
	p_GetFile()->p_CopyTo(t_dir->p_GetPath()+String(L"/icon.png",9));
}
c_AndroidIcons* c_AndroidIcons::m_new(){
	DBG_ENTER("AndroidIcons.new")
	c_AndroidIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidicons.monkey<12>");
	return this;
}
void c_AndroidIcons::mark(){
	Object::mark();
	gc_mark_q(m_app);
	gc_mark_q(m_VALID_TYPES);
}
String c_AndroidIcons::debug(){
	String t="(AndroidIcons)\n";
	t+=dbg_decl("app",&m_app);
	t+=dbg_decl("VALID_TYPES",&m_VALID_TYPES);
	return t;
}
c_AndroidLocalytics::c_AndroidLocalytics(){
}
void c_AndroidLocalytics::p_CopyAndroidBillingLibrary(c_App* t_app){
	DBG_ENTER("AndroidLocalytics.CopyAndroidBillingLibrary")
	c_AndroidLocalytics *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<44>");
	c_Dir* t_src=t_app->p_SourceDir(String(L"LocalyticsSession-2.4/src",25));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<45>");
	c_Dir* t_dst=t_app->p_TargetDir(String(L"src_Localytics/src",18));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<47>");
	if(!t_dst->p_Parent()->p_Exists()){
		DBG_BLOCK();
		t_dst->p_Parent()->p_Create();
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<48>");
	t_src->p_CopyTo3(t_dst,true,true,true);
}
void c_AndroidLocalytics::p_PatchBuildXml(c_App* t_app){
	DBG_ENTER("AndroidLocalytics.PatchBuildXml")
	c_AndroidLocalytics *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<28>");
	String t_patchStr=String(L"<copy todir=\"src\"><fileset dir=\"src_Localytics\"/></copy>",56);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<29>");
	String t_match=String(L"</project>",10);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<30>");
	c_File* t_target=t_app->p_TargetFile(String(L"build.xml",9));
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<32>");
	if(!t_target->p_Contains(t_patchStr)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<33>");
		if(!t_target->p_Contains(t_match)){
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<34>");
			t_app->p_LogWarning(String(L"Unable to add required copy instructions to build.xml",53));
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<35>");
			t_app->p_LogWarning(String(L"please add the following line to your build.xml:",48));
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<36>");
			t_app->p_LogWarning(String(L"    ",4)+t_patchStr);
		}else{
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<38>");
			t_target->p_InsertBefore(t_match,t_patchStr);
		}
	}
}
void c_AndroidLocalytics::p_Run(c_App* t_app){
	DBG_ENTER("AndroidLocalytics.Run")
	c_AndroidLocalytics *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<17>");
	c_Android::m_AddPermission(String(L"android.permission.INTERNET",27));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<19>");
	p_CopyAndroidBillingLibrary(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<20>");
	p_PatchBuildXml(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<22>");
	t_app->p_LogInfo(String(L"Monkey interface can be found here: http://goo.gl/0w5AG",55));
}
c_AndroidLocalytics* c_AndroidLocalytics::m_new(){
	DBG_ENTER("AndroidLocalytics.new")
	c_AndroidLocalytics *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidlocalytics.monkey<13>");
	return this;
}
void c_AndroidLocalytics::mark(){
	Object::mark();
}
String c_AndroidLocalytics::debug(){
	String t="(AndroidLocalytics)\n";
	return t;
}
c_List::c_List(){
	m__head=((new c_HeadNode)->m_new());
}
c_List* c_List::m_new(){
	DBG_ENTER("List.new")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node2* c_List::p_AddLast(String t_data){
	DBG_ENTER("List.AddLast")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<108>");
	c_Node2* t_=(new c_Node2)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List* c_List::m_new2(Array<String > t_data){
	DBG_ENTER("List.new")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<13>");
	Array<String > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		String t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<14>");
		p_AddLast(t_t);
	}
	return this;
}
bool c_List::p_IsEmpty(){
	DBG_ENTER("List.IsEmpty")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<50>");
	bool t_=m__head->m__succ==m__head;
	return t_;
}
String c_List::p_RemoveFirst(){
	DBG_ENTER("List.RemoveFirst")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<87>");
	if(p_IsEmpty()){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on empty list",31));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<89>");
	String t_data=m__head->m__succ->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<90>");
	m__head->m__succ->p_Remove();
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<91>");
	return t_data;
}
bool c_List::p_Equals5(String t_lhs,String t_rhs){
	DBG_ENTER("List.Equals")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
c_Node2* c_List::p_Find(String t_value,c_Node2* t_start){
	DBG_ENTER("List.Find")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_start,"start")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<116>");
	while(t_start!=m__head){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<117>");
		if(p_Equals5(t_value,t_start->m__data)){
			DBG_BLOCK();
			return t_start;
		}
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<118>");
		t_start=t_start->m__succ;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<120>");
	return 0;
}
c_Node2* c_List::p_Find2(String t_value){
	DBG_ENTER("List.Find")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<112>");
	c_Node2* t_=p_Find(t_value,m__head->m__succ);
	return t_;
}
void c_List::p_RemoveFirst2(String t_value){
	DBG_ENTER("List.RemoveFirst")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<141>");
	c_Node2* t_node=p_Find2(t_value);
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<142>");
	if((t_node)!=0){
		DBG_BLOCK();
		t_node->p_Remove();
	}
}
int c_List::p_Count(){
	DBG_ENTER("List.Count")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<41>");
	int t_n=0;
	c_Node2* t_node=m__head->m__succ;
	DBG_LOCAL(t_n,"n")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<42>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<43>");
		t_node=t_node->m__succ;
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<44>");
		t_n+=1;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<46>");
	return t_n;
}
c_Enumerator* c_List::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<186>");
	c_Enumerator* t_=(new c_Enumerator)->m_new(this);
	return t_;
}
Array<String > c_List::p_ToArray(){
	DBG_ENTER("List.ToArray")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<19>");
	Array<String > t_arr=Array<String >(p_Count());
	int t_i=0;
	DBG_LOCAL(t_arr,"arr")
	DBG_LOCAL(t_i,"i")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<20>");
	c_Enumerator* t_=this->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		String t_t=t_->p_NextObject();
		DBG_LOCAL(t_t,"t")
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<21>");
		t_arr.At(t_i)=t_t;
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<22>");
		t_i+=1;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<24>");
	return t_arr;
}
void c_List::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_StringList::c_StringList(){
}
c_StringList* c_StringList::m_new(Array<String > t_data){
	DBG_ENTER("StringList.new")
	c_StringList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<431>");
	c_List::m_new2(t_data);
	return this;
}
c_StringList* c_StringList::m_new2(){
	DBG_ENTER("StringList.new")
	c_StringList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<428>");
	c_List::m_new();
	return this;
}
bool c_StringList::p_Equals5(String t_lhs,String t_rhs){
	DBG_ENTER("StringList.Equals")
	c_StringList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<439>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
void c_StringList::mark(){
	c_List::mark();
}
String c_StringList::debug(){
	String t="(StringList)\n";
	t=c_List::debug()+t;
	return t;
}
c_Node2::c_Node2(){
	m__succ=0;
	m__pred=0;
	m__data=String();
}
c_Node2* c_Node2::m_new(c_Node2* t_succ,c_Node2* t_pred,String t_data){
	DBG_ENTER("Node.new")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<265>");
	m__data=t_data;
	return this;
}
c_Node2* c_Node2::m_new2(){
	DBG_ENTER("Node.new")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node2::p_Remove(){
	DBG_ENTER("Node.Remove")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node2::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
}
String c_Node2::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode::c_HeadNode(){
}
c_HeadNode* c_HeadNode::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<310>");
	c_Node2::m_new2();
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode::mark(){
	c_Node2::mark();
}
String c_HeadNode::debug(){
	String t="(HeadNode)\n";
	t=c_Node2::debug()+t;
	return t;
}
c_Enumerator::c_Enumerator(){
	m__list=0;
	m__curr=0;
}
c_Enumerator* c_Enumerator::m_new(c_List* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator* c_Enumerator::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
String c_Enumerator::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<338>");
	String t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
Array<String > bb_os_LoadDir(String t_path,bool t_recursive,bool t_hidden){
	DBG_ENTER("LoadDir")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_recursive,"recursive")
	DBG_LOCAL(t_hidden,"hidden")
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<48>");
	c_StringList* t_dirs=(new c_StringList)->m_new2();
	c_StringList* t_files=(new c_StringList)->m_new2();
	DBG_LOCAL(t_dirs,"dirs")
	DBG_LOCAL(t_files,"files")
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<50>");
	t_dirs->p_AddLast(String());
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<52>");
	while(!t_dirs->p_IsEmpty()){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<54>");
		String t_dir=t_dirs->p_RemoveFirst();
		DBG_LOCAL(t_dir,"dir")
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<56>");
		Array<String > t_=LoadDir(t_path+String(L"/",1)+t_dir);
		int t_2=0;
		while(t_2<t_.Length()){
			DBG_BLOCK();
			String t_f=t_.At(t_2);
			t_2=t_2+1;
			DBG_LOCAL(t_f,"f")
			DBG_INFO("/Applications/Monkey/modules/os/os.monkey<57>");
			if(!t_hidden && t_f.StartsWith(String(L".",1))){
				DBG_BLOCK();
				continue;
			}
			DBG_INFO("/Applications/Monkey/modules/os/os.monkey<59>");
			if((t_dir).Length()!=0){
				DBG_BLOCK();
				t_f=t_dir+String(L"/",1)+t_f;
			}
			DBG_INFO("/Applications/Monkey/modules/os/os.monkey<61>");
			int t_1=FileType(t_path+String(L"/",1)+t_f);
			DBG_LOCAL(t_1,"1")
			DBG_INFO("/Applications/Monkey/modules/os/os.monkey<62>");
			if(t_1==1){
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/os/os.monkey<63>");
				t_files->p_AddLast(t_f);
			}else{
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/os/os.monkey<64>");
				if(t_1==2){
					DBG_BLOCK();
					DBG_INFO("/Applications/Monkey/modules/os/os.monkey<65>");
					if(t_recursive){
						DBG_BLOCK();
						DBG_INFO("/Applications/Monkey/modules/os/os.monkey<66>");
						t_dirs->p_AddLast(t_f);
					}else{
						DBG_BLOCK();
						DBG_INFO("/Applications/Monkey/modules/os/os.monkey<68>");
						t_files->p_AddLast(t_f);
					}
				}
			}
		}
	}
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<74>");
	Array<String > t_3=t_files->p_ToArray();
	return t_3;
}
int bb_os_DeleteDir(String t_path,bool t_recursive){
	DBG_ENTER("DeleteDir")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_recursive,"recursive")
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<111>");
	if(!t_recursive){
		DBG_BLOCK();
		int t_=DeleteDir(t_path);
		return t_;
	}
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<113>");
	int t_4=FileType(t_path);
	DBG_LOCAL(t_4,"4")
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<114>");
	if(t_4==0){
		DBG_BLOCK();
		return 1;
	}else{
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<115>");
		if(t_4==1){
			DBG_BLOCK();
			return 0;
		}
	}
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<118>");
	Array<String > t_2=LoadDir(t_path);
	int t_3=0;
	while(t_3<t_2.Length()){
		DBG_BLOCK();
		String t_f=t_2.At(t_3);
		t_3=t_3+1;
		DBG_LOCAL(t_f,"f")
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<119>");
		if(t_f==String(L".",1) || t_f==String(L"..",2)){
			DBG_BLOCK();
			continue;
		}
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<121>");
		String t_fpath=t_path+String(L"/",1)+t_f;
		DBG_LOCAL(t_fpath,"fpath")
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<123>");
		if(FileType(t_fpath)==2){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/os/os.monkey<124>");
			if(!((bb_os_DeleteDir(t_fpath,true))!=0)){
				DBG_BLOCK();
				return 0;
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/os/os.monkey<126>");
			if(!((DeleteFile(t_fpath))!=0)){
				DBG_BLOCK();
				return 0;
			}
		}
	}
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<130>");
	int t_5=DeleteDir(t_path);
	return t_5;
}
int bb_os_CopyDir(String t_srcpath,String t_dstpath,bool t_recursive,bool t_hidden){
	DBG_ENTER("CopyDir")
	DBG_LOCAL(t_srcpath,"srcpath")
	DBG_LOCAL(t_dstpath,"dstpath")
	DBG_LOCAL(t_recursive,"recursive")
	DBG_LOCAL(t_hidden,"hidden")
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<79>");
	if(FileType(t_srcpath)!=2){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<83>");
	Array<String > t_files=LoadDir(t_srcpath);
	DBG_LOCAL(t_files,"files")
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<85>");
	int t_2=FileType(t_dstpath);
	DBG_LOCAL(t_2,"2")
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<86>");
	if(t_2==0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<87>");
		if(!((CreateDir(t_dstpath))!=0)){
			DBG_BLOCK();
			return 0;
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<88>");
		if(t_2==1){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/os/os.monkey<89>");
			return 0;
		}
	}
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<92>");
	Array<String > t_=t_files;
	int t_3=0;
	while(t_3<t_.Length()){
		DBG_BLOCK();
		String t_f=t_.At(t_3);
		t_3=t_3+1;
		DBG_LOCAL(t_f,"f")
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<93>");
		if(!t_hidden && t_f.StartsWith(String(L".",1))){
			DBG_BLOCK();
			continue;
		}
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<95>");
		String t_srcp=t_srcpath+String(L"/",1)+t_f;
		DBG_LOCAL(t_srcp,"srcp")
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<96>");
		String t_dstp=t_dstpath+String(L"/",1)+t_f;
		DBG_LOCAL(t_dstp,"dstp")
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<98>");
		int t_32=FileType(t_srcp);
		DBG_LOCAL(t_32,"3")
		DBG_INFO("/Applications/Monkey/modules/os/os.monkey<99>");
		if(t_32==1){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/os/os.monkey<100>");
			if(!((CopyFile(t_srcp,t_dstp))!=0)){
				DBG_BLOCK();
				return 0;
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/os/os.monkey<101>");
			if(t_32==2){
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/os/os.monkey<102>");
				if(t_recursive && !((bb_os_CopyDir(t_srcp,t_dstp,t_recursive,t_hidden))!=0)){
					DBG_BLOCK();
					return 0;
				}
			}
		}
	}
	DBG_INFO("/Applications/Monkey/modules/os/os.monkey<106>");
	return 1;
}
c_AndroidRevmob::c_AndroidRevmob(){
}
void c_AndroidRevmob::p_PatchActivity(c_App* t_app){
	DBG_ENTER("AndroidRevmob.PatchActivity")
	c_AndroidRevmob *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<27>");
	String t_match=String(L"</application>",14);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<31>");
	String t_patchStr=String(L"<activity\n\tandroid:name=\"com.revmob.ads.fullscreen.FullscreenActivity\"\n\tandroid:configChanges=\"keyboardHidden|orientation\" >\n</activity>",136);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<32>");
	c_File* t_target=c_Android::m_GetManifest();
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<34>");
	if(t_target->p_Contains(String(L"com.revmob.ads.fullscreen.FullscreenActivity",44))){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<38>");
	if(!t_target->p_Contains(t_match)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<39>");
		t_app->p_LogWarning(String(L"Unable to add required activity to AndroidManifest.xml",54));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<40>");
		t_app->p_LogWarning(String(L"Please add the following activity manually:",43));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<41>");
		t_app->p_LogWarning(String(L"    ",4)+t_patchStr);
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<43>");
		t_target->p_InsertBefore(t_match,t_patchStr);
	}
}
void c_AndroidRevmob::p_PatchPermissions(){
	DBG_ENTER("AndroidRevmob.PatchPermissions")
	c_AndroidRevmob *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<48>");
	c_Android::m_AddPermission(String(L"android.permission.ACCESS_NETWORK_STATE",39));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<49>");
	c_Android::m_AddPermission(String(L"android.permission.ACCESS_WIFI_STATE",36));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<50>");
	c_Android::m_AddPermission(String(L"android.permission.INTERNET",27));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<51>");
	c_Android::m_AddPermission(String(L"android.permission.READ_PHONE_STATE",35));
}
void c_AndroidRevmob::p_CopyLibs(c_App* t_app){
	DBG_ENTER("AndroidRevmob.CopyLibs")
	c_AndroidRevmob *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<55>");
	c_Android::m_EnsureLibsFolder();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<57>");
	c_File* t_src=t_app->p_SourceFile(String(L"revmob-6.4.3.jar",16));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<58>");
	c_File* t_dst=t_app->p_TargetFile(String(L"libs/revmob-6.4.3.jar",21));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<60>");
	t_src->p_CopyTo2(t_dst);
}
void c_AndroidRevmob::p_PatchLayout(c_App* t_app){
	DBG_ENTER("AndroidRevmob.PatchLayout")
	c_AndroidRevmob *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<64>");
	String t_match=String(L"android:id=\"@+id/banner\"",24);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<65>");
	c_File* t_target=t_app->p_TargetFile(String(L"templates/res/layout/main.xml",29));
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<67>");
	if(t_target->p_Contains(t_match)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<73>");
	String t_monkeyView=t_target->p_GetContentBetween(String(L"<view class=",12),String(L"/>",2));
	DBG_LOCAL(t_monkeyView,"monkeyView")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<75>");
	if(t_monkeyView!=String()){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<76>");
		c_File* t_manifestTemplate=t_app->p_SourceFile(String(L"manifestTemplate.xml",20));
		DBG_LOCAL(t_manifestTemplate,"manifestTemplate")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<77>");
		t_manifestTemplate->p_Replace(String(L"{%%MONKEY_VIEW%%}",17),t_monkeyView);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<79>");
		t_target->p_data2(t_manifestTemplate->p_Get());
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<81>");
		t_app->p_LogWarning(String(L"Unable to add revmob layout elements",36));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<82>");
		t_app->p_LogWarning(String(L"A layout with @id/banner is required!",37));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<83>");
		t_app->p_LogWarning(String(L"Monkey view wasn't found in base-manifest",41));
	}
}
void c_AndroidRevmob::p_Run(c_App* t_app){
	DBG_ENTER("AndroidRevmob.Run")
	c_AndroidRevmob *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<17>");
	p_PatchActivity(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<18>");
	p_PatchPermissions();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<19>");
	p_CopyLibs(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<20>");
	p_PatchLayout(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<21>");
	t_app->p_LogInfo(String(L"Monkey interface can be found here: http://goo.gl/yVTiV",55));
}
c_AndroidRevmob* c_AndroidRevmob::m_new(){
	DBG_ENTER("AndroidRevmob.new")
	c_AndroidRevmob *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidrevmob.monkey<13>");
	return this;
}
void c_AndroidRevmob::mark(){
	Object::mark();
}
String c_AndroidRevmob::debug(){
	String t="(AndroidRevmob)\n";
	return t;
}
c_AndroidVersion::c_AndroidVersion(){
}
void c_AndroidVersion::p_Run(c_App* t_app){
	DBG_ENTER("AndroidVersion.Run")
	c_AndroidVersion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<14>");
	if(t_app->p_GetAdditionArguments().Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<15>");
		t_app->p_LogError(String(L"First argument must be the new version",38));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<18>");
	c_File* t_manifest=c_Android::m_GetManifest();
	DBG_LOCAL(t_manifest,"manifest")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<19>");
	Array<int > t_codeLines=t_manifest->p_FindLines(String(L"android:versionCode",19));
	DBG_LOCAL(t_codeLines,"codeLines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<20>");
	Array<int > t_nameLines=t_manifest->p_FindLines(String(L"android:versionName",19));
	DBG_LOCAL(t_nameLines,"nameLines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<22>");
	if(t_codeLines.Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<23>");
		t_app->p_LogError(String(L"Found zero or more than one 'android:versionCode' property",58));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<25>");
	if(t_nameLines.Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<26>");
		t_app->p_LogError(String(L"Found zero or more than one 'android:versionName' property",58));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<29>");
	String t_codeValue=t_app->p_GetAdditionArguments().At(0).Replace(String(L".",1),String());
	DBG_LOCAL(t_codeValue,"codeValue")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<30>");
	String t_nameValue=t_app->p_GetAdditionArguments().At(0);
	DBG_LOCAL(t_nameValue,"nameValue")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<32>");
	t_manifest->p_ReplaceLine(t_codeLines.At(0),String(L"\tandroid:versionCode=\"",22)+t_codeValue+String(L"\"",1));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<33>");
	t_manifest->p_ReplaceLine(t_nameLines.At(0),String(L"\tandroid:versionName=\"",22)+t_nameValue+String(L"\"",1));
}
c_AndroidVersion* c_AndroidVersion::m_new(){
	DBG_ENTER("AndroidVersion.new")
	c_AndroidVersion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidversion.monkey<12>");
	return this;
}
void c_AndroidVersion::mark(){
	Object::mark();
}
String c_AndroidVersion::debug(){
	String t="(AndroidVersion)\n";
	return t;
}
c_List2::c_List2(){
	m__head=((new c_HeadNode2)->m_new());
}
c_List2* c_List2::m_new(){
	DBG_ENTER("List.new")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node3* c_List2::p_AddLast2(int t_data){
	DBG_ENTER("List.AddLast")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<108>");
	c_Node3* t_=(new c_Node3)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List2* c_List2::m_new2(Array<int > t_data){
	DBG_ENTER("List.new")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<13>");
	Array<int > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		int t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<14>");
		p_AddLast2(t_t);
	}
	return this;
}
int c_List2::p_Count(){
	DBG_ENTER("List.Count")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<41>");
	int t_n=0;
	c_Node3* t_node=m__head->m__succ;
	DBG_LOCAL(t_n,"n")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<42>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<43>");
		t_node=t_node->m__succ;
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<44>");
		t_n+=1;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<46>");
	return t_n;
}
c_Enumerator2* c_List2::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<186>");
	c_Enumerator2* t_=(new c_Enumerator2)->m_new(this);
	return t_;
}
Array<int > c_List2::p_ToArray(){
	DBG_ENTER("List.ToArray")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<19>");
	Array<int > t_arr=Array<int >(p_Count());
	int t_i=0;
	DBG_LOCAL(t_arr,"arr")
	DBG_LOCAL(t_i,"i")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<20>");
	c_Enumerator2* t_=this->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		int t_t=t_->p_NextObject();
		DBG_LOCAL(t_t,"t")
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<21>");
		t_arr.At(t_i)=t_t;
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<22>");
		t_i+=1;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<24>");
	return t_arr;
}
void c_List2::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List2::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_IntList::c_IntList(){
}
c_IntList* c_IntList::m_new(Array<int > t_data){
	DBG_ENTER("IntList.new")
	c_IntList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<398>");
	c_List2::m_new2(t_data);
	return this;
}
c_IntList* c_IntList::m_new2(){
	DBG_ENTER("IntList.new")
	c_IntList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<395>");
	c_List2::m_new();
	return this;
}
void c_IntList::mark(){
	c_List2::mark();
}
String c_IntList::debug(){
	String t="(IntList)\n";
	t=c_List2::debug()+t;
	return t;
}
c_Node3::c_Node3(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node3* c_Node3::m_new(c_Node3* t_succ,c_Node3* t_pred,int t_data){
	DBG_ENTER("Node.new")
	c_Node3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<265>");
	m__data=t_data;
	return this;
}
c_Node3* c_Node3::m_new2(){
	DBG_ENTER("Node.new")
	c_Node3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<258>");
	return this;
}
void c_Node3::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
}
String c_Node3::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode2::c_HeadNode2(){
}
c_HeadNode2* c_HeadNode2::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<310>");
	c_Node3::m_new2();
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode2::mark(){
	c_Node3::mark();
}
String c_HeadNode2::debug(){
	String t="(HeadNode)\n";
	t=c_Node3::debug()+t;
	return t;
}
c_Enumerator2::c_Enumerator2(){
	m__list=0;
	m__curr=0;
}
c_Enumerator2* c_Enumerator2::m_new(c_List2* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator2* c_Enumerator2::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator2::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
int c_Enumerator2::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<338>");
	int t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("/Applications/Monkey/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator2::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator2::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
c_AndroidVungle::c_AndroidVungle(){
}
void c_AndroidVungle::p_CopyLibs(c_App* t_app){
	DBG_ENTER("AndroidVungle.CopyLibs")
	c_AndroidVungle *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<48>");
	c_Android::m_EnsureLibsFolder();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<50>");
	c_File* t_src=t_app->p_SourceFile(String(L"vungle-publisher-1.3.11.jar",27));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<51>");
	c_File* t_dst=t_app->p_TargetFile(String(L"libs/vungle-publisher-1.3.11.jar",32));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<53>");
	t_src->p_CopyTo2(t_dst);
}
void c_AndroidVungle::p_PatchActivity(c_App* t_app){
	DBG_ENTER("AndroidVungle.PatchActivity")
	c_AndroidVungle *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<29>");
	String t_match=String(L"</application>",14);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<30>");
	String t_patchStr=String(L"<activity android:name=\"com.vungle.sdk.VungleAdvert\" android:configChanges=\"keyboardHidden|orientation|screenSize\" android:theme=\"@android:style/Theme.NoTitleBar.Fullscreen\" /> \n",178);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<31>");
	t_patchStr=t_patchStr+String(L"<service android:name=\"com.vungle.sdk.VungleIntentService\" />",61);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<32>");
	c_File* t_target=c_Android::m_GetManifest();
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<34>");
	if(t_target->p_Contains(String(L"com.amazon.device.ads.AdActivity",32))){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<38>");
	if(!t_target->p_Contains(t_match)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<39>");
		t_app->p_LogWarning(String(L"Unable to add required activity to AndroidManifest.xml",54));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<40>");
		t_app->p_LogWarning(String(L"Please add the following activity manually:",43));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<41>");
		t_app->p_LogWarning(String(L"    ",4)+t_patchStr);
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<43>");
		t_target->p_InsertBefore(t_match,t_patchStr);
	}
}
void c_AndroidVungle::p_Run(c_App* t_app){
	DBG_ENTER("AndroidVungle.Run")
	c_AndroidVungle *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<16>");
	c_Android::m_AddPermission(String(L"android.permission.WRITE_EXTERNAL_STORAGE",41));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<17>");
	c_Android::m_AddPermission(String(L"android.permission.ACCESS_NETWORK_STATE",39));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<18>");
	c_Android::m_AddPermission(String(L"android.permission.ACCESS_WIFI_STATE",36));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<19>");
	c_Android::m_AddPermission(String(L"android.permission.INTERNET",27));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<21>");
	p_CopyLibs(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<23>");
	p_PatchActivity(t_app);
}
c_AndroidVungle* c_AndroidVungle::m_new(){
	DBG_ENTER("AndroidVungle.new")
	c_AndroidVungle *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/androidvungle.monkey<13>");
	return this;
}
void c_AndroidVungle::mark(){
	Object::mark();
}
String c_AndroidVungle::debug(){
	String t="(AndroidVungle)\n";
	return t;
}
c_GooglePayment::c_GooglePayment(){
}
void c_GooglePayment::p_PatchServiceAndReceiver(c_App* t_app){
	DBG_ENTER("GooglePayment.PatchServiceAndReceiver")
	c_GooglePayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<29>");
	String t_match=String(L"</application>",14);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<38>");
	String t_patchStr=String(L"<service android:name=\"net.robotmedia.billing.BillingService\" />\n<receiver android:name=\"net.robotmedia.billing.BillingReceiver\">\n\t<intent-filter>\n\t\t<action android:name=\"com.android.vending.billing.IN_APP_NOTIFY\" />\n\t\t<action android:name=\"com.android.vending.billing.RESPONSE_CODE\" />\n\t\t<action android:name=\"com.android.vending.billing.PURCHASE_STATE_CHANGED\" />\n\t</intent-filter>\n</receiver>",395);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<39>");
	c_File* t_target=c_Android::m_GetManifest();
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<41>");
	if(t_target->p_Contains(String(L"net.robotmedia.billing.BillingService",37))){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<45>");
	if(!t_target->p_Contains(t_match)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<46>");
		t_app->p_LogWarning(String(L"Unable to add required activity to AndroidManifest.xml",54));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<47>");
		t_app->p_LogWarning(String(L"Please add the following activity manually:",43));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<48>");
		t_app->p_LogWarning(String(L"    ",4)+t_patchStr);
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<50>");
		t_target->p_InsertBefore(t_match,t_patchStr);
	}
}
void c_GooglePayment::p_CopyAndroidBillingLibrary(c_App* t_app){
	DBG_ENTER("GooglePayment.CopyAndroidBillingLibrary")
	c_GooglePayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<71>");
	c_Dir* t_src=t_app->p_SourceDir(String(L"AndroidBillingLibrary/AndroidBillingLibrary/src",47));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<72>");
	c_Dir* t_dst=t_app->p_TargetDir(String(L"src_AndroidBillingLibrary/src",29));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<74>");
	if(!t_src->p_Exists()){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<76>");
		t_app->p_LogError(String(L"AndroidBillingLibrary folger seems to be empty! Did you run 'git submodule update --init'?",90));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<77>");
	if(!t_dst->p_Parent()->p_Exists()){
		DBG_BLOCK();
		t_dst->p_Parent()->p_Create();
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<78>");
	t_src->p_CopyTo3(t_dst,true,true,true);
}
void c_GooglePayment::p_PatchAndroidBillingLibray(c_App* t_app){
	DBG_ENTER("GooglePayment.PatchAndroidBillingLibray")
	c_GooglePayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<82>");
	c_File* t_target=t_app->p_TargetFile(String(L"src_AndroidBillingLibrary/net/robotmedia/billing/BillingRequest.java",68));
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<83>");
	String t_match=String(L"if (response == ResponseCode.RESULT_OK) {",41);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<85>");
	String t_alreadyPatchedMarker=String(L"// MONKEY-WIZARD-PATCHED",24);
	DBG_LOCAL(t_alreadyPatchedMarker,"alreadyPatchedMarker")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<86>");
	if(t_target->p_Contains(t_alreadyPatchedMarker)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<90>");
	if(!t_target->p_Exists() || !t_target->p_GetLine(220).Contains(t_match) || !t_target->p_GetLine(222).Contains(String(L"}",1))){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<91>");
		t_app->p_LogWarning(String(L"Unable to patch the AndroidBillingLibrary",41));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<92>");
		t_app->p_LogWarning(String(L"Please remove this if statement:",32));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<93>");
		t_app->p_LogWarning(String(L"    ",4)+t_match);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<94>");
		t_app->p_LogWarning(String(L"Around line 220 (including the closing }) in:",45));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<95>");
		t_app->p_LogWarning(String(L"    ",4)+t_target->p_GetPath());
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<96>");
		t_app->p_LogWarning(String(L"But only the condition and _NOT_ the enclosed statement!",56));
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<98>");
		t_target->p_RemoveLine(222);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<99>");
		t_target->p_RemoveLine(220);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<100>");
		t_target->p_Append(String(L"\n",1)+t_alreadyPatchedMarker);
	}
}
void c_GooglePayment::p_PatchBuildXml(c_App* t_app){
	DBG_ENTER("GooglePayment.PatchBuildXml")
	c_GooglePayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<55>");
	String t_patchStr=String(L"<copy todir=\"src\"><fileset dir=\"src_AndroidBillingLibrary\"/></copy>",67);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<56>");
	String t_match=String(L"</project>",10);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<57>");
	c_File* t_target=t_app->p_TargetFile(String(L"build.xml",9));
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<59>");
	if(!t_target->p_Contains(t_patchStr)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<60>");
		if(!t_target->p_Contains(t_match)){
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<61>");
			t_app->p_LogWarning(String(L"Unable to add required copy instructions to build.xml",53));
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<62>");
			t_app->p_LogWarning(String(L"please add the following line to your build.xml:",48));
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<63>");
			t_app->p_LogWarning(String(L"    ",4)+t_patchStr);
		}else{
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<65>");
			t_target->p_InsertBefore(t_match,t_patchStr);
		}
	}
}
void c_GooglePayment::p_Run(c_App* t_app){
	DBG_ENTER("GooglePayment.Run")
	c_GooglePayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<15>");
	c_Android::m_AddPermission(String(L"android.permission.INTERNET",27));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<16>");
	c_Android::m_AddPermission(String(L"com.android.vending.BILLING",27));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<17>");
	p_PatchServiceAndReceiver(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<19>");
	p_CopyAndroidBillingLibrary(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<20>");
	p_PatchAndroidBillingLibray(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<21>");
	p_PatchBuildXml(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<23>");
	t_app->p_LogInfo(String(L"Monkey interface can be found here: http://goo.gl/QoYEC",55));
}
c_GooglePayment* c_GooglePayment::m_new(){
	DBG_ENTER("GooglePayment.new")
	c_GooglePayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/googlepayment.monkey<13>");
	return this;
}
void c_GooglePayment::mark(){
	Object::mark();
}
String c_GooglePayment::debug(){
	String t="(GooglePayment)\n";
	return t;
}
c_IosAddLanguage::c_IosAddLanguage(){
}
String c_IosAddLanguage::m_GetLang(c_App* t_app){
	DBG_ENTER("IosAddLanguage.GetLang")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<46>");
	if(t_app->p_GetAdditionArguments().Length()<1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<47>");
		t_app->p_LogError(String(L"Not enough arguments. Language code required.",45));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<50>");
	String t_=t_app->p_GetAdditionArguments().At(0);
	return t_;
}
void c_IosAddLanguage::p_Run(c_App* t_app){
	DBG_ENTER("IosAddLanguage.Run")
	c_IosAddLanguage *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<18>");
	String t_lang=m_GetLang(t_app);
	DBG_LOCAL(t_lang,"lang")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<19>");
	String t_langPath=t_lang+String(L".lproj",6);
	DBG_LOCAL(t_langPath,"langPath")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<20>");
	String t_name=String(L"localize.strings",16);
	DBG_LOCAL(t_name,"name")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<22>");
	String t_firstId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_firstId,"firstId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<23>");
	String t_secondId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_secondId,"secondId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<24>");
	String t_thirdId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_thirdId,"thirdId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<26>");
	c_Dir* t_dir=(new c_Dir)->m_new(t_app->p_TargetDir(t_langPath)->p_GetPath());
	DBG_LOCAL(t_dir,"dir")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<27>");
	t_dir->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<29>");
	c_File* t_file=(new c_File)->m_new(t_app->p_TargetDir(String())->p_GetPath()+t_langPath+String(L"/",1)+t_name);
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<30>");
	t_file->p_data2(String(L"/* Localize Strings */",22));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<31>");
	t_file->p_Save();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<33>");
	String t_buildsection=c_Ios::m_GetProject()->p_GetContentBetween(String(L"Begin PBXBuildFile section",26),String(L"End PBXBuildFile section",24));
	DBG_LOCAL(t_buildsection,"buildsection")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<35>");
	c_Ios::m_AddPbxBuildFile(t_langPath+String(L"/",1)+t_name,t_firstId,t_secondId,false,String());
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<36>");
	c_Ios::m_AddPbxFileReferenceFile(t_thirdId,t_lang,t_langPath+String(L"/",1)+t_name,String(L"file",4));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<37>");
	c_Ios::m_AddIconPBXGroup(t_langPath+String(L"/",1)+t_name,t_secondId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<38>");
	c_Ios::m_AddIconPBXResourcesBuildPhase(t_langPath+String(L"/",1)+t_name,t_firstId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<40>");
	String t_[]={t_thirdId};
	String t_2[]={t_lang};
	c_Ios::m_AddPBXVariantGroup(t_secondId,t_name,Array<String >(t_,1),Array<String >(t_2,1));
}
void c_IosAddLanguage::m_CopyImage(c_App* t_app,int t_idx,String t_name){
	DBG_ENTER("IosAddLanguage.CopyImage")
	DBG_LOCAL(t_app,"app")
	DBG_LOCAL(t_idx,"idx")
	DBG_LOCAL(t_name,"name")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<54>");
	Array<String > t_args=t_app->p_GetAdditionArguments();
	DBG_LOCAL(t_args,"args")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<55>");
	if(t_args.Length()<t_idx){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<56>");
		t_app->p_LogError(String(L"Argument ",9)+String(t_idx)+String(L" for file ",10)+t_name+String(L" missing",8));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<59>");
	String t_filename=t_app->p_GetAdditionArguments().At(t_idx-1);
	DBG_LOCAL(t_filename,"filename")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<60>");
	if(!t_filename.EndsWith(String(L".png",4))){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<61>");
		t_app->p_LogError(String(L"Image must be in PNG format",27));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<64>");
	String t_dstDir=t_app->p_TargetDir(String())->p_GetPath();
	DBG_LOCAL(t_dstDir,"dstDir")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<65>");
	c_File* t_file=(new c_File)->m_new(t_filename);
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<67>");
	if(!t_file->p_Exists()){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<68>");
		t_app->p_LogError(String(L"Invalid file given: ",20)+t_file->p_GetPath());
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<70>");
	t_file->p_CopyTo(t_dstDir+t_name);
}
void c_IosAddLanguage::m_AddImage(c_App* t_app,String t_filename){
	DBG_ENTER("IosAddLanguage.AddImage")
	DBG_LOCAL(t_app,"app")
	DBG_LOCAL(t_filename,"filename")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<74>");
	String t_firstId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_firstId,"firstId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<75>");
	String t_secondId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_secondId,"secondId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<77>");
	c_Ios::m_AddIconPBXBuildFile(t_filename,t_firstId,t_secondId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<78>");
	c_Ios::m_AddIconPBXFileReference(t_filename,t_secondId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<79>");
	c_Ios::m_AddIconPBXGroup(t_filename,t_secondId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<80>");
	c_Ios::m_AddIconPBXResourcesBuildPhase(t_filename,t_firstId);
}
c_IosAddLanguage* c_IosAddLanguage::m_new(){
	DBG_ENTER("IosAddLanguage.new")
	c_IosAddLanguage *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosaddlanguage.monkey<13>");
	return this;
}
void c_IosAddLanguage::mark(){
	Object::mark();
}
String c_IosAddLanguage::debug(){
	String t="(IosAddLanguage)\n";
	return t;
}
c_Ios::c_Ios(){
}
c_App* c_Ios::m_app;
c_File* c_Ios::m_GetProject(){
	DBG_ENTER("Ios.GetProject")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<46>");
	c_File* t_=m_app->p_TargetFile(String(L"MonkeyGame.xcodeproj/project.pbxproj",36));
	return t_;
}
String c_Ios::m_IntToHex(int t_value){
	DBG_ENTER("Ios.IntToHex")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<475>");
	if(t_value<9){
		DBG_BLOCK();
		String t_=String()+String(t_value);
		return t_;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<477>");
	String t_result=String();
	DBG_LOCAL(t_result,"result")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<479>");
	while(t_value>0){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<480>");
		t_result=String((Char)((int)String(L"0123456789ABCDEF",16).At(t_value % 16)),1)+t_result;
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<481>");
		t_value/=16;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<484>");
	return t_result;
}
String c_Ios::m_GenerateRandomId(){
	DBG_ENTER("Ios.GenerateRandomId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<488>");
	String t_result=String();
	DBG_LOCAL(t_result,"result")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<489>");
	while(t_result.Length()<24){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<490>");
		t_result=t_result+m_IntToHex(int(bb_random_Rnd2(FLOAT(0.0),FLOAT(17.0))));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<494>");
	String t_=t_result.Slice(0,24);
	return t_;
}
String c_Ios::m_GenerateUniqueId(){
	DBG_ENTER("Ios.GenerateUniqueId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<121>");
	c_File* t_file=m_GetProject();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<122>");
	String t_result=String();
	DBG_LOCAL(t_result,"result")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<124>");
	do{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<125>");
		t_result=m_GenerateRandomId();
	}while(!(!t_file->p_Contains(t_result)));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<128>");
	return t_result;
}
void c_Ios::m_AddPbxBuildFile(String t_name,String t_firstId,String t_secondId,bool t_optional,String t_compilerFlags){
	DBG_ENTER("Ios.AddPbxBuildFile")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_firstId,"firstId")
	DBG_LOCAL(t_secondId,"secondId")
	DBG_LOCAL(t_optional,"optional")
	DBG_LOCAL(t_compilerFlags,"compilerFlags")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<415>");
	String t_settings=String(L"settings = {};",14);
	DBG_LOCAL(t_settings,"settings")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<416>");
	if(t_optional){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<417>");
		t_settings=t_settings.Replace(String(L"};",2),String(L"ATTRIBUTES = (Weak, ); };",25));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<419>");
	if((t_compilerFlags).Length()!=0){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<420>");
		t_settings=t_settings.Replace(String(L"};",2),String(L"COMPILER_FLAGS = \"",18)+t_compilerFlags+String(L"\"; };",5));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<423>");
	String t_match=String(L"/* End PBXBuildFile section",27);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<429>");
	String t_patchStr=String(L"\t\t",2)+t_firstId+String(L" ",1)+String(L"/* ",3)+t_name+String(L" in Frameworks */ = ",20)+String(L"{isa = PBXBuildFile; ",21)+String(L"fileRef = ",10)+t_secondId+String(L" /* ",4)+t_name+String(L" */; ",5)+t_settings+String(L" };",3);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<430>");
	c_File* t_target=m_GetProject();
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<432>");
	if(t_target->p_Contains(t_patchStr)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<434>");
	if(!t_target->p_Contains(t_match)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<435>");
		m_app->p_LogWarning(String(L"Unable to add ",14)+t_name+String(L" PBXBuildFile",13));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<436>");
		m_app->p_LogWarning(String(L"Please add this framework manually!",35));
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<438>");
		t_target->p_InsertBefore(t_match,t_patchStr);
	}
}
void c_Ios::m_AddPbxFileReference(String t_name,String t_patchStr){
	DBG_ENTER("Ios.AddPbxFileReference")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<386>");
	String t_match=String(L"/* End PBXFileReference section",31);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<387>");
	c_File* t_target=m_GetProject();
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<389>");
	if(t_target->p_Contains(t_patchStr)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<391>");
	if(!t_target->p_Contains(t_match)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<392>");
		m_app->p_LogWarning(String(L"Unable to add ",14)+t_name+String(L" PBXFileReference",17));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<393>");
		m_app->p_LogWarning(String(L"Please add this framework manually!",35));
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<395>");
		t_target->p_InsertBefore(t_match,t_patchStr);
	}
}
void c_Ios::m_AddPbxFileReferenceFile(String t_id,String t_name,String t_path,String t_type){
	DBG_ENTER("Ios.AddPbxFileReferenceFile")
	DBG_LOCAL(t_id,"id")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_type,"type")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<367>");
	String t_patchStr=String(L"\t\t",2)+t_id+String(L" ",1)+String(L"/* ",3)+t_name+String(L" */ = ",6)+String(L"{isa = PBXFileReference; ",25)+String(L"fileEncoding = 4; ",18)+String(L"lastKnownFileType = ",20)+t_type+String(L"; ",2)+String(L"path = ",7)+t_path+String(L"; ",2)+String(L"sourceTree = \"<group>\"; };",26);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<369>");
	m_AddPbxFileReference(t_name,t_patchStr);
}
void c_Ios::m_AddIconPBXGroup(String t_filename,String t_id){
	DBG_ENTER("Ios.AddIconPBXGroup")
	DBG_LOCAL(t_filename,"filename")
	DBG_LOCAL(t_id,"id")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<327>");
	c_File* t_project=m_GetProject();
	DBG_LOCAL(t_project,"project")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<328>");
	Array<int > t_lines=t_project->p_FindLines(String(L"29B97314FDCFA39411CA2CEA /* CustomTemplate */",45));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<329>");
	int t_insertLine=t_lines.At(0)+2;
	DBG_LOCAL(t_insertLine,"insertLine")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<331>");
	if(!t_project->p_GetLine(t_insertLine).Contains(String(L"children = (",12))){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<332>");
		m_app->p_LogError(String(L"Unable to add icon into PBXGroup",32));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<338>");
	t_project->p_InsertAfterLine(t_insertLine,String(L"\t\t\t\t",4)+t_id+String(L" ",1)+String(L"/* ",3)+t_filename+String(L" */,",4));
}
void c_Ios::m_AddIconPBXResourcesBuildPhase(String t_filename,String t_id){
	DBG_ENTER("Ios.AddIconPBXResourcesBuildPhase")
	DBG_LOCAL(t_filename,"filename")
	DBG_LOCAL(t_id,"id")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<312>");
	c_File* t_project=m_GetProject();
	DBG_LOCAL(t_project,"project")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<313>");
	Array<int > t_lines=t_project->p_FindLines(String(L"isa = PBXResourcesBuildPhase;",29));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<314>");
	int t_insertLine=t_lines.At(0)+2;
	DBG_LOCAL(t_insertLine,"insertLine")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<316>");
	if(!t_project->p_GetLine(t_insertLine).Contains(String(L"files = (",9))){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<317>");
		m_app->p_LogError(String(L"Unable to add icon into PBXResourcesBuildPhase",46));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<323>");
	t_project->p_InsertAfterLine(t_insertLine,String(L"\t\t\t\t",4)+t_id+String(L" ",1)+String(L"/* ",3)+t_filename+String(L" in Resources */,",17));
}
void c_Ios::m_EnsurePBXVariantGroupSection(){
	DBG_ENTER("Ios.EnsurePBXVariantGroupSection")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<132>");
	if(m_GetProject()->p_Contains(String(L"PBXVariantGroup section",23))){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<134>");
	String t_match=String(L"/* End PBXSourcesBuildPhase section */",38);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<137>");
	String t_text=String(L"\n/* Begin PBXVariantGroup section */\n/* End PBXVariantGroup section */",70);
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<138>");
	m_GetProject()->p_InsertAfter(t_match,t_text);
}
void c_Ios::m_AddPBXVariantGroup(String t_id,String t_name,Array<String > t_ids,Array<String > t_children){
	DBG_ENTER("Ios.AddPBXVariantGroup")
	DBG_LOCAL(t_id,"id")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_ids,"ids")
	DBG_LOCAL(t_children,"children")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<142>");
	m_EnsurePBXVariantGroupSection();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<144>");
	String t_childrenRecords=String();
	DBG_LOCAL(t_childrenRecords,"childrenRecords")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<145>");
	for(int t_i=0;t_i<t_children.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<147>");
		t_childrenRecords=t_childrenRecords+(String(L"\t\t\t\t",4)+t_ids.At(t_i)+String(L" /* ",4)+t_children.At(t_i)+String(L" */,\n",5));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<150>");
	String t_match=String(L"/* End PBXVariantGroup section */",33);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<159>");
	String t_text=String(L"\t\t",2)+t_id+String(L" /* ",4)+t_name+String(L" */ = {\n",8)+String(L"\t\t\tisa = PBXVariantGroup;\n",26)+String(L"\t\t\tchildren = (\n",16)+t_childrenRecords+String(L"\t\t\t);\n",6)+String(L"\t\t\tname = ",10)+t_name+String(L";\n",2)+String(L"\t\t\tsourceTree = \"<group>\";\n",27)+String(L"\t\t};",4);
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<160>");
	m_GetProject()->p_InsertBefore(t_match,t_text);
}
void c_Ios::m_AddIconPBXBuildFile(String t_filename,String t_firstId,String t_secondId){
	DBG_ENTER("Ios.AddIconPBXBuildFile")
	DBG_LOCAL(t_filename,"filename")
	DBG_LOCAL(t_firstId,"firstId")
	DBG_LOCAL(t_secondId,"secondId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<356>");
	m_GetProject()->p_InsertBefore(String(L"/* End PBXBuildFile section */",30),String(L"\t\t",2)+t_firstId+String(L" ",1)+String(L"/* ",3)+t_filename+String(L" in Resources */ = ",19)+String(L"{isa = PBXBuildFile; fileRef = ",31)+t_secondId+String(L" ",1)+String(L"/* ",3)+t_filename+String(L" */; };",7));
}
void c_Ios::m_AddIconPBXFileReference(String t_filename,String t_id){
	DBG_ENTER("Ios.AddIconPBXFileReference")
	DBG_LOCAL(t_filename,"filename")
	DBG_LOCAL(t_id,"id")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<347>");
	m_GetProject()->p_InsertBefore(String(L"/* End PBXFileReference section */",34),String(L"\t\t",2)+t_id+String(L" ",1)+String(L"/* ",3)+t_filename+String(L" */ = ",6)+String(L"{isa = PBXFileReference; lastKnownFileType = image.png; ",56)+String(L"path = \"",8)+t_filename+String(L"\"; sourceTree = \"<group>\"; };",29));
}
bool c_Ios::m_ContainsFramework(String t_name){
	DBG_ENTER("Ios.ContainsFramework")
	DBG_LOCAL(t_name,"name")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<58>");
	bool t_=m_GetProject()->p_Contains(String(L"/* ",3)+t_name+String(L" ",1));
	return t_;
}
void c_Ios::m_AddPbxFileReferenceSdk(String t_name,String t_id){
	DBG_ENTER("Ios.AddPbxFileReferenceSdk")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_id,"id")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<294>");
	String t_patchStr=String(L"\t\t",2)+t_id+String(L" ",1)+String(L"/* ",3)+t_name+String(L" */ = ",6)+String(L"{isa = PBXFileReference; ",25)+String(L"lastKnownFileType = wrapper.framework; ",39)+String(L"name = ",7)+t_name+String(L"; ",2)+String(L"path = System/Library/Frameworks/",33)+t_name+String(L"; ",2)+String(L"sourceTree = SDKROOT; };",24);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<296>");
	m_AddPbxFileReference(t_name,t_patchStr);
}
void c_Ios::m_AddPbxFrameworkBuildPhase(String t_name,String t_id){
	DBG_ENTER("Ios.AddPbxFrameworkBuildPhase")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_id,"id")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<270>");
	String t_patchStr=String(L"\t\t\t\t",4)+t_id+String(L" /* ",4)+t_name+String(L" in Frameworks */,",18);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<271>");
	String t_searchBegin=String(L"Begin PBXFrameworksBuildPhase section",37);
	DBG_LOCAL(t_searchBegin,"searchBegin")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<272>");
	String t_searchEnd=String(L"End PBXFrameworksBuildPhase section",35);
	DBG_LOCAL(t_searchEnd,"searchEnd")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<273>");
	String t_addAfter=String(L"files = (",9);
	DBG_LOCAL(t_addAfter,"addAfter")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<274>");
	c_File* t_target=m_GetProject();
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<276>");
	if(t_target->p_ContainsBetween(t_patchStr,t_searchBegin,t_searchEnd)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<278>");
	if(!t_target->p_ContainsBetween(t_addAfter,t_searchBegin,t_searchEnd)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<279>");
		m_app->p_LogWarning(String(L"Unable To add ",14)+t_name+String(L" PBXFrameworksBuildPhase",24));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<280>");
		m_app->p_LogWarning(String(L"Please add this framework manually!",35));
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<282>");
		t_target->p_InsertAfterBetween(t_addAfter,t_patchStr,t_searchBegin,t_searchEnd);
	}
}
void c_Ios::m_AddPbxGroupChild(String t_where,String t_name,String t_id){
	DBG_ENTER("Ios.AddPbxGroupChild")
	DBG_LOCAL(t_where,"where")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_id,"id")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<221>");
	String t_patchStr=String(L"\t\t\t\t",4)+t_id+String(L" /* ",4)+t_name+String(L" */,",4);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<222>");
	String t_searchBegin=String(L" /* ",4)+t_where+String(L" */ = {",7);
	DBG_LOCAL(t_searchBegin,"searchBegin")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<223>");
	String t_searchEnd=String(L"End PBXGroup section",20);
	DBG_LOCAL(t_searchEnd,"searchEnd")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<224>");
	String t_addAfter=String(L"children = (",12);
	DBG_LOCAL(t_addAfter,"addAfter")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<225>");
	c_File* t_target=m_GetProject();
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<227>");
	if(t_target->p_ContainsBetween(t_patchStr,t_searchBegin,t_searchEnd)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<229>");
	if(!t_target->p_ContainsBetween(t_addAfter,t_searchBegin,t_searchEnd)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<230>");
		m_app->p_LogWarning(String(L"Unable to add ",14)+t_name+String(L" to PBXGroup ",13)+t_where);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<231>");
		m_app->p_LogWarning(String(L"Please add this framework manually!",35));
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<233>");
		t_target->p_InsertAfterBetween(t_addAfter,t_patchStr,t_searchBegin,t_searchEnd);
	}
}
void c_Ios::m_AddFramework(String t_name,bool t_optional){
	DBG_ENTER("Ios.AddFramework")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_optional,"optional")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<14>");
	m_app->p_LogInfo(String(L"Adding framework: ",18)+t_name);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<15>");
	if(m_ContainsFramework(t_name)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<17>");
	String t_firstId=m_GenerateUniqueId();
	DBG_LOCAL(t_firstId,"firstId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<18>");
	String t_secondId=m_GenerateUniqueId();
	DBG_LOCAL(t_secondId,"secondId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<20>");
	m_AddPbxFileReferenceSdk(t_name,t_secondId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<22>");
	m_AddPbxBuildFile(t_name,t_firstId,t_secondId,t_optional,String());
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<23>");
	m_AddPbxFrameworkBuildPhase(t_name,t_firstId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<24>");
	m_AddPbxGroupChild(String(L"Frameworks",10),t_name,t_secondId);
}
void c_Ios::m_AddPbxFileReferenceLProj(String t_id,String t_name,String t_path){
	DBG_ENTER("Ios.AddPbxFileReferenceLProj")
	DBG_LOCAL(t_id,"id")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<380>");
	String t_patchStr=String(L"\t\t",2)+t_id+String(L" ",1)+String(L"/* ",3)+t_name+String(L" */ = ",6)+String(L"{isa = PBXFileReference; ",25)+String(L"lastKnownFileType = text.plist.strings; ",40)+String(L"name = \"",8)+t_name+String(L"\"; ",3)+String(L"path = \"",8)+t_name+String(L".lproj/",7)+t_path+String(L"\"; ",3)+String(L"sourceTree = \"<group>\"; };",26);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<382>");
	m_AddPbxFileReference(t_name,t_patchStr);
}
void c_Ios::m_AddKnownRegion(String t_region){
	DBG_ENTER("Ios.AddKnownRegion")
	DBG_LOCAL(t_region,"region")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<182>");
	String t_text=String(L"\t\t\t\t\"",5)+t_region+String(L"\",",2);
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<183>");
	m_GetProject()->p_InsertAfter(String(L"knownRegions = (",16),t_text);
}
void c_Ios::m_RegisterPxbGroup(String t_name,String t_id){
	DBG_ENTER("Ios.RegisterPxbGroup")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_id,"id")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<255>");
	Array<int > t_lines=m_GetProject()->p_FindLines(String(L"/* CustomTemplate */ = {",24));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<256>");
	if(t_lines.Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<257>");
		m_app->p_LogError(String(L"Unable to locate CustomTemplate definition",42));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<260>");
	int t_childLine=t_lines.At(0)+2;
	DBG_LOCAL(t_childLine,"childLine")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<261>");
	if(!m_GetProject()->p_GetLine(t_childLine).Contains(String(L"children = (",12))){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<262>");
		m_app->p_LogError(String(L"Unable to locate children of CustomTemplate",43));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<265>");
	String t_text=String(L"\t\t\t\t",4)+t_id+String(L" /* ",4)+t_name+String(L" */,",4);
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<266>");
	m_GetProject()->p_InsertAfterLine(t_childLine,t_text);
}
void c_Ios::m_AddPbxGroup(String t_name,String t_id,String t_path){
	DBG_ENTER("Ios.AddPbxGroup")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_id,"id")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<239>");
	String t_headline=t_id+String(L" /* ",4)+t_name+String(L" */ = {",7);
	DBG_LOCAL(t_headline,"headline")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<240>");
	if(m_GetProject()->p_Contains(t_headline)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<241>");
	bbPrint(t_name);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<249>");
	String t_text=String(L"\t\t",2)+t_headline+String(L"\n",1)+String(L"\t\t\tisa = PBXGroup;\n",19)+String(L"\t\t\tchildren = (\n",16)+String(L"\t\t\t);\n",6)+String(L"\t\t\tpath = ",10)+t_path+String(L";\n",2)+String(L"\t\t\tsourceTree = \"<group>\";\n",27)+String(L"\t\t};",4);
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<251>");
	m_GetProject()->p_InsertBefore(String(L"/* End PBXGroup section */",26),t_text);
}
void c_Ios::m_AddPbxResource(String t_name,String t_id){
	DBG_ENTER("Ios.AddPbxResource")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_id,"id")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<400>");
	Array<int > t_lines=m_GetProject()->p_FindLines(String(L" /* Resources */ = {",20));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<401>");
	if(t_lines.Length()!=2){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<402>");
		m_app->p_LogError(String(L"Unable to detect PBXResourcesBuildPhase section",47));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<405>");
	int t_filesLine=t_lines.At(1)+3;
	DBG_LOCAL(t_filesLine,"filesLine")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<406>");
	if(!m_GetProject()->p_GetLine(t_filesLine).Contains(String(L"files = (",9))){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<407>");
		m_app->p_LogError(String(L"Unable to find files in PBXResourcesBuildPhase",46));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<410>");
	String t_text=String(L"\t\t\t\t",4)+t_id+String(L" /* ",4)+t_name+String(L" in Resources */,",17);
	DBG_LOCAL(t_text,"text")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<411>");
	m_GetProject()->p_InsertAfterLine(t_filesLine,t_text);
}
c_File* c_Ios::m_GetPlist(){
	DBG_ENTER("Ios.GetPlist")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<50>");
	c_File* t_=m_app->p_TargetFile(String(L"MonkeyGame-Info.plist",21));
	return t_;
}
int c_Ios::m_GetKeyLine(c_File* t_plist,String t_key){
	DBG_ENTER("Ios.GetKeyLine")
	DBG_LOCAL(t_plist,"plist")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<452>");
	Array<int > t_lines=t_plist->p_FindLines(String(L"<key>",5)+t_key+String(L"</key>",6));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<454>");
	if(t_lines.Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<455>");
		m_app->p_LogError(String(L"Found zero Or more than one setting For ",40)+t_key);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<458>");
	return t_lines.At(0);
}
void c_Ios::m_ValidateValueLine(c_File* t_plist,int t_line){
	DBG_ENTER("Ios.ValidateValueLine")
	DBG_LOCAL(t_plist,"plist")
	DBG_LOCAL(t_line,"line")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<462>");
	String t_lineStr=t_plist->p_GetLine(t_line);
	DBG_LOCAL(t_lineStr,"lineStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<463>");
	bool t_hasStrStart=t_lineStr.Contains(String(L"<string>",8));
	DBG_LOCAL(t_hasStrStart,"hasStrStart")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<464>");
	bool t_hasStrEnd=t_lineStr.Contains(String(L"</string>",9));
	DBG_LOCAL(t_hasStrEnd,"hasStrEnd")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<466>");
	if(t_hasStrStart && t_hasStrEnd){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<468>");
	m_app->p_LogError(String(L"Expected <string></string> after <key>CFBundleVersion</key>",59));
}
void c_Ios::m_UpdatePlistSetting(String t_key,String t_value){
	DBG_ENTER("Ios.UpdatePlistSetting")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<110>");
	c_File* t_plist=m_GetPlist();
	DBG_LOCAL(t_plist,"plist")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<112>");
	int t_keyLine=m_GetKeyLine(t_plist,t_key);
	DBG_LOCAL(t_keyLine,"keyLine")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<113>");
	int t_valueLine=t_keyLine+1;
	DBG_LOCAL(t_valueLine,"valueLine")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<114>");
	m_ValidateValueLine(t_plist,t_valueLine);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<116>");
	String t_newVersion=String(L"\t<string>",9)+t_value+String(L"</string>",9);
	DBG_LOCAL(t_newVersion,"newVersion")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<117>");
	t_plist->p_ReplaceLine(t_valueLine,t_newVersion);
}
void c_Ios::m_AddPbxFileReferencePath(String t_name,String t_id){
	DBG_ENTER("Ios.AddPbxFileReferencePath")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_id,"id")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<306>");
	String t_patchStr=String(L"\t\t",2)+t_id+String(L" ",1)+String(L"/* ",3)+t_name+String(L" */ = ",6)+String(L"{isa = PBXFileReference; ",25)+String(L"lastKnownFileType = wrapper.framework; ",39)+String(L"path = ",7)+t_name+String(L"; ",2)+String(L"sourceTree = \"<group>\"; };",26);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<308>");
	m_AddPbxFileReference(t_name,t_patchStr);
}
void c_Ios::m_EnsureSearchPathWithSRCROOT(String t_config){
	DBG_ENTER("Ios.EnsureSearchPathWithSRCROOT")
	DBG_LOCAL(t_config,"config")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<187>");
	String t_searchStr=String(L"FRAMEWORK_SEARCH_PATHS",22);
	DBG_LOCAL(t_searchStr,"searchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<193>");
	String t_searchBegin=String(L"/* ",3)+t_config+String(L" */ = {",7);
	DBG_LOCAL(t_searchBegin,"searchBegin")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<195>");
	String t_searchEnd=String(L"name = ",7)+t_config+String(L";",1);
	DBG_LOCAL(t_searchEnd,"searchEnd")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<196>");
	c_File* t_target=m_GetProject();
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<199>");
	if(!t_target->p_ContainsBetween(t_searchStr,t_searchBegin,t_searchEnd)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<201>");
		if(!t_target->p_Contains(t_searchBegin)){
			DBG_BLOCK();
			bbError(t_searchBegin+String(L" --- Not found! in ",19)+t_target->p_Get());
		}
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<202>");
		String t_addAfter=String(L"buildSettings = {",17);
		DBG_LOCAL(t_addAfter,"addAfter")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<207>");
		String t_patchStr=String(L"\t\t\t\tFRAMEWORK_SEARCH_PATHS = (\n\t\t\t\t\t\"$(inherited)\",\n\t\t\t\t\t\"\\\"$(SRCROOT)\\\"\",\n\t\t\t\t);",81);
		DBG_LOCAL(t_patchStr,"patchStr")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<208>");
		t_target->p_InsertAfterBetween(t_addAfter,t_patchStr,t_searchBegin,t_searchEnd);
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<213>");
	if(!t_target->p_ContainsBetween(String(L"$(SRCROOT)",10),t_searchBegin,t_searchEnd)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<214>");
		String t_addAfter2=String(L"FRAMEWORK_SEARCH_PATHS = (",26);
		DBG_LOCAL(t_addAfter2,"addAfter")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<215>");
		String t_patchStr2=String(L"\t\t\t\t\t\"\\\"$(SRCROOT)\\\"\",",22);
		DBG_LOCAL(t_patchStr2,"patchStr")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<216>");
		t_target->p_InsertAfterBetween(t_addAfter2,t_patchStr2,t_searchBegin,t_searchEnd);
	}
}
void c_Ios::m_AddFrameworkFromPath(String t_name,bool t_optional){
	DBG_ENTER("Ios.AddFrameworkFromPath")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_optional,"optional")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<28>");
	m_app->p_LogInfo(String(L"Adding framework from path: ",28)+t_name);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<29>");
	if(m_ContainsFramework(t_name)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<31>");
	String t_firstId=m_GenerateUniqueId();
	DBG_LOCAL(t_firstId,"firstId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<32>");
	String t_secondId=m_GenerateUniqueId();
	DBG_LOCAL(t_secondId,"secondId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<34>");
	m_AddPbxFileReferencePath(t_name,t_secondId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<36>");
	m_EnsureSearchPathWithSRCROOT(String(L"Debug",5));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<37>");
	m_EnsureSearchPathWithSRCROOT(String(L"Release",7));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<39>");
	m_AddPbxBuildFile(t_name,t_firstId,t_secondId,t_optional,String());
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<41>");
	m_AddPbxFrameworkBuildPhase(t_name,t_firstId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<42>");
	m_AddPbxGroupChild(String(L"Frameworks",10),t_name,t_secondId);
}
void c_Ios::m_UpdateDeploymentTarget(String t_newValue){
	DBG_ENTER("Ios.UpdateDeploymentTarget")
	DBG_LOCAL(t_newValue,"newValue")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<75>");
	c_File* t_file=m_GetProject();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<76>");
	Array<int > t_lines=t_file->p_FindLines(String(L"IPHONEOS_DEPLOYMENT_TARGET = ",29));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<78>");
	Array<int > t_=t_lines;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		int t_l=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_l,"l")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<79>");
		t_file->p_ReplaceLine(t_l,String(L"\t\t\t\tIPHONEOS_DEPLOYMENT_TARGET = ",33)+t_newValue+String(L";",1));
	}
}
c_File* c_Ios::m_GetMainSource(){
	DBG_ENTER("Ios.GetMainSource")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<54>");
	c_File* t_=m_app->p_TargetFile(String(L"main.mm",7));
	return t_;
}
String c_Ios::m_ExtractSettingKey(String t_row){
	DBG_ENTER("Ios.ExtractSettingKey")
	DBG_LOCAL(t_row,"row")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<445>");
	Array<String > t_parts=t_row.Split(String(L" ",1));
	DBG_LOCAL(t_parts,"parts")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<446>");
	String t_key=t_parts.At(t_parts.Length()-1);
	DBG_LOCAL(t_key,"key")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<447>");
	String t_=t_key.Replace(String(L";",1),String()).Trim();
	return t_;
}
String c_Ios::m_GetProjectSetting(String t_key){
	DBG_ENTER("Ios.GetProjectSetting")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<84>");
	c_File* t_file=m_GetProject();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<85>");
	Array<int > t_lines=t_file->p_FindLines(t_key+String(L" = ",3));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<87>");
	if(t_lines.Length()==0){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<88>");
		m_app->p_LogError(String(L"No current ",11)+t_key+String(L" found",6));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<91>");
	String t_lastValue=String();
	DBG_LOCAL(t_lastValue,"lastValue")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<92>");
	for(int t_i=0;t_i<t_lines.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<93>");
		String t_currValue=m_ExtractSettingKey(t_file->p_GetLine(t_lines.At(t_i)));
		DBG_LOCAL(t_currValue,"currValue")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<95>");
		if(((t_lastValue).Length()!=0) && t_lastValue!=t_currValue){
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<96>");
			m_app->p_LogError(String(L"Different old ",14)+t_key+String(L" settings found",15));
		}
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<99>");
		t_lastValue=t_currValue;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<102>");
	if(t_lastValue.Length()<=0){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<103>");
		m_app->p_LogError(String(L"No old ",7)+t_key+String(L" found",6));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<106>");
	return t_lastValue;
}
void c_Ios::m_UpdateProjectSetting(String t_key,String t_newValue){
	DBG_ENTER("Ios.UpdateProjectSetting")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_newValue,"newValue")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<62>");
	String t_oldValue=m_GetProjectSetting(t_key);
	DBG_LOCAL(t_oldValue,"oldValue")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<63>");
	String t_oldRow=t_key+String(L" = ",3)+t_oldValue+String(L";",1);
	DBG_LOCAL(t_oldRow,"oldRow")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<64>");
	String t_oldRowQuoted=t_key+String(L" = \"",4)+t_oldValue+String(L"\";",2);
	DBG_LOCAL(t_oldRowQuoted,"oldRowQuoted")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<66>");
	c_File* t_file=m_GetProject();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<68>");
	String t_2[]={t_oldRow,t_oldRowQuoted};
	Array<String > t_=Array<String >(t_2,2);
	int t_3=0;
	while(t_3<t_.Length()){
		DBG_BLOCK();
		String t_old=t_.At(t_3);
		t_3=t_3+1;
		DBG_LOCAL(t_old,"old")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<69>");
		if(!t_file->p_Contains(t_old)){
			DBG_BLOCK();
			continue;
		}
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/ios.monkey<70>");
		t_file->p_Replace(t_old,t_key+String(L" = \"",4)+t_newValue+String(L"\";",2));
	}
}
void c_Ios::mark(){
	Object::mark();
}
String c_Ios::debug(){
	String t="(Ios)\n";
	t+=dbg_decl("app",&c_Ios::m_app);
	return t;
}
int bb_random_Seed;
Float bb_random_Rnd(){
	DBG_ENTER("Rnd")
	DBG_INFO("/Applications/Monkey/modules/monkey/random.monkey<21>");
	bb_random_Seed=bb_random_Seed*1664525+1013904223|0;
	DBG_INFO("/Applications/Monkey/modules/monkey/random.monkey<22>");
	Float t_=Float(bb_random_Seed>>8&16777215)/FLOAT(16777216.0);
	return t_;
}
Float bb_random_Rnd2(Float t_low,Float t_high){
	DBG_ENTER("Rnd")
	DBG_LOCAL(t_low,"low")
	DBG_LOCAL(t_high,"high")
	DBG_INFO("/Applications/Monkey/modules/monkey/random.monkey<30>");
	Float t_=bb_random_Rnd3(t_high-t_low)+t_low;
	return t_;
}
Float bb_random_Rnd3(Float t_range){
	DBG_ENTER("Rnd")
	DBG_LOCAL(t_range,"range")
	DBG_INFO("/Applications/Monkey/modules/monkey/random.monkey<26>");
	Float t_=bb_random_Rnd()*t_range;
	return t_;
}
c_IosAppirater::c_IosAppirater(){
}
void c_IosAppirater::m_CopyFramework(c_App* t_app){
	DBG_ENTER("IosAppirater.CopyFramework")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<26>");
	c_Dir* t_src=t_app->p_SourceDir(String(L"appirater",9));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<27>");
	c_Dir* t_dst=t_app->p_TargetDir(String(L"appirater",9));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<28>");
	t_src->p_CopyTo3(t_dst,true,false,true);
}
Array<String > c_IosAppirater::m_GetRegions(){
	DBG_ENTER("IosAppirater.GetRegions")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<87>");
	String t_2[]={String(L"ca",2),String(L"cs",2),String(L"da",2),String(L"de",2),String(L"el",2),String(L"en",2),String(L"es",2),String(L"fi",2),String(L"fr",2),String(L"he",2),String(L"hu",2),String(L"it",2),String(L"ja",2),String(L"ko",2),String(L"nb",2),String(L"nl",2),String(L"pl",2),String(L"pt",2),String(L"ru",2),String(L"sk",2),String(L"sv",2),String(L"tr",2),String(L"zh-Hans",7),String(L"zh-Hant",7)};
	Array<String > t_=Array<String >(t_2,24);
	return t_;
}
void c_IosAppirater::m_AddRegionFiles(c_App* t_app){
	DBG_ENTER("IosAppirater.AddRegionFiles")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<51>");
	Array<String > t_regions=m_GetRegions();
	DBG_LOCAL(t_regions,"regions")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<52>");
	Array<String > t_ids=Array<String >(t_regions.Length());
	DBG_LOCAL(t_ids,"ids")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<54>");
	for(int t_i=0;t_i<t_regions.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<55>");
		String t_id=c_Ios::m_GenerateUniqueId();
		DBG_LOCAL(t_id,"id")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<56>");
		t_ids.At(t_i)=t_id;
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<61>");
		c_Ios::m_AddPbxFileReferenceLProj(t_ids.At(t_i),t_regions.At(t_i),String(L"AppiraterLocalizable.strings",28));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<62>");
		c_Ios::m_AddKnownRegion(t_regions.At(t_i));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<65>");
	String t_variantId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_variantId,"variantId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<70>");
	c_Ios::m_AddPBXVariantGroup(t_variantId,String(L"AppiraterLocalizable.strings",28),t_ids,t_regions);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<72>");
	String t_groupId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_groupId,"groupId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<73>");
	c_Ios::m_RegisterPxbGroup(String(L"appirater",9),t_groupId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<74>");
	c_Ios::m_AddPbxGroup(String(L"appirater",9),t_groupId,String(L"appirater",9));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<75>");
	c_Ios::m_AddPbxGroupChild(String(L"appirater",9),String(L"AppiraterLocalizable.strings",28),t_variantId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<77>");
	String t_fileId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_fileId,"fileId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<78>");
	c_Ios::m_AddPbxBuildFile(String(L"AppiraterLocalizable.strings",28),t_fileId,t_variantId,false,String());
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<79>");
	c_Ios::m_AddPbxResource(String(L"AppiraterLocalizable.strings",28),t_fileId);
}
void c_IosAppirater::m_AddSourceFiles(c_App* t_app){
	DBG_ENTER("IosAppirater.AddSourceFiles")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<35>");
	String t_[]={c_Ios::m_GenerateUniqueId(),String(L"Appirater.m",11),String(L"sourcecode.c.objc",17)};
	String t_2[]={c_Ios::m_GenerateUniqueId(),String(L"Appirater.h",11),String(L"sourcecode.c.h",14)};
	String t_3[]={c_Ios::m_GenerateUniqueId(),String(L"AppiraterDelegate.h",19),String(L"sourcecode.c.h",14)};
	Array<String > t_4[]={Array<String >(t_,3),Array<String >(t_2,3),Array<String >(t_3,3)};
	Array<Array<String > > t_files=Array<Array<String > >(t_4,3);
	DBG_LOCAL(t_files,"files")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<37>");
	Array<Array<String > > t_5=t_files;
	int t_6=0;
	while(t_6<t_5.Length()){
		DBG_BLOCK();
		Array<String > t_row=t_5.At(t_6);
		t_6=t_6+1;
		DBG_LOCAL(t_row,"row")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<38>");
		c_Ios::m_AddPbxFileReferenceFile(t_row.At(0),t_row.At(1),t_row.At(1),t_row.At(2));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<39>");
		c_Ios::m_AddPbxGroupChild(String(L"appirater",9),t_row.At(1),t_row.At(0));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<43>");
	String t_fileId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_fileId,"fileId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<44>");
	c_Ios::m_AddPbxBuildFile(String(L"Appirater.m",11),t_fileId,t_files.At(0).At(0),false,String(L"-fobjc-arc",10));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<47>");
	c_Ios::m_GetProject()->p_InsertAfter(String(L"/* main.mm in Sources */,",25),String(L"\t\t\t\t",4)+t_fileId+String(L" /* Appirater.m in Sources */,",30));
}
void c_IosAppirater::p_Run(c_App* t_app){
	DBG_ENTER("IosAppirater.Run")
	c_IosAppirater *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<14>");
	c_Ios::m_AddFramework(String(L"CFNetwork.framework",19),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<15>");
	c_Ios::m_AddFramework(String(L"StoreKit.framework",18),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<16>");
	c_Ios::m_AddFramework(String(L"SystemConfiguration.framework",29),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<18>");
	m_CopyFramework(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<19>");
	m_AddRegionFiles(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<20>");
	m_AddSourceFiles(t_app);
}
c_IosAppirater* c_IosAppirater::m_new(){
	DBG_ENTER("IosAppirater.new")
	c_IosAppirater *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosappirater.monkey<12>");
	return this;
}
void c_IosAppirater::mark(){
	Object::mark();
}
String c_IosAppirater::debug(){
	String t="(IosAppirater)\n";
	return t;
}
c_IosBundleId::c_IosBundleId(){
}
String c_IosBundleId::m_GetBundleId(c_App* t_app){
	DBG_ENTER("IosBundleId.GetBundleId")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosbundleid.monkey<19>");
	if(t_app->p_GetAdditionArguments().Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosbundleid.monkey<20>");
		t_app->p_LogError(String(L"Bundle id argument missing",26));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosbundleid.monkey<23>");
	String t_=t_app->p_GetAdditionArguments().At(0);
	return t_;
}
void c_IosBundleId::p_Run(c_App* t_app){
	DBG_ENTER("IosBundleId.Run")
	c_IosBundleId *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosbundleid.monkey<13>");
	c_Ios::m_UpdatePlistSetting(String(L"CFBundleIdentifier",18),m_GetBundleId(t_app));
}
c_IosBundleId* c_IosBundleId::m_new(){
	DBG_ENTER("IosBundleId.new")
	c_IosBundleId *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosbundleid.monkey<11>");
	return this;
}
void c_IosBundleId::mark(){
	Object::mark();
}
String c_IosBundleId::debug(){
	String t="(IosBundleId)\n";
	return t;
}
c_IosChartboost::c_IosChartboost(){
}
void c_IosChartboost::p_ConvertToFrameworkDir(c_App* t_app){
	DBG_ENTER("IosChartboost.ConvertToFrameworkDir")
	c_IosChartboost *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<40>");
	String t_fw=String(L"Chartboost.framework",20);
	DBG_LOCAL(t_fw,"fw")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<42>");
	t_app->p_TargetDir(t_fw+String(L"/Headers",8))->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<43>");
	t_app->p_TargetDir(t_fw+String(L"/Resources",10))->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<45>");
	c_File* t_header=t_app->p_TargetFile(t_fw+String(L"/Chartboost.h",13));
	DBG_LOCAL(t_header,"header")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<46>");
	t_header->p_CopyTo2(t_app->p_TargetFile(t_fw+String(L"/Headers/Chartboost.h",21)));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<47>");
	t_header->p_Remove();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<49>");
	c_File* t_lib=t_app->p_TargetFile(t_fw+String(L"/libChartboost.a",16));
	DBG_LOCAL(t_lib,"lib")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<50>");
	t_lib->p_CopyTo2(t_app->p_TargetFile(t_fw+String(L"/Chartboost",11)));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<51>");
	t_lib->p_Remove();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<53>");
	c_File* t_plist=t_app->p_TargetFile(t_fw+String(L"/Resources/Info.plist",21));
	DBG_LOCAL(t_plist,"plist")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<78>");
	t_plist->p_Set(String(L"<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"><plist version=\"1.0\"><dict>\t<key>CFBuildHash</key>\t<string></string>\t<key>CFBundleDevelopmentRegion</key>\t<string>English</string>\t<key>CFBundleExecutable</key>\t<string>Chartboost</string>\t<key>CFBundleIdentifier</key>\t<string>com.chartboost.Chartboost</string>\t<key>CFBundleInfoDictionaryVersion</key>\t<string>6.0</string>\t<key>CFBundlePackageType</key>\t<string>FMWK</string>\t<key>CFBundleShortVersionString</key>\t<string>3.2</string>\t<key>CFBundleSignature</key>\t<string>?</string>\t<key>CFBundleVersion</key>\t<string>3.2</string></dict></plist>",686));
}
void c_IosChartboost::p_CopyFramework(c_App* t_app){
	DBG_ENTER("IosChartboost.CopyFramework")
	c_IosChartboost *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<30>");
	c_Dir* t_src=t_app->p_SourceDir(String(L"Chartboost-3.2",14));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<31>");
	c_Dir* t_dst=t_app->p_TargetDir(String(L"Chartboost.framework",20));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<33>");
	if(t_dst->p_Exists()){
		DBG_BLOCK();
		t_dst->p_Remove2(true);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<34>");
	t_src->p_CopyTo3(t_dst,true,true,true);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<36>");
	p_ConvertToFrameworkDir(t_app);
}
void c_IosChartboost::p_Run(c_App* t_app){
	DBG_ENTER("IosChartboost.Run")
	c_IosChartboost *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<17>");
	c_Ios::m_AddFramework(String(L"AdSupport.framework",19),true);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<18>");
	c_Ios::m_AddFramework(String(L"StoreKit.framework",18),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<19>");
	c_Ios::m_AddFramework(String(L"SystemConfiguration.framework",29),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<20>");
	c_Ios::m_AddFramework(String(L"QuartzCore.framework",20),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<21>");
	c_Ios::m_AddFramework(String(L"GameKit.framework",17),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<23>");
	c_Ios::m_AddFrameworkFromPath(String(L"Chartboost.framework",20),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<24>");
	p_CopyFramework(t_app);
}
c_IosChartboost* c_IosChartboost::m_new(){
	DBG_ENTER("IosChartboost.new")
	c_IosChartboost *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioschartboost.monkey<13>");
	return this;
}
void c_IosChartboost::mark(){
	Object::mark();
}
String c_IosChartboost::debug(){
	String t="(IosChartboost)\n";
	return t;
}
c_IosCompressPngFiles::c_IosCompressPngFiles(){
	m_app=0;
}
bool c_IosCompressPngFiles::p_IsEnabled(){
	DBG_ENTER("IosCompressPngFiles.IsEnabled")
	c_IosCompressPngFiles *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<51>");
	if(m_app->p_GetAdditionArguments().Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<52>");
		m_app->p_LogError(String(L"Mode argument (valid values 0/1) missing",40));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<55>");
	String t_value=m_app->p_GetAdditionArguments().At(0);
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<56>");
	if(t_value!=String(L"0",1) && t_value!=String(L"1",1)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<57>");
		m_app->p_LogError(String(L"Invalid mode given. Valid modes are 0 and 1.",44));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<60>");
	bool t_=t_value==String(L"1",1);
	return t_;
}
void c_IosCompressPngFiles::p_RemoveOldSettings(){
	DBG_ENTER("IosCompressPngFiles.RemoveOldSettings")
	c_IosCompressPngFiles *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<29>");
	c_File* t_file=c_Ios::m_GetProject();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<30>");
	Array<int > t_lines=t_file->p_FindLines(String(L"COMPRESS_PNG_FILES = ",21));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<32>");
	for(int t_i=t_lines.Length()-1;t_i>=0;t_i=t_i+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<33>");
		t_file->p_RemoveLine(t_lines.At(t_i));
	}
}
void c_IosCompressPngFiles::p_AddSettings(bool t_enabled){
	DBG_ENTER("IosCompressPngFiles.AddSettings")
	c_IosCompressPngFiles *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_enabled,"enabled")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<38>");
	String t_value=String(L"NO",2);
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<39>");
	if(t_enabled){
		DBG_BLOCK();
		t_value=String(L"YES",3);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<41>");
	c_File* t_file=c_Ios::m_GetProject();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<42>");
	Array<int > t_lines=t_file->p_FindLines(String(L"buildSettings = {",17));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<44>");
	String t_newRow=String(L"\t\t\t\tCOMPRESS_PNG_FILES = ",25)+t_value+String(L";",1);
	DBG_LOCAL(t_newRow,"newRow")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<45>");
	for(int t_i=t_lines.Length()-1;t_i>=0;t_i=t_i+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<46>");
		t_file->p_InsertAfterLine(t_lines.At(t_i),t_newRow);
	}
}
void c_IosCompressPngFiles::p_Run(c_App* t_app){
	DBG_ENTER("IosCompressPngFiles.Run")
	c_IosCompressPngFiles *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<19>");
	gc_assign(this->m_app,t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<21>");
	bool t_enabled=p_IsEnabled();
	DBG_LOCAL(t_enabled,"enabled")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<22>");
	p_RemoveOldSettings();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<23>");
	p_AddSettings(t_enabled);
}
c_IosCompressPngFiles* c_IosCompressPngFiles::m_new(){
	DBG_ENTER("IosCompressPngFiles.new")
	c_IosCompressPngFiles *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioscompresspngfiles.monkey<11>");
	return this;
}
void c_IosCompressPngFiles::mark(){
	Object::mark();
	gc_mark_q(m_app);
}
String c_IosCompressPngFiles::debug(){
	String t="(IosCompressPngFiles)\n";
	t+=dbg_decl("app",&m_app);
	return t;
}
c_IosDeploymentTarget::c_IosDeploymentTarget(){
}
String c_IosDeploymentTarget::p_GetTarget(c_App* t_app){
	DBG_ENTER("IosDeploymentTarget.GetTarget")
	c_IosDeploymentTarget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosdeploymenttarget.monkey<19>");
	if(t_app->p_GetAdditionArguments().Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosdeploymenttarget.monkey<20>");
		t_app->p_LogError(String(L"Target version argument missing",31));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosdeploymenttarget.monkey<23>");
	String t_=t_app->p_GetAdditionArguments().At(0);
	return t_;
}
void c_IosDeploymentTarget::p_Run(c_App* t_app){
	DBG_ENTER("IosDeploymentTarget.Run")
	c_IosDeploymentTarget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosdeploymenttarget.monkey<13>");
	c_Ios::m_UpdateDeploymentTarget(p_GetTarget(t_app));
}
c_IosDeploymentTarget* c_IosDeploymentTarget::m_new(){
	DBG_ENTER("IosDeploymentTarget.new")
	c_IosDeploymentTarget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosdeploymenttarget.monkey<11>");
	return this;
}
void c_IosDeploymentTarget::mark(){
	Object::mark();
}
String c_IosDeploymentTarget::debug(){
	String t="(IosDeploymentTarget)\n";
	return t;
}
c_IosFlurry::c_IosFlurry(){
}
void c_IosFlurry::p_ConvertToFrameworkDir(c_App* t_app){
	DBG_ENTER("IosFlurry.ConvertToFrameworkDir")
	c_IosFlurry *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<38>");
	String t_fw=String(L"Flurry.framework",16);
	DBG_LOCAL(t_fw,"fw")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<40>");
	t_app->p_TargetDir(t_fw+String(L"/Headers",8))->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<41>");
	t_app->p_TargetDir(t_fw+String(L"/Resources",10))->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<43>");
	c_File* t_header=t_app->p_TargetFile(t_fw+String(L"/Flurry.h",9));
	DBG_LOCAL(t_header,"header")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<44>");
	t_header->p_CopyTo2(t_app->p_TargetFile(t_fw+String(L"/Headers/Flurry.h",17)));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<45>");
	t_header->p_Remove();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<47>");
	c_File* t_lib=t_app->p_TargetFile(t_fw+String(L"/libFlurry.a",12));
	DBG_LOCAL(t_lib,"lib")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<48>");
	t_lib->p_CopyTo2(t_app->p_TargetFile(t_fw+String(L"/Flurry",7)));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<49>");
	t_lib->p_Remove();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<51>");
	c_File* t_plist=t_app->p_TargetFile(t_fw+String(L"/Resources/Info.plist",21));
	DBG_LOCAL(t_plist,"plist")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<76>");
	t_plist->p_Set(String(L"<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"><plist version=\"1.0\"><dict>\t<key>CFBuildHash</key>\t<string></string>\t<key>CFBundleDevelopmentRegion</key>\t<string>English</string>\t<key>CFBundleExecutable</key>\t<string>Flurry</string>\t<key>CFBundleIdentifier</key>\t<string>com.flurry.Flurry</string>\t<key>CFBundleInfoDictionaryVersion</key>\t<string>6.0</string>\t<key>CFBundlePackageType</key>\t<string>FMWK</string>\t<key>CFBundleShortVersionString</key>\t<string>4.1.0</string>\t<key>CFBundleSignature</key>\t<string>?</string>\t<key>CFBundleVersion</key>\t<string>4.1.0</string></dict></plist>",678));
}
void c_IosFlurry::p_CopyFramework(c_App* t_app){
	DBG_ENTER("IosFlurry.CopyFramework")
	c_IosFlurry *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<28>");
	c_Dir* t_src=t_app->p_SourceDir(String(L"Flurry",6));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<29>");
	c_Dir* t_dst=t_app->p_TargetDir(String(L"Flurry.framework",16));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<31>");
	if(t_dst->p_Exists()){
		DBG_BLOCK();
		t_dst->p_Remove2(true);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<32>");
	t_src->p_CopyTo3(t_dst,true,true,true);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<34>");
	p_ConvertToFrameworkDir(t_app);
}
void c_IosFlurry::p_Run(c_App* t_app){
	DBG_ENTER("IosFlurry.Run")
	c_IosFlurry *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<17>");
	c_Ios::m_AddFramework(String(L"SystemConfiguration.framework",29),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<19>");
	c_Ios::m_AddFrameworkFromPath(String(L"Flurry.framework",16),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<20>");
	p_CopyFramework(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<22>");
	t_app->p_LogInfo(String(L"Monkey interface can be found here: http://goo.gl/d7jh5",55));
}
c_IosFlurry* c_IosFlurry::m_new(){
	DBG_ENTER("IosFlurry.new")
	c_IosFlurry *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurry.monkey<13>");
	return this;
}
void c_IosFlurry::mark(){
	Object::mark();
}
String c_IosFlurry::debug(){
	String t="(IosFlurry)\n";
	return t;
}
c_IosFlurryAds::c_IosFlurryAds(){
}
void c_IosFlurryAds::p_ConvertToFrameworkDir(c_App* t_app){
	DBG_ENTER("IosFlurryAds.ConvertToFrameworkDir")
	c_IosFlurryAds *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<38>");
	String t_fw=String(L"FlurryAds.framework",19);
	DBG_LOCAL(t_fw,"fw")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<40>");
	t_app->p_TargetDir(t_fw+String(L"/Headers",8))->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<41>");
	t_app->p_TargetDir(t_fw+String(L"/Resources",10))->p_Create();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<43>");
	c_File* t_header=t_app->p_TargetFile(t_fw+String(L"/FlurryAds.h",12));
	DBG_LOCAL(t_header,"header")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<44>");
	t_header->p_CopyTo2(t_app->p_TargetFile(t_fw+String(L"/Headers/FlurryAds.h",20)));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<45>");
	t_header->p_Remove();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<47>");
	c_File* t_header2=t_app->p_TargetFile(t_fw+String(L"/FlurryAdDelegate.h",19));
	DBG_LOCAL(t_header2,"header2")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<48>");
	t_header2->p_CopyTo2(t_app->p_TargetFile(t_fw+String(L"/Headers/FlurryAdDelegate.h",27)));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<49>");
	t_header2->p_Remove();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<51>");
	c_File* t_lib=t_app->p_TargetFile(t_fw+String(L"/libFlurryAds.a",15));
	DBG_LOCAL(t_lib,"lib")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<52>");
	t_lib->p_CopyTo2(t_app->p_TargetFile(t_fw+String(L"/FlurryAds",10)));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<53>");
	t_lib->p_Remove();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<55>");
	c_File* t_plist=t_app->p_TargetFile(t_fw+String(L"/Resources/Info.plist",21));
	DBG_LOCAL(t_plist,"plist")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<80>");
	t_plist->p_Set(String(L"<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"><plist version=\"1.0\"><dict>\t<key>CFBuildHash</key>\t<string></string>\t<key>CFBundleDevelopmentRegion</key>\t<string>English</string>\t<key>CFBundleExecutable</key>\t<string>FlurryAds</string>\t<key>CFBundleIdentifier</key>\t<string>com.flurry.FlurryAds</string>\t<key>CFBundleInfoDictionaryVersion</key>\t<string>6.0</string>\t<key>CFBundlePackageType</key>\t<string>FMWK</string>\t<key>CFBundleShortVersionString</key>\t<string>4.1.0</string>\t<key>CFBundleSignature</key>\t<string>?</string>\t<key>CFBundleVersion</key>\t<string>4.1.0</string></dict></plist>",684));
}
void c_IosFlurryAds::p_CopyFramework(c_App* t_app){
	DBG_ENTER("IosFlurryAds.CopyFramework")
	c_IosFlurryAds *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<28>");
	c_Dir* t_src=t_app->p_SourceDir(String(L"FlurryAds",9));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<29>");
	c_Dir* t_dst=t_app->p_TargetDir(String(L"FlurryAds.framework",19));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<31>");
	if(t_dst->p_Exists()){
		DBG_BLOCK();
		t_dst->p_Remove2(true);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<32>");
	t_src->p_CopyTo3(t_dst,true,true,true);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<34>");
	p_ConvertToFrameworkDir(t_app);
}
void c_IosFlurryAds::p_Run(c_App* t_app){
	DBG_ENTER("IosFlurryAds.Run")
	c_IosFlurryAds *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<17>");
	c_Ios::m_AddFramework(String(L"SystemConfiguration.framework",29),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<19>");
	c_Ios::m_AddFrameworkFromPath(String(L"FlurryAds.framework",19),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<20>");
	p_CopyFramework(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<22>");
	t_app->p_LogInfo(String(L"!! You need to add IosFlurry too !!",35));
}
c_IosFlurryAds* c_IosFlurryAds::m_new(){
	DBG_ENTER("IosFlurryAds.new")
	c_IosFlurryAds *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosflurryads.monkey<13>");
	return this;
}
void c_IosFlurryAds::mark(){
	Object::mark();
}
String c_IosFlurryAds::debug(){
	String t="(IosFlurryAds)\n";
	return t;
}
c_IosFramework::c_IosFramework(){
}
void c_IosFramework::p_Run(c_App* t_app){
	DBG_ENTER("IosFramework.Run")
	c_IosFramework *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosframework.monkey<13>");
	if(t_app->p_GetAdditionArguments().Length()!=2){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosframework.monkey<14>");
		t_app->p_LogInfo(String(L"First argument must be the name of the framework",48));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosframework.monkey<15>");
		t_app->p_LogInfo(String(L"Second argument indicates (0/1) if it's optional",48));
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosframework.monkey<16>");
		t_app->p_LogError(String(L"Invalid number of arguments given",33));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosframework.monkey<19>");
	String t_name=t_app->p_GetAdditionArguments().At(0);
	DBG_LOCAL(t_name,"name")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosframework.monkey<20>");
	bool t_optional=t_app->p_GetAdditionArguments().At(1)==String(L"1",1);
	DBG_LOCAL(t_optional,"optional")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosframework.monkey<21>");
	c_Ios::m_AddFramework(t_name,t_optional);
}
c_IosFramework* c_IosFramework::m_new(){
	DBG_ENTER("IosFramework.new")
	c_IosFramework *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosframework.monkey<11>");
	return this;
}
void c_IosFramework::mark(){
	Object::mark();
}
String c_IosFramework::debug(){
	String t="(IosFramework)\n";
	return t;
}
c_IosHideStatusBar::c_IosHideStatusBar(){
	m_app=0;
}
void c_IosHideStatusBar::p_RemoveOldSettings(){
	DBG_ENTER("IosHideStatusBar.RemoveOldSettings")
	c_IosHideStatusBar *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<28>");
	c_File* t_file=c_Ios::m_GetPlist();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<29>");
	Array<int > t_lines=t_file->p_FindLines(String(L"<key>UIStatusBarHidden",22));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<31>");
	for(int t_i=t_lines.Length()-1;t_i>=0;t_i=t_i+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<32>");
		t_file->p_RemoveLine(t_lines.At(t_i)+1);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<33>");
		t_file->p_RemoveLine(t_lines.At(t_i));
	}
}
bool c_IosHideStatusBar::p_IsEnabled(){
	DBG_ENTER("IosHideStatusBar.IsEnabled")
	c_IosHideStatusBar *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<48>");
	if(m_app->p_GetAdditionArguments().Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<49>");
		m_app->p_LogError(String(L"Mode argument (valid values 0/1) missing",40));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<52>");
	String t_value=m_app->p_GetAdditionArguments().At(0);
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<53>");
	if(t_value!=String(L"0",1) && t_value!=String(L"1",1)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<54>");
		m_app->p_LogError(String(L"Invalid mode given. Valid modes are 0 and 1.",44));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<57>");
	bool t_=t_value==String(L"1",1);
	return t_;
}
void c_IosHideStatusBar::p_AddSettings2(){
	DBG_ENTER("IosHideStatusBar.AddSettings")
	c_IosHideStatusBar *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<38>");
	c_File* t_file=c_Ios::m_GetPlist();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<44>");
	t_file->p_InsertBefore(String(L"</dict>",7),String(L"\t<key>UIStatusBarHidden</key>\n\t<true/>\n\t<key>UIStatusBarHidden~ipad</key>\n\t<true/>",82));
}
void c_IosHideStatusBar::p_Run(c_App* t_app){
	DBG_ENTER("IosHideStatusBar.Run")
	c_IosHideStatusBar *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<19>");
	gc_assign(this->m_app,t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<21>");
	p_RemoveOldSettings();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<22>");
	if(p_IsEnabled()){
		DBG_BLOCK();
		p_AddSettings2();
	}
}
c_IosHideStatusBar* c_IosHideStatusBar::m_new(){
	DBG_ENTER("IosHideStatusBar.new")
	c_IosHideStatusBar *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioshidestatusbar.monkey<11>");
	return this;
}
void c_IosHideStatusBar::mark(){
	Object::mark();
	gc_mark_q(m_app);
}
String c_IosHideStatusBar::debug(){
	String t="(IosHideStatusBar)\n";
	t+=dbg_decl("app",&m_app);
	return t;
}
c_IosIcons::c_IosIcons(){
	m_app=0;
}
bool c_IosIcons::p_IsPrerendered(){
	DBG_ENTER("IosIcons.IsPrerendered")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<192>");
	if(m_app->p_GetAdditionArguments().Length()<1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<193>");
		m_app->p_LogError(String(L"First argument missing - prerendered or not?",44));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<196>");
	String t_value=m_app->p_GetAdditionArguments().At(0);
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<197>");
	if(t_value!=String(L"0",1) && t_value!=String(L"1",1)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<198>");
		m_app->p_LogError(String(L"Invalid prerendered value given. Valid modes are 0 and 1.",57));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<201>");
	bool t_=t_value==String(L"1",1);
	return t_;
}
Array<c_File* > c_IosIcons::p_GetNewIcons(){
	DBG_ENTER("IosIcons.GetNewIcons")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<205>");
	if(m_app->p_GetAdditionArguments().Length()<2){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<206>");
		m_app->p_LogError(String(L"No icon arguments found",23));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<209>");
	Array<c_File* > t_result=Array<c_File* >(m_app->p_GetAdditionArguments().Length()-1);
	DBG_LOCAL(t_result,"result")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<210>");
	for(int t_i=1;t_i<m_app->p_GetAdditionArguments().Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<211>");
		gc_assign(t_result.At(t_i-1),(new c_File)->m_new(m_app->p_GetAdditionArguments().At(t_i)));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<213>");
	return t_result;
}
void c_IosIcons::p_CheckNewIconsExists(Array<c_File* > t_icons){
	DBG_ENTER("IosIcons.CheckNewIconsExists")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_icons,"icons")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<44>");
	for(int t_i=0;t_i<t_icons.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<45>");
		if(t_icons.At(t_i)->p_Exists()){
			DBG_BLOCK();
			continue;
		}
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<46>");
		m_app->p_LogError(String(L"Invalid file given: ",20)+t_icons.At(t_i)->p_GetPath());
	}
}
void c_IosIcons::p_RemovePrerenderedFlag(){
	DBG_ENTER("IosIcons.RemovePrerenderedFlag")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<112>");
	c_File* t_file=c_Ios::m_GetPlist();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<113>");
	Array<int > t_lines=t_file->p_FindLines(String(L"<key>UIPrerenderedIcon</key>",28));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<115>");
	for(int t_i=t_lines.Length()-1;t_i>=0;t_i=t_i+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<116>");
		t_file->p_RemoveLine(t_lines.At(t_i)+1);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<117>");
		t_file->p_RemoveLine(t_lines.At(t_i));
	}
}
Array<String > c_IosIcons::p_RemoveKeyWithValues(String t_key){
	DBG_ENTER("IosIcons.RemoveKeyWithValues")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<147>");
	c_File* t_file=c_Ios::m_GetPlist();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<148>");
	Array<int > t_lines=t_file->p_FindLines(String(L"<key>",5)+t_key+String(L"</key>",6));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<149>");
	if(t_lines.Length()==0){
		DBG_BLOCK();
		return Array<String >();
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<152>");
	int t_startLine=t_lines.At(0);
	DBG_LOCAL(t_startLine,"startLine")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<153>");
	int t_rows=0;
	DBG_LOCAL(t_rows,"rows")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<155>");
	do{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<156>");
		t_rows+=1;
	}while(!(t_file->p_GetLine(t_startLine+t_rows).Trim().StartsWith(String(L"</",2))));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<159>");
	if(t_rows<=0){
		DBG_BLOCK();
		return Array<String >();
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<162>");
	Array<String > t_removed=Array<String >(t_rows+1);
	DBG_LOCAL(t_removed,"removed")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<163>");
	for(int t_i=t_rows;t_i>=0;t_i=t_i+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<164>");
		t_removed.At(t_i)=t_file->p_GetLine(t_startLine+t_i);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<165>");
		t_file->p_RemoveLine(t_startLine+t_i);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<168>");
	return t_removed;
}
String c_IosIcons::p_ExtractFileName(String t_row){
	DBG_ENTER("IosIcons.ExtractFileName")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_row,"row")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<172>");
	String t_=t_row.Replace(String(L"<string>",8),String()).Replace(String(L"</string>",9),String());
	return t_;
}
void c_IosIcons::p_RemoveFileDefinition(String t_filename){
	DBG_ENTER("IosIcons.RemoveFileDefinition")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_filename,"filename")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<176>");
	if(t_filename==String()){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<178>");
	c_File* t_file=c_Ios::m_GetProject();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<179>");
	Array<int > t_lines=t_file->p_FindLines(String(L"/* ",3)+t_filename);
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<181>");
	for(int t_i=t_lines.Length()-1;t_i>=0;t_i=t_i+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<182>");
		t_file->p_RemoveLine(t_lines.At(t_i));
	}
}
void c_IosIcons::p_RemoveFilePhysical(String t_filename){
	DBG_ENTER("IosIcons.RemoveFilePhysical")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_filename,"filename")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<187>");
	c_File* t_file=m_app->p_TargetFile(t_filename);
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<188>");
	if(t_file->p_Exists()){
		DBG_BLOCK();
		t_file->p_Remove();
	}
}
void c_IosIcons::p_ParseRowsAndRemoveFiles(Array<String > t_removed){
	DBG_ENTER("IosIcons.ParseRowsAndRemoveFiles")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_removed,"removed")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<135>");
	for(int t_i=0;t_i<t_removed.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<136>");
		String t_row=t_removed.At(t_i).Trim();
		DBG_LOCAL(t_row,"row")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<137>");
		if(!t_row.StartsWith(String(L"<string>",8))){
			DBG_BLOCK();
			continue;
		}
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<139>");
		String t_filename=p_ExtractFileName(t_row);
		DBG_LOCAL(t_filename,"filename")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<140>");
		p_RemoveFileDefinition(t_filename);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<141>");
		p_RemoveFilePhysical(t_filename);
	}
}
void c_IosIcons::p_RemoveIcons(){
	DBG_ENTER("IosIcons.RemoveIcons")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<122>");
	Array<String > t_removedRows=p_RemoveKeyWithValues(String(L"CFBundleIconFiles",17));
	DBG_LOCAL(t_removedRows,"removedRows")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<123>");
	p_ParseRowsAndRemoveFiles(t_removedRows);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<127>");
	t_removedRows=p_RemoveKeyWithValues(String(L"CFBundleIconFiles",17));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<128>");
	p_ParseRowsAndRemoveFiles(t_removedRows);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<130>");
	p_RemoveKeyWithValues(String(L"CFBundlePrimaryIcon",19));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<131>");
	p_RemoveKeyWithValues(String(L"CFBundleIcons",13));
}
void c_IosIcons::p_AddPrerenderedFlag(bool t_enabled){
	DBG_ENTER("IosIcons.AddPrerenderedFlag")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_enabled,"enabled")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<102>");
	if(!t_enabled){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<104>");
	c_File* t_file=c_Ios::m_GetPlist();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<108>");
	t_file->p_InsertBefore(String(L"</dict>",7),String(L"\t<key>UIPrerenderedIcon</key>\n\t<true/>",38));
}
void c_IosIcons::p_AddIconsPlist(Array<c_File* > t_icons,bool t_isPrerendered){
	DBG_ENTER("IosIcons.AddIconsPlist")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_icons,"icons")
	DBG_LOCAL(t_isPrerendered,"isPrerendered")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<69>");
	if(t_icons.Length()==0){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<71>");
	String t_files=String();
	DBG_LOCAL(t_files,"files")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<72>");
	for(int t_i=0;t_i<t_icons.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<73>");
		t_files=t_files+(String(L"\t\t\t\t<string>",12)+t_icons.At(t_i)->p_GetBasename()+String(L"</string>\n",10));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<76>");
	String t_prerendered=String();
	DBG_LOCAL(t_prerendered,"prerendered")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<77>");
	if(t_isPrerendered){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<78>");
		t_prerendered=String(L"\t\t\t<key>UIPrerenderedIcon</key>\n\t\t\t<true/>\n",43);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<81>");
	c_File* t_file=c_Ios::m_GetPlist();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<98>");
	t_file->p_InsertBefore(String(L"</dict>",7),String(L"\t<key>CFBundleIconFiles</key>\n\t<array>\n",39)+t_files+String(L"\t</array>\n",10)+String(L"\t<key>CFBundleIcons</key>\n",26)+String(L"\t<dict>\n",8)+String(L"\t\t<key>CFBundlePrimaryIcon</key>\n",33)+String(L"\t\t<dict>\n",9)+String(L"\t\t\t<key>CFBundleIconFiles</key>\n",32)+String(L"\t\t\t<array>\n",11)+t_files+String(L"\t\t\t</array>\n",12)+t_prerendered+String(L"\t\t</dict>\n",10)+String(L"\t</dict>",8));
}
void c_IosIcons::p_AddIconsDefinitions(Array<c_File* > t_icons){
	DBG_ENTER("IosIcons.AddIconsDefinitions")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_icons,"icons")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<56>");
	for(int t_i=0;t_i<t_icons.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<57>");
		String t_filename=t_icons.At(t_i)->p_GetBasename();
		DBG_LOCAL(t_filename,"filename")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<58>");
		String t_firstId=c_Ios::m_GenerateUniqueId();
		DBG_LOCAL(t_firstId,"firstId")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<59>");
		String t_secondId=c_Ios::m_GenerateUniqueId();
		DBG_LOCAL(t_secondId,"secondId")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<61>");
		c_Ios::m_AddIconPBXBuildFile(t_filename,t_firstId,t_secondId);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<62>");
		c_Ios::m_AddIconPBXFileReference(t_filename,t_secondId);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<63>");
		c_Ios::m_AddIconPBXGroup(t_filename,t_secondId);
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<64>");
		c_Ios::m_AddIconPBXResourcesBuildPhase(t_filename,t_firstId);
	}
}
void c_IosIcons::p_AddIcons(Array<c_File* > t_icons,bool t_isPrerendered){
	DBG_ENTER("IosIcons.AddIcons")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_icons,"icons")
	DBG_LOCAL(t_isPrerendered,"isPrerendered")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<51>");
	p_AddIconsPlist(t_icons,t_isPrerendered);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<52>");
	p_AddIconsDefinitions(t_icons);
}
void c_IosIcons::p_CopyIconFiles(Array<c_File* > t_icons){
	DBG_ENTER("IosIcons.CopyIconFiles")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_icons,"icons")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<37>");
	String t_dstDir=m_app->p_TargetDir(String())->p_GetPath();
	DBG_LOCAL(t_dstDir,"dstDir")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<38>");
	for(int t_i=0;t_i<t_icons.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<39>");
		t_icons.At(t_i)->p_CopyTo(t_dstDir+t_icons.At(t_i)->p_GetBasename());
	}
}
void c_IosIcons::p_Run(c_App* t_app){
	DBG_ENTER("IosIcons.Run")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<20>");
	gc_assign(this->m_app,t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<22>");
	bool t_prerendered=p_IsPrerendered();
	DBG_LOCAL(t_prerendered,"prerendered")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<23>");
	Array<c_File* > t_icons=p_GetNewIcons();
	DBG_LOCAL(t_icons,"icons")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<24>");
	p_CheckNewIconsExists(t_icons);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<26>");
	p_RemovePrerenderedFlag();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<27>");
	p_RemoveIcons();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<29>");
	p_AddPrerenderedFlag(t_prerendered);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<30>");
	p_AddIcons(t_icons,t_prerendered);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<31>");
	p_CopyIconFiles(t_icons);
}
c_IosIcons* c_IosIcons::m_new(){
	DBG_ENTER("IosIcons.new")
	c_IosIcons *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosicons.monkey<12>");
	return this;
}
void c_IosIcons::mark(){
	Object::mark();
	gc_mark_q(m_app);
}
String c_IosIcons::debug(){
	String t="(IosIcons)\n";
	t+=dbg_decl("app",&m_app);
	return t;
}
c_IosInterfaceOrientation::c_IosInterfaceOrientation(){
	m_app=0;
}
void c_IosInterfaceOrientation::p_RemoveKeyWithValues2(int t_startLine){
	DBG_ENTER("IosInterfaceOrientation.RemoveKeyWithValues")
	c_IosInterfaceOrientation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_startLine,"startLine")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<83>");
	c_File* t_file=c_Ios::m_GetPlist();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<84>");
	int t_rows=0;
	DBG_LOCAL(t_rows,"rows")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<86>");
	do{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<87>");
		t_rows+=1;
	}while(!(t_file->p_GetLine(t_startLine+t_rows).Trim().StartsWith(String(L"</",2))));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<90>");
	if(t_rows<=0){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<92>");
	for(int t_i=t_rows;t_i>=0;t_i=t_i+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<93>");
		t_file->p_RemoveLine(t_startLine+t_i);
	}
}
void c_IosInterfaceOrientation::p_RemoveOldSettings(){
	DBG_ENTER("IosInterfaceOrientation.RemoveOldSettings")
	c_IosInterfaceOrientation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<74>");
	c_File* t_file=c_Ios::m_GetPlist();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<75>");
	Array<int > t_lines=t_file->p_FindLines(String(L"<key>UISupportedInterfaceOrientations",37));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<77>");
	for(int t_i=t_lines.Length()-1;t_i>=0;t_i=t_i+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<78>");
		p_RemoveKeyWithValues2(t_lines.At(t_i));
	}
}
void c_IosInterfaceOrientation::p_AddOrientationBothPlist(){
	DBG_ENTER("IosInterfaceOrientation.AddOrientationBothPlist")
	c_IosInterfaceOrientation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<98>");
	c_File* t_file=c_Ios::m_GetPlist();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<114>");
	t_file->p_InsertBefore(String(L"</dict>",7),String(L"\t<key>UISupportedInterfaceOrientations</key>\n\t<array>\n\t\t<string>UIInterfaceOrientationPortrait</string>\n\t\t<string>UIInterfaceOrientationPortraitUpsideDown</string>\n\t\t<string>UIInterfaceOrientationLandscapeLeft</string>\n\t\t<string>UIInterfaceOrientationLandscapeRight</string>\n\t</array>\n\t<key>UISupportedInterfaceOrientations~ipad</key>\n\t<array>\n\t\t<string>UIInterfaceOrientationPortrait</string>\n\t\t<string>UIInterfaceOrientationPortraitUpsideDown</string>\n\t\t<string>UIInterfaceOrientationLandscapeLeft</string>\n\t\t<string>UIInterfaceOrientationLandscapeRight</string>\n\t</array>",574));
}
String c_IosInterfaceOrientation::p_GetOrientation(){
	DBG_ENTER("IosInterfaceOrientation.GetOrientation")
	c_IosInterfaceOrientation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<66>");
	if(m_app->p_GetAdditionArguments().Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<67>");
		m_app->p_LogError(String(L"Orientation argument missing",28));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<70>");
	String t_=m_app->p_GetAdditionArguments().At(0).ToUpper();
	return t_;
}
String c_IosInterfaceOrientation::p_AddOrientationBoth(){
	DBG_ENTER("IosInterfaceOrientation.AddOrientationBoth")
	c_IosInterfaceOrientation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<54>");
	return String(L"NSUInteger mask = UIInterfaceOrientationMaskLandscapeLeft|UIInterfaceOrientationMaskLandscapeRight|UIInterfaceOrientationMaskPortrait|UIInterfaceOrientationMaskPortraitUpsideDown;",179);
}
String c_IosInterfaceOrientation::p_AddOrientationLandscape(){
	DBG_ENTER("IosInterfaceOrientation.AddOrientationLandscape")
	c_IosInterfaceOrientation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<58>");
	return String(L"NSUInteger mask = UIInterfaceOrientationMaskLandscapeLeft|UIInterfaceOrientationMaskLandscapeRight;",99);
}
String c_IosInterfaceOrientation::p_AddOrientationPortrait(){
	DBG_ENTER("IosInterfaceOrientation.AddOrientationPortrait")
	c_IosInterfaceOrientation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<62>");
	return String(L"NSUInteger mask = UIInterfaceOrientationMaskPortrait|UIInterfaceOrientationMaskPortraitUpsideDown;",98);
}
void c_IosInterfaceOrientation::p_Run(c_App* t_app){
	DBG_ENTER("IosInterfaceOrientation.Run")
	c_IosInterfaceOrientation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<23>");
	gc_assign(this->m_app,t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<25>");
	p_RemoveOldSettings();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<26>");
	p_AddOrientationBothPlist();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<28>");
	c_File* t_src=c_Ios::m_GetMainSource();
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<31>");
	Array<int > t_lines=t_src->p_FindLines(String(L"-(NSUInteger)supportedInterfaceOrientations",43));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<32>");
	for(int t_i=0;t_i<=12;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<33>");
		t_src->p_RemoveLine(t_lines.At(0)+1);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<36>");
	String t_code=String();
	DBG_LOCAL(t_code,"code")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<37>");
	String t_1=p_GetOrientation();
	DBG_LOCAL(t_1,"1")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<38>");
	if(t_1==String(L"BOTH",4)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<39>");
		t_code=p_AddOrientationBoth();
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<40>");
		if(t_1==String(L"LANDSCAPE",9)){
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<41>");
			t_code=p_AddOrientationLandscape();
		}else{
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<42>");
			if(t_1==String(L"PORTRAIT",8)){
				DBG_BLOCK();
				DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<43>");
				t_code=p_AddOrientationPortrait();
			}else{
				DBG_BLOCK();
				DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<45>");
				t_app->p_LogError(String(L"Invalid orientation given",25));
			}
		}
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<48>");
	t_src->p_InsertAfterLine(t_lines.At(0),String(L"\t",1)+t_code);
}
c_IosInterfaceOrientation* c_IosInterfaceOrientation::m_new(){
	DBG_ENTER("IosInterfaceOrientation.new")
	c_IosInterfaceOrientation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosinterfaceorientation.monkey<12>");
	return this;
}
void c_IosInterfaceOrientation::mark(){
	Object::mark();
	gc_mark_q(m_app);
}
String c_IosInterfaceOrientation::debug(){
	String t="(IosInterfaceOrientation)\n";
	t+=dbg_decl("app",&m_app);
	return t;
}
c_IosLaunchImage::c_IosLaunchImage(){
}
String c_IosLaunchImage::m_GetMode(c_App* t_app){
	DBG_ENTER("IosLaunchImage.GetMode")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<36>");
	if(t_app->p_GetAdditionArguments().Length()<1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<37>");
		t_app->p_LogError(String(L"Not enough arguments.",21));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<40>");
	String t_=t_app->p_GetAdditionArguments().At(0).ToUpper();
	return t_;
}
void c_IosLaunchImage::m_CopyImage(c_App* t_app,int t_idx,String t_name){
	DBG_ENTER("IosLaunchImage.CopyImage")
	DBG_LOCAL(t_app,"app")
	DBG_LOCAL(t_idx,"idx")
	DBG_LOCAL(t_name,"name")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<70>");
	Array<String > t_args=t_app->p_GetAdditionArguments();
	DBG_LOCAL(t_args,"args")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<71>");
	if(t_args.Length()<t_idx){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<72>");
		t_app->p_LogError(String(L"Argument ",9)+String(t_idx)+String(L" for file ",10)+t_name+String(L" missing",8));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<75>");
	String t_filename=t_app->p_GetAdditionArguments().At(t_idx-1);
	DBG_LOCAL(t_filename,"filename")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<76>");
	if(!t_filename.EndsWith(String(L".png",4))){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<77>");
		t_app->p_LogError(String(L"Image must be in PNG format",27));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<80>");
	String t_dstDir=t_app->p_TargetDir(String())->p_GetPath();
	DBG_LOCAL(t_dstDir,"dstDir")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<81>");
	c_File* t_file=(new c_File)->m_new(t_filename);
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<83>");
	if(!t_file->p_Exists()){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<84>");
		t_app->p_LogError(String(L"Invalid file given: ",20)+t_file->p_GetPath());
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<86>");
	t_file->p_CopyTo(t_dstDir+t_name);
}
void c_IosLaunchImage::m_AddImage(c_App* t_app,String t_filename){
	DBG_ENTER("IosLaunchImage.AddImage")
	DBG_LOCAL(t_app,"app")
	DBG_LOCAL(t_filename,"filename")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<90>");
	String t_firstId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_firstId,"firstId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<91>");
	String t_secondId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_secondId,"secondId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<93>");
	c_Ios::m_AddIconPBXBuildFile(t_filename,t_firstId,t_secondId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<94>");
	c_Ios::m_AddIconPBXFileReference(t_filename,t_secondId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<95>");
	c_Ios::m_AddIconPBXGroup(t_filename,t_secondId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<96>");
	c_Ios::m_AddIconPBXResourcesBuildPhase(t_filename,t_firstId);
}
void c_IosLaunchImage::m_ProcessIPhone(c_App* t_app){
	DBG_ENTER("IosLaunchImage.ProcessIPhone")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<44>");
	m_CopyImage(t_app,4,String(L"Default-568h@2x.png",19));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<45>");
	m_CopyImage(t_app,3,String(L"Default@2x.png",14));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<46>");
	m_CopyImage(t_app,2,String(L"Default.png",11));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<48>");
	m_AddImage(t_app,String(L"Default-568h@2x.png",19));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<49>");
	m_AddImage(t_app,String(L"Default@2x.png",14));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<50>");
	m_AddImage(t_app,String(L"Default.png",11));
}
void c_IosLaunchImage::m_ProcessIPadLandscape(c_App* t_app){
	DBG_ENTER("IosLaunchImage.ProcessIPadLandscape")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<54>");
	m_CopyImage(t_app,3,String(L"Default-Landscape@2x~ipad.png",29));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<55>");
	m_CopyImage(t_app,2,String(L"Default-Landscape~ipad.png",26));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<57>");
	m_AddImage(t_app,String(L"Default-Landscape@2x~ipad.png",29));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<58>");
	m_AddImage(t_app,String(L"Default-Landscape~ipad.png",26));
}
void c_IosLaunchImage::m_ProcessIPadPortrait(c_App* t_app){
	DBG_ENTER("IosLaunchImage.ProcessIPadPortrait")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<62>");
	m_CopyImage(t_app,3,String(L"Default-Portrait@2x~ipad.png",28));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<63>");
	m_CopyImage(t_app,2,String(L"Default-Portrait~ipad.png",25));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<65>");
	m_AddImage(t_app,String(L"Default-Portrait@2x~ipad.png",28));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<66>");
	m_AddImage(t_app,String(L"Default-Portrait~ipad.png",25));
}
void c_IosLaunchImage::p_Run(c_App* t_app){
	DBG_ENTER("IosLaunchImage.Run")
	c_IosLaunchImage *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<21>");
	String t_1=m_GetMode(t_app);
	DBG_LOCAL(t_1,"1")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<22>");
	if(t_1==String(L"IPHONE",6)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<23>");
		m_ProcessIPhone(t_app);
	}else{
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<24>");
		if(t_1==String(L"IPAD-LANDSCAPE",14)){
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<25>");
			m_ProcessIPadLandscape(t_app);
		}else{
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<26>");
			if(t_1==String(L"IPAD-PORTRAIT",13)){
				DBG_BLOCK();
				DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<27>");
				m_ProcessIPadPortrait(t_app);
			}else{
				DBG_BLOCK();
				DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<29>");
				t_app->p_LogError(String(L"Invalid mode given.",19));
			}
		}
	}
}
c_IosLaunchImage* c_IosLaunchImage::m_new(){
	DBG_ENTER("IosLaunchImage.new")
	c_IosLaunchImage *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/ioslaunchimage.monkey<13>");
	return this;
}
void c_IosLaunchImage::mark(){
	Object::mark();
}
String c_IosLaunchImage::debug(){
	String t="(IosLaunchImage)\n";
	return t;
}
c_IosPatchCodeSigningIdentity::c_IosPatchCodeSigningIdentity(){
	m_app=0;
}
void c_IosPatchCodeSigningIdentity::p_Run(c_App* t_app){
	DBG_ENTER("IosPatchCodeSigningIdentity.Run")
	c_IosPatchCodeSigningIdentity *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iospatchcodesigningidentity.monkey<29>");
	gc_assign(this->m_app,t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iospatchcodesigningidentity.monkey<31>");
	c_File* t_file=c_Ios::m_GetProject();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iospatchcodesigningidentity.monkey<33>");
	Array<int > t_line=t_file->p_FindLines(String(L"\"CODE_SIGN_IDENTITY[sdk=iphoneos*]\" = \"iPhone Developer\";",57));
	DBG_LOCAL(t_line,"line")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iospatchcodesigningidentity.monkey<35>");
	String t_newLineContent=t_file->p_GetLine(t_line.At(1)).Replace(String(L"iPhone Developer",16),String(L"iPhone Distribution",19));
	DBG_LOCAL(t_newLineContent,"newLineContent")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iospatchcodesigningidentity.monkey<36>");
	t_file->p_ReplaceLine(t_line.At(1),t_newLineContent);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iospatchcodesigningidentity.monkey<38>");
	Array<int > t_lines=t_file->p_FindLines(String(L"name = Release",14));
	DBG_LOCAL(t_lines,"lines")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iospatchcodesigningidentity.monkey<39>");
	int t_appendAfterLine=t_lines.At(0)-2;
	DBG_LOCAL(t_appendAfterLine,"appendAfterLine")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iospatchcodesigningidentity.monkey<40>");
	t_file->p_InsertAfterLine(t_appendAfterLine,t_newLineContent.Replace(String(L"[sdk=iphoneos*]",15),String()));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iospatchcodesigningidentity.monkey<42>");
	t_file->p_Save();
}
c_IosPatchCodeSigningIdentity* c_IosPatchCodeSigningIdentity::m_new(){
	DBG_ENTER("IosPatchCodeSigningIdentity.new")
	c_IosPatchCodeSigningIdentity *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iospatchcodesigningidentity.monkey<21>");
	return this;
}
void c_IosPatchCodeSigningIdentity::mark(){
	Object::mark();
	gc_mark_q(m_app);
}
String c_IosPatchCodeSigningIdentity::debug(){
	String t="(IosPatchCodeSigningIdentity)\n";
	t+=dbg_decl("app",&m_app);
	return t;
}
c_IosProductName::c_IosProductName(){
	m_app=0;
}
String c_IosProductName::p_GetNewName(){
	DBG_ENTER("IosProductName.GetNewName")
	c_IosProductName *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<38>");
	if(m_app->p_GetAdditionArguments().Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<39>");
		m_app->p_LogError(String(L"Product name argument missing",29));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<42>");
	String t_=m_app->p_GetAdditionArguments().At(0);
	return t_;
}
void c_IosProductName::p_Run(c_App* t_app){
	DBG_ENTER("IosProductName.Run")
	c_IosProductName *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<19>");
	gc_assign(this->m_app,t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<21>");
	String t_newName=p_GetNewName();
	DBG_LOCAL(t_newName,"newName")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<22>");
	String t_newNameTrimmed=t_newName.Replace(String(L" ",1),String()).Trim();
	DBG_LOCAL(t_newNameTrimmed,"newNameTrimmed")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<23>");
	String t_oldName=c_Ios::m_GetProjectSetting(String(L"PRODUCT_NAME",12));
	DBG_LOCAL(t_oldName,"oldName")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<25>");
	c_File* t_file=c_Ios::m_GetProject();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<28>");
	t_file->p_Replace(String(L"/* ",3)+t_oldName+String(L".app */",7),String(L"/* ",3)+t_newNameTrimmed+String(L".app */",7));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<31>");
	t_file->p_Replace(String(L"; path = ",9)+t_oldName+String(L".app;",5),String(L"; path = ",9)+t_newNameTrimmed+String(L".app;",5));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<32>");
	c_Ios::m_UpdateProjectSetting(String(L"PRODUCT_NAME",12),t_newName);
}
c_IosProductName* c_IosProductName::m_new(){
	DBG_ENTER("IosProductName.new")
	c_IosProductName *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosproductname.monkey<11>");
	return this;
}
void c_IosProductName::mark(){
	Object::mark();
	gc_mark_q(m_app);
}
String c_IosProductName::debug(){
	String t="(IosProductName)\n";
	t+=dbg_decl("app",&m_app);
	return t;
}
c_IosRevmob::c_IosRevmob(){
}
void c_IosRevmob::p_CopyFramework(c_App* t_app){
	DBG_ENTER("IosRevmob.CopyFramework")
	c_IosRevmob *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosrevmob.monkey<24>");
	c_Dir* t_src=t_app->p_SourceDir(String(L"RevMobAds.framework",19));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosrevmob.monkey<25>");
	c_Dir* t_dst=t_app->p_TargetDir(String(L"RevMobAds.framework",19));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosrevmob.monkey<26>");
	t_src->p_CopyTo3(t_dst,true,true,true);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosrevmob.monkey<31>");
	c_Dir* t_target=t_app->p_TargetDir(String(L"RevMobAds.framework/Versions",28));
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosrevmob.monkey<32>");
	if(t_target->p_Exists()){
		DBG_BLOCK();
		t_target->p_Remove2(true);
	}
}
void c_IosRevmob::p_Run(c_App* t_app){
	DBG_ENTER("IosRevmob.Run")
	c_IosRevmob *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosrevmob.monkey<15>");
	c_Ios::m_AddFrameworkFromPath(String(L"RevMobAds.framework",19),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosrevmob.monkey<16>");
	p_CopyFramework(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosrevmob.monkey<18>");
	t_app->p_LogInfo(String(L"RevMob: Monkey interface can be found here: http://goo.gl/yVTiV",63));
}
c_IosRevmob* c_IosRevmob::m_new(){
	DBG_ENTER("IosRevmob.new")
	c_IosRevmob *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosrevmob.monkey<13>");
	return this;
}
void c_IosRevmob::mark(){
	Object::mark();
}
String c_IosRevmob::debug(){
	String t="(IosRevmob)\n";
	return t;
}
c_IosVersion::c_IosVersion(){
}
String c_IosVersion::m_GetVersion(c_App* t_app){
	DBG_ENTER("IosVersion.GetVersion")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosversion.monkey<19>");
	if(t_app->p_GetAdditionArguments().Length()!=1){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosversion.monkey<20>");
		t_app->p_LogError(String(L"Version string argument missing",31));
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosversion.monkey<23>");
	String t_=t_app->p_GetAdditionArguments().At(0);
	return t_;
}
void c_IosVersion::p_Run(c_App* t_app){
	DBG_ENTER("IosVersion.Run")
	c_IosVersion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosversion.monkey<13>");
	c_Ios::m_UpdatePlistSetting(String(L"CFBundleVersion",15),m_GetVersion(t_app));
}
c_IosVersion* c_IosVersion::m_new(){
	DBG_ENTER("IosVersion.new")
	c_IosVersion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosversion.monkey<11>");
	return this;
}
void c_IosVersion::mark(){
	Object::mark();
}
String c_IosVersion::debug(){
	String t="(IosVersion)\n";
	return t;
}
c_IosVungle::c_IosVungle(){
}
void c_IosVungle::p_AddLibZ(){
	DBG_ENTER("IosVungle.AddLibZ")
	c_IosVungle *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<62>");
	String t_firstId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_firstId,"firstId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<63>");
	String t_secondId=c_Ios::m_GenerateUniqueId();
	DBG_LOCAL(t_secondId,"secondId")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<65>");
	String t_name=String(L"libz.dylib",10);
	DBG_LOCAL(t_name,"name")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<67>");
	c_Ios::m_AddPbxBuildFile(t_name,t_firstId,t_secondId,false,String());
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<76>");
	String t_patchStr=String(L"\t\t",2)+t_secondId+String(L" ",1)+String(L"/* ",3)+t_name+String(L" */ = ",6)+String(L"{isa = PBXFileReference; ",25)+String(L"lastKnownFileType = \"compiled.mach-o.dylib\"; ",45)+String(L"name = ",7)+t_name+String(L"; ",2)+String(L"path = usr/lib/libz.dylib; ",27)+String(L"sourceTree = SDKROOT; };",24);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<77>");
	c_Ios::m_AddPbxFileReference(t_name,t_patchStr);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<79>");
	c_Ios::m_AddPbxFrameworkBuildPhase(t_name,t_firstId);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<81>");
	c_Ios::m_AddPbxGroupChild(String(L"Frameworks",10),t_name,t_secondId);
}
void c_IosVungle::p_AddOrientationPortrait(){
	DBG_ENTER("IosVungle.AddOrientationPortrait")
	c_IosVungle *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<46>");
	c_File* t_file=c_Ios::m_GetPlist();
	DBG_LOCAL(t_file,"file")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<58>");
	t_file->p_InsertBefore(String(L"</dict>",7),String(L"\t<key>UISupportedInterfaceOrientations</key>\n\t<array>\n\t\t<string>UIInterfaceOrientationPortrait</string>\n\t\t<string>UIInterfaceOrientationPortraitUpsideDown</string>\n\t</array>\n\t<key>UISupportedInterfaceOrientations~ipad</key>\n\t<array>\n\t\t<string>UIInterfaceOrientationPortrait</string>\n\t\t<string>UIInterfaceOrientationPortraitUpsideDown</string>\n\t</array>",352));
}
void c_IosVungle::p_CopyFramework(c_App* t_app){
	DBG_ENTER("IosVungle.CopyFramework")
	c_IosVungle *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<23>");
	c_Dir* t_src=t_app->p_SourceDir(String(L"vunglepub.embeddedframework",27));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<24>");
	c_Dir* t_dst=t_app->p_TargetDir(String(L"vunglepub.embeddedframework",27));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<25>");
	t_src->p_CopyTo3(t_dst,true,true,true);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<30>");
	c_Dir* t_target=t_app->p_TargetDir(String(L"vunglepub.embeddedframework/vunglepub.framework/Versions",56));
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<31>");
	if(t_target->p_Exists()){
		DBG_BLOCK();
		t_target->p_Remove2(true);
	}
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<33>");
	p_AddLibZ();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<35>");
	c_Ios::m_AddFrameworkFromPath(String(L"vunglepub.embeddedframework",27),false);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<39>");
	c_Ios::m_GetProject()->p_InsertAfter(String(L"\t\t\t\tFRAMEWORK_SEARCH_PATHS = (",30),String(L"\t\t\t\t\t\"\\\"$(PROJECT_DIR)/vunglepub.embeddedframework\\\"\",",54));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<42>");
	p_AddOrientationPortrait();
}
void c_IosVungle::p_Run(c_App* t_app){
	DBG_ENTER("IosVungle.Run")
	c_IosVungle *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<15>");
	p_CopyFramework(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<17>");
	t_app->p_LogInfo(String(L"Vungle installed!",17));
}
c_IosVungle* c_IosVungle::m_new(){
	DBG_ENTER("IosVungle.new")
	c_IosVungle *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/iosvungle.monkey<13>");
	return this;
}
void c_IosVungle::mark(){
	Object::mark();
}
String c_IosVungle::debug(){
	String t="(IosVungle)\n";
	return t;
}
c_SamsungPayment::c_SamsungPayment(){
}
void c_SamsungPayment::p_CopyLibs(c_App* t_app){
	DBG_ENTER("SamsungPayment.CopyLibs")
	c_SamsungPayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<24>");
	c_Android::m_EnsureLibsFolder();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<26>");
	c_Dir* t_src=t_app->p_SourceDir(String(L"src",3));
	DBG_LOCAL(t_src,"src")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<27>");
	c_Dir* t_dst=t_app->p_TargetDir(String(L"src_AndroidBillingLibrary/src",29));
	DBG_LOCAL(t_dst,"dst")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<28>");
	t_src->p_CopyTo3(t_dst,true,true,true);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<30>");
	t_src=t_app->p_SourceDir(String(L"res",3));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<31>");
	t_dst=t_app->p_TargetDir(String(L"res",3));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<32>");
	t_src->p_CopyTo3(t_dst,true,true,false);
}
void c_SamsungPayment::p_PatchBuildXml(c_App* t_app){
	DBG_ENTER("SamsungPayment.PatchBuildXml")
	c_SamsungPayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<36>");
	String t_patchStr=String(L"<copy todir=\"src\"><fileset dir=\"src_AndroidBillingLibrary\"/></copy>",67);
	DBG_LOCAL(t_patchStr,"patchStr")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<37>");
	String t_match=String(L"</project>",10);
	DBG_LOCAL(t_match,"match")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<38>");
	c_File* t_target=t_app->p_TargetFile(String(L"build.xml",9));
	DBG_LOCAL(t_target,"target")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<40>");
	if(!t_target->p_Contains(t_patchStr)){
		DBG_BLOCK();
		DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<41>");
		if(!t_target->p_Contains(t_match)){
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<42>");
			t_app->p_LogWarning(String(L"Unable to add required copy instructions to build.xml",53));
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<43>");
			t_app->p_LogWarning(String(L"please add the following line to your build.xml:",48));
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<44>");
			t_app->p_LogWarning(String(L"    ",4)+t_patchStr);
		}else{
			DBG_BLOCK();
			DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<46>");
			t_target->p_InsertBefore(t_match,t_patchStr);
		}
	}
}
void c_SamsungPayment::p_Run(c_App* t_app){
	DBG_ENTER("SamsungPayment.Run")
	c_SamsungPayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_app,"app")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<15>");
	c_Android::m_AddPermission(String(L"android.permission.INTERNET",27));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<16>");
	c_Android::m_AddPermission(String(L"com.sec.android.iap.permission.BILLING",38));
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<17>");
	p_CopyLibs(t_app);
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<18>");
	p_PatchBuildXml(t_app);
}
c_SamsungPayment* c_SamsungPayment::m_new(){
	DBG_ENTER("SamsungPayment.new")
	c_SamsungPayment *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard/commands/samsungpayment.monkey<13>");
	return this;
}
void c_SamsungPayment::mark(){
	Object::mark();
}
String c_SamsungPayment::debug(){
	String t="(SamsungPayment)\n";
	return t;
}
c_ArrayObject::c_ArrayObject(){
	m_value=Array<String >();
}
c_ArrayObject* c_ArrayObject::m_new(Array<String > t_value){
	DBG_ENTER("ArrayObject.new")
	c_ArrayObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<120>");
	gc_assign(this->m_value,t_value);
	return this;
}
Array<String > c_ArrayObject::p_ToArray(){
	DBG_ENTER("ArrayObject.ToArray")
	c_ArrayObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<124>");
	return m_value;
}
c_ArrayObject* c_ArrayObject::m_new2(){
	DBG_ENTER("ArrayObject.new")
	c_ArrayObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/boxes.monkey<116>");
	return this;
}
void c_ArrayObject::mark(){
	Object::mark();
	gc_mark_q(m_value);
}
String c_ArrayObject::debug(){
	String t="(ArrayObject)\n";
	t+=dbg_decl("value",&m_value);
	return t;
}
c_ClassInfo::c_ClassInfo(){
	m__name=String();
	m__attrs=0;
	m__sclass=0;
	m__ifaces=Array<c_ClassInfo* >();
	m__rconsts=Array<c_ConstInfo* >();
	m__consts=Array<c_ConstInfo* >();
	m__rfields=Array<c_FieldInfo* >();
	m__fields=Array<c_FieldInfo* >();
	m__rglobals=Array<c_GlobalInfo* >();
	m__globals=Array<c_GlobalInfo* >();
	m__rmethods=Array<c_MethodInfo* >();
	m__methods=Array<c_MethodInfo* >();
	m__rfunctions=Array<c_FunctionInfo* >();
	m__functions=Array<c_FunctionInfo* >();
	m__ctors=Array<c_FunctionInfo* >();
}
c_ClassInfo* c_ClassInfo::m_new(String t_name,int t_attrs,c_ClassInfo* t_sclass,Array<c_ClassInfo* > t_ifaces){
	DBG_ENTER("ClassInfo.new")
	c_ClassInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_attrs,"attrs")
	DBG_LOCAL(t_sclass,"sclass")
	DBG_LOCAL(t_ifaces,"ifaces")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<215>");
	m__name=t_name;
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<216>");
	m__attrs=t_attrs;
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<217>");
	gc_assign(m__sclass,t_sclass);
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<218>");
	gc_assign(m__ifaces,t_ifaces);
	return this;
}
c_ClassInfo* c_ClassInfo::m_new2(){
	DBG_ENTER("ClassInfo.new")
	c_ClassInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<212>");
	return this;
}
int c_ClassInfo::p_Init(){
	DBG_ENTER("ClassInfo.Init")
	c_ClassInfo *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
String c_ClassInfo::p_Name(){
	DBG_ENTER("ClassInfo.Name")
	c_ClassInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<222>");
	return m__name;
}
Object* c_ClassInfo::p_NewInstance(){
	DBG_ENTER("ClassInfo.NewInstance")
	c_ClassInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<254>");
	bbError(String(L"Can't create instance of class",30));
	return 0;
}
int c_ClassInfo::p_InitR(){
	DBG_ENTER("ClassInfo.InitR")
	c_ClassInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<421>");
	if((m__sclass)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<422>");
		c_Stack* t_consts=(new c_Stack)->m_new2(m__sclass->m__rconsts);
		DBG_LOCAL(t_consts,"consts")
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<423>");
		Array<c_ConstInfo* > t_=m__consts;
		int t_2=0;
		while(t_2<t_.Length()){
			DBG_BLOCK();
			c_ConstInfo* t_t=t_.At(t_2);
			t_2=t_2+1;
			DBG_LOCAL(t_t,"t")
			DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<424>");
			t_consts->p_Push(t_t);
		}
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<426>");
		gc_assign(m__rconsts,t_consts->p_ToArray());
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<427>");
		c_Stack2* t_fields=(new c_Stack2)->m_new2(m__sclass->m__rfields);
		DBG_LOCAL(t_fields,"fields")
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<428>");
		Array<c_FieldInfo* > t_3=m__fields;
		int t_4=0;
		while(t_4<t_3.Length()){
			DBG_BLOCK();
			c_FieldInfo* t_t2=t_3.At(t_4);
			t_4=t_4+1;
			DBG_LOCAL(t_t2,"t")
			DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<429>");
			t_fields->p_Push4(t_t2);
		}
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<431>");
		gc_assign(m__rfields,t_fields->p_ToArray());
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<432>");
		c_Stack3* t_globals=(new c_Stack3)->m_new2(m__sclass->m__rglobals);
		DBG_LOCAL(t_globals,"globals")
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<433>");
		Array<c_GlobalInfo* > t_5=m__globals;
		int t_6=0;
		while(t_6<t_5.Length()){
			DBG_BLOCK();
			c_GlobalInfo* t_t3=t_5.At(t_6);
			t_6=t_6+1;
			DBG_LOCAL(t_t3,"t")
			DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<434>");
			t_globals->p_Push7(t_t3);
		}
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<436>");
		gc_assign(m__rglobals,t_globals->p_ToArray());
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<437>");
		c_Stack4* t_methods=(new c_Stack4)->m_new2(m__sclass->m__rmethods);
		DBG_LOCAL(t_methods,"methods")
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<438>");
		Array<c_MethodInfo* > t_7=m__methods;
		int t_8=0;
		while(t_8<t_7.Length()){
			DBG_BLOCK();
			c_MethodInfo* t_t4=t_7.At(t_8);
			t_8=t_8+1;
			DBG_LOCAL(t_t4,"t")
			DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<439>");
			t_methods->p_Push10(t_t4);
		}
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<441>");
		gc_assign(m__rmethods,t_methods->p_ToArray());
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<442>");
		c_Stack5* t_functions=(new c_Stack5)->m_new2(m__sclass->m__rfunctions);
		DBG_LOCAL(t_functions,"functions")
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<443>");
		Array<c_FunctionInfo* > t_9=m__functions;
		int t_10=0;
		while(t_10<t_9.Length()){
			DBG_BLOCK();
			c_FunctionInfo* t_t5=t_9.At(t_10);
			t_10=t_10+1;
			DBG_LOCAL(t_t5,"t")
			DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<444>");
			t_functions->p_Push13(t_t5);
		}
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<446>");
		gc_assign(m__rfunctions,t_functions->p_ToArray());
	}else{
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<448>");
		gc_assign(m__rconsts,m__consts);
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<449>");
		gc_assign(m__rfields,m__fields);
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<450>");
		gc_assign(m__rglobals,m__globals);
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<451>");
		gc_assign(m__rmethods,m__methods);
		DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<452>");
		gc_assign(m__rfunctions,m__functions);
	}
	return 0;
}
void c_ClassInfo::mark(){
	Object::mark();
	gc_mark_q(m__sclass);
	gc_mark_q(m__ifaces);
	gc_mark_q(m__rconsts);
	gc_mark_q(m__consts);
	gc_mark_q(m__rfields);
	gc_mark_q(m__fields);
	gc_mark_q(m__rglobals);
	gc_mark_q(m__globals);
	gc_mark_q(m__rmethods);
	gc_mark_q(m__methods);
	gc_mark_q(m__rfunctions);
	gc_mark_q(m__functions);
	gc_mark_q(m__ctors);
}
String c_ClassInfo::debug(){
	String t="(ClassInfo)\n";
	t+=dbg_decl("_name",&m__name);
	t+=dbg_decl("_attrs",&m__attrs);
	t+=dbg_decl("_sclass",&m__sclass);
	t+=dbg_decl("_ifaces",&m__ifaces);
	t+=dbg_decl("_consts",&m__consts);
	t+=dbg_decl("_fields",&m__fields);
	t+=dbg_decl("_globals",&m__globals);
	t+=dbg_decl("_methods",&m__methods);
	t+=dbg_decl("_functions",&m__functions);
	t+=dbg_decl("_ctors",&m__ctors);
	t+=dbg_decl("_rconsts",&m__rconsts);
	t+=dbg_decl("_rglobals",&m__rglobals);
	t+=dbg_decl("_rfields",&m__rfields);
	t+=dbg_decl("_rmethods",&m__rmethods);
	t+=dbg_decl("_rfunctions",&m__rfunctions);
	return t;
}
Array<c_ClassInfo* > bb_reflection__classes;
c_R45::c_R45(){
}
c_R45* c_R45::m_new(){
	c_ClassInfo::m_new(String(L"monkey.lang.Object",18),1,0,Array<c_ClassInfo* >());
	return this;
}
int c_R45::p_Init(){
	p_InitR();
	return 0;
}
void c_R45::mark(){
	c_ClassInfo::mark();
}
String c_R45::debug(){
	String t="(R45)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R46::c_R46(){
}
c_R46* c_R46::m_new(){
	c_ClassInfo::m_new(String(L"monkey.boxes.BoolObject",23),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >());
	gc_assign(bb_reflection__boolClass,(this));
	return this;
}
Object* c_R46::p_NewInstance(){
	return ((new c_BoolObject)->m_new2());
}
int c_R46::p_Init(){
	gc_assign(m__fields,Array<c_FieldInfo* >(1));
	gc_assign(m__fields.At(0),((new c_R47)->m_new()));
	gc_assign(m__methods,Array<c_MethodInfo* >(2));
	gc_assign(m__methods.At(0),((new c_R49)->m_new()));
	gc_assign(m__methods.At(1),((new c_R50)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(2));
	gc_assign(m__ctors.At(0),((new c_R48)->m_new()));
	gc_assign(m__ctors.At(1),((new c_R51)->m_new()));
	p_InitR();
	return 0;
}
void c_R46::mark(){
	c_ClassInfo::mark();
}
String c_R46::debug(){
	String t="(R46)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_ClassInfo* bb_reflection__boolClass;
c_R52::c_R52(){
}
c_R52* c_R52::m_new(){
	c_ClassInfo::m_new(String(L"monkey.boxes.IntObject",22),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >());
	gc_assign(bb_reflection__intClass,(this));
	return this;
}
Object* c_R52::p_NewInstance(){
	return ((new c_IntObject)->m_new3());
}
int c_R52::p_Init(){
	gc_assign(m__fields,Array<c_FieldInfo* >(1));
	gc_assign(m__fields.At(0),((new c_R53)->m_new()));
	gc_assign(m__methods,Array<c_MethodInfo* >(5));
	gc_assign(m__methods.At(0),((new c_R56)->m_new()));
	gc_assign(m__methods.At(1),((new c_R57)->m_new()));
	gc_assign(m__methods.At(2),((new c_R58)->m_new()));
	gc_assign(m__methods.At(3),((new c_R59)->m_new()));
	gc_assign(m__methods.At(4),((new c_R60)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(3));
	gc_assign(m__ctors.At(0),((new c_R54)->m_new()));
	gc_assign(m__ctors.At(1),((new c_R55)->m_new()));
	gc_assign(m__ctors.At(2),((new c_R61)->m_new()));
	p_InitR();
	return 0;
}
void c_R52::mark(){
	c_ClassInfo::mark();
}
String c_R52::debug(){
	String t="(R52)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_ClassInfo* bb_reflection__intClass;
c_R62::c_R62(){
}
c_R62* c_R62::m_new(){
	c_ClassInfo::m_new(String(L"monkey.boxes.FloatObject",24),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >());
	gc_assign(bb_reflection__floatClass,(this));
	return this;
}
Object* c_R62::p_NewInstance(){
	return ((new c_FloatObject)->m_new3());
}
int c_R62::p_Init(){
	gc_assign(m__fields,Array<c_FieldInfo* >(1));
	gc_assign(m__fields.At(0),((new c_R63)->m_new()));
	gc_assign(m__methods,Array<c_MethodInfo* >(5));
	gc_assign(m__methods.At(0),((new c_R66)->m_new()));
	gc_assign(m__methods.At(1),((new c_R67)->m_new()));
	gc_assign(m__methods.At(2),((new c_R68)->m_new()));
	gc_assign(m__methods.At(3),((new c_R69)->m_new()));
	gc_assign(m__methods.At(4),((new c_R70)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(3));
	gc_assign(m__ctors.At(0),((new c_R64)->m_new()));
	gc_assign(m__ctors.At(1),((new c_R65)->m_new()));
	gc_assign(m__ctors.At(2),((new c_R71)->m_new()));
	p_InitR();
	return 0;
}
void c_R62::mark(){
	c_ClassInfo::mark();
}
String c_R62::debug(){
	String t="(R62)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_ClassInfo* bb_reflection__floatClass;
c_R72::c_R72(){
}
c_R72* c_R72::m_new(){
	c_ClassInfo::m_new(String(L"monkey.boxes.StringObject",25),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >());
	gc_assign(bb_reflection__stringClass,(this));
	return this;
}
Object* c_R72::p_NewInstance(){
	return ((new c_StringObject)->m_new4());
}
int c_R72::p_Init(){
	gc_assign(m__fields,Array<c_FieldInfo* >(1));
	gc_assign(m__fields.At(0),((new c_R73)->m_new()));
	gc_assign(m__methods,Array<c_MethodInfo* >(3));
	gc_assign(m__methods.At(0),((new c_R77)->m_new()));
	gc_assign(m__methods.At(1),((new c_R78)->m_new()));
	gc_assign(m__methods.At(2),((new c_R79)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(4));
	gc_assign(m__ctors.At(0),((new c_R74)->m_new()));
	gc_assign(m__ctors.At(1),((new c_R75)->m_new()));
	gc_assign(m__ctors.At(2),((new c_R76)->m_new()));
	gc_assign(m__ctors.At(3),((new c_R80)->m_new()));
	p_InitR();
	return 0;
}
void c_R72::mark(){
	c_ClassInfo::mark();
}
String c_R72::debug(){
	String t="(R72)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_ClassInfo* bb_reflection__stringClass;
c_R81::c_R81(){
}
c_R81* c_R81::m_new(){
	c_ClassInfo::m_new(String(L"monkey.lang.Throwable",21),33,bb_reflection__classes.At(0),Array<c_ClassInfo* >());
	return this;
}
int c_R81::p_Init(){
	p_InitR();
	return 0;
}
void c_R81::mark(){
	c_ClassInfo::mark();
}
String c_R81::debug(){
	String t="(R81)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R82::c_R82(){
}
c_R82* c_R82::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.amazonads.AmazonAds",35),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R82::p_NewInstance(){
	return ((new c_AmazonAds)->m_new());
}
int c_R82::p_Init(){
	gc_assign(m__consts,Array<c_ConstInfo* >(1));
	gc_assign(m__consts.At(0),(new c_ConstInfo)->m_new(String(L"VERSION",7),0,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"5.1.14",6)))));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R83)->m_new()));
	p_InitR();
	return 0;
}
void c_R82::mark(){
	c_ClassInfo::mark();
}
String c_R82::debug(){
	String t="(R82)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_UnknownClass::c_UnknownClass(){
}
c_UnknownClass* c_UnknownClass::m_new(){
	DBG_ENTER("UnknownClass.new")
	c_UnknownClass *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<625>");
	c_ClassInfo::m_new(String(L"?",1),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_UnknownClass::mark(){
	c_ClassInfo::mark();
}
String c_UnknownClass::debug(){
	String t="(UnknownClass)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_ClassInfo* bb_reflection__unknownClass;
c_R84::c_R84(){
}
c_R84* c_R84::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.amazonpayment.AmazonPayment",43),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R84::p_NewInstance(){
	return ((new c_AmazonPayment)->m_new());
}
int c_R84::p_Init(){
	gc_assign(m__consts,Array<c_ConstInfo* >(1));
	gc_assign(m__consts.At(0),(new c_ConstInfo)->m_new(String(L"VERSION",7),0,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"1.0.3",5)))));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R85)->m_new()));
	p_InitR();
	return 0;
}
void c_R84::mark(){
	c_ClassInfo::mark();
}
String c_R84::debug(){
	String t="(R84)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R86::c_R86(){
}
c_R86* c_R86::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.androidantkey.AndroidAntKey",43),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R86::p_NewInstance(){
	return ((new c_AndroidAntKey)->m_new());
}
int c_R86::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R87)->m_new()));
	p_InitR();
	return 0;
}
void c_R86::mark(){
	c_ClassInfo::mark();
}
String c_R86::debug(){
	String t="(R86)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R88::c_R88(){
}
c_R88* c_R88::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.androidbass.AndroidBass",39),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R88::p_NewInstance(){
	return ((new c_AndroidBass)->m_new());
}
int c_R88::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R89)->m_new()));
	p_InitR();
	return 0;
}
void c_R88::mark(){
	c_ClassInfo::mark();
}
String c_R88::debug(){
	String t="(R88)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R90::c_R90(){
}
c_R90* c_R90::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.androidchartboost.AndroidChartboost",51),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R90::p_NewInstance(){
	return ((new c_AndroidChartboost)->m_new());
}
int c_R90::p_Init(){
	gc_assign(m__consts,Array<c_ConstInfo* >(1));
	gc_assign(m__consts.At(0),(new c_ConstInfo)->m_new(String(L"VERSION",7),0,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"3.1.5",5)))));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R91)->m_new()));
	p_InitR();
	return 0;
}
void c_R90::mark(){
	c_ClassInfo::mark();
}
String c_R90::debug(){
	String t="(R90)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R92::c_R92(){
}
c_R92* c_R92::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.androidicons.AndroidIcons",41),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R92::p_NewInstance(){
	return ((new c_AndroidIcons)->m_new());
}
int c_R92::p_Init(){
	gc_assign(m__fields,Array<c_FieldInfo* >(1));
	gc_assign(m__fields.At(0),((new c_R93)->m_new()));
	gc_assign(m__methods,Array<c_MethodInfo* >(6));
	gc_assign(m__methods.At(0),((new c_R94)->m_new()));
	gc_assign(m__methods.At(1),((new c_R95)->m_new()));
	gc_assign(m__methods.At(2),((new c_R96)->m_new()));
	gc_assign(m__methods.At(3),((new c_R97)->m_new()));
	gc_assign(m__methods.At(4),((new c_R98)->m_new()));
	gc_assign(m__methods.At(5),((new c_R99)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R100)->m_new()));
	p_InitR();
	return 0;
}
void c_R92::mark(){
	c_ClassInfo::mark();
}
String c_R92::debug(){
	String t="(R92)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R101::c_R101(){
}
c_R101* c_R101::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.androidlocalytics.AndroidLocalytics",51),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R101::p_NewInstance(){
	return ((new c_AndroidLocalytics)->m_new());
}
int c_R101::p_Init(){
	gc_assign(m__consts,Array<c_ConstInfo* >(1));
	gc_assign(m__consts.At(0),(new c_ConstInfo)->m_new(String(L"VERSION",7),0,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"2.4",3)))));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R102)->m_new()));
	p_InitR();
	return 0;
}
void c_R101::mark(){
	c_ClassInfo::mark();
}
String c_R101::debug(){
	String t="(R101)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R103::c_R103(){
}
c_R103* c_R103::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.androidrevmob.AndroidRevmob",43),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R103::p_NewInstance(){
	return ((new c_AndroidRevmob)->m_new());
}
int c_R103::p_Init(){
	gc_assign(m__consts,Array<c_ConstInfo* >(1));
	gc_assign(m__consts.At(0),(new c_ConstInfo)->m_new(String(L"VERSION",7),0,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"6.4.3",5)))));
	gc_assign(m__methods,Array<c_MethodInfo* >(1));
	gc_assign(m__methods.At(0),((new c_R104)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R105)->m_new()));
	p_InitR();
	return 0;
}
void c_R103::mark(){
	c_ClassInfo::mark();
}
String c_R103::debug(){
	String t="(R103)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R106::c_R106(){
}
c_R106* c_R106::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.androidversion.AndroidVersion",45),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R106::p_NewInstance(){
	return ((new c_AndroidVersion)->m_new());
}
int c_R106::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R107)->m_new()));
	p_InitR();
	return 0;
}
void c_R106::mark(){
	c_ClassInfo::mark();
}
String c_R106::debug(){
	String t="(R106)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R108::c_R108(){
}
c_R108* c_R108::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.androidvungle.AndroidVungle",43),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R108::p_NewInstance(){
	return ((new c_AndroidVungle)->m_new());
}
int c_R108::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R109)->m_new()));
	p_InitR();
	return 0;
}
void c_R108::mark(){
	c_ClassInfo::mark();
}
String c_R108::debug(){
	String t="(R108)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R110::c_R110(){
}
c_R110* c_R110::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.googlepayment.GooglePayment",43),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R110::p_NewInstance(){
	return ((new c_GooglePayment)->m_new());
}
int c_R110::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R111)->m_new()));
	p_InitR();
	return 0;
}
void c_R110::mark(){
	c_ClassInfo::mark();
}
String c_R110::debug(){
	String t="(R110)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R112::c_R112(){
}
c_R112* c_R112::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosaddlanguage.IosAddLanguage",45),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R112::p_NewInstance(){
	return ((new c_IosAddLanguage)->m_new());
}
int c_R112::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R113)->m_new()));
	p_InitR();
	return 0;
}
void c_R112::mark(){
	c_ClassInfo::mark();
}
String c_R112::debug(){
	String t="(R112)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R114::c_R114(){
}
c_R114* c_R114::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosappirater.IosAppirater",41),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R114::p_NewInstance(){
	return ((new c_IosAppirater)->m_new());
}
int c_R114::p_Init(){
	gc_assign(m__functions,Array<c_FunctionInfo* >(1));
	gc_assign(m__functions.At(0),((new c_R115)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R116)->m_new()));
	p_InitR();
	return 0;
}
void c_R114::mark(){
	c_ClassInfo::mark();
}
String c_R114::debug(){
	String t="(R114)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R117::c_R117(){
}
c_R117* c_R117::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosbundleid.IosBundleId",39),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R117::p_NewInstance(){
	return ((new c_IosBundleId)->m_new());
}
int c_R117::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R118)->m_new()));
	p_InitR();
	return 0;
}
void c_R117::mark(){
	c_ClassInfo::mark();
}
String c_R117::debug(){
	String t="(R117)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R119::c_R119(){
}
c_R119* c_R119::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.ioschartboost.IosChartboost",43),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R119::p_NewInstance(){
	return ((new c_IosChartboost)->m_new());
}
int c_R119::p_Init(){
	gc_assign(m__consts,Array<c_ConstInfo* >(1));
	gc_assign(m__consts.At(0),(new c_ConstInfo)->m_new(String(L"VERSION",7),0,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"3.2",3)))));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R120)->m_new()));
	p_InitR();
	return 0;
}
void c_R119::mark(){
	c_ClassInfo::mark();
}
String c_R119::debug(){
	String t="(R119)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R121::c_R121(){
}
c_R121* c_R121::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.ioscompresspngfiles.IosCompressPngFiles",55),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R121::p_NewInstance(){
	return ((new c_IosCompressPngFiles)->m_new());
}
int c_R121::p_Init(){
	gc_assign(m__methods,Array<c_MethodInfo* >(3));
	gc_assign(m__methods.At(0),((new c_R122)->m_new()));
	gc_assign(m__methods.At(1),((new c_R123)->m_new()));
	gc_assign(m__methods.At(2),((new c_R124)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R125)->m_new()));
	p_InitR();
	return 0;
}
void c_R121::mark(){
	c_ClassInfo::mark();
}
String c_R121::debug(){
	String t="(R121)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R126::c_R126(){
}
c_R126* c_R126::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosdeploymenttarget.IosDeploymentTarget",55),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R126::p_NewInstance(){
	return ((new c_IosDeploymentTarget)->m_new());
}
int c_R126::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R127)->m_new()));
	p_InitR();
	return 0;
}
void c_R126::mark(){
	c_ClassInfo::mark();
}
String c_R126::debug(){
	String t="(R126)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R128::c_R128(){
}
c_R128* c_R128::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosflurry.IosFlurry",35),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R128::p_NewInstance(){
	return ((new c_IosFlurry)->m_new());
}
int c_R128::p_Init(){
	gc_assign(m__consts,Array<c_ConstInfo* >(1));
	gc_assign(m__consts.At(0),(new c_ConstInfo)->m_new(String(L"VERSION",7),0,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"4.1.0",5)))));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R129)->m_new()));
	p_InitR();
	return 0;
}
void c_R128::mark(){
	c_ClassInfo::mark();
}
String c_R128::debug(){
	String t="(R128)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R130::c_R130(){
}
c_R130* c_R130::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosflurryads.IosFlurryAds",41),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R130::p_NewInstance(){
	return ((new c_IosFlurryAds)->m_new());
}
int c_R130::p_Init(){
	gc_assign(m__consts,Array<c_ConstInfo* >(1));
	gc_assign(m__consts.At(0),(new c_ConstInfo)->m_new(String(L"VERSION",7),0,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"4.1.0",5)))));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R131)->m_new()));
	p_InitR();
	return 0;
}
void c_R130::mark(){
	c_ClassInfo::mark();
}
String c_R130::debug(){
	String t="(R130)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R132::c_R132(){
}
c_R132* c_R132::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosframework.IosFramework",41),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R132::p_NewInstance(){
	return ((new c_IosFramework)->m_new());
}
int c_R132::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R133)->m_new()));
	p_InitR();
	return 0;
}
void c_R132::mark(){
	c_ClassInfo::mark();
}
String c_R132::debug(){
	String t="(R132)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R134::c_R134(){
}
c_R134* c_R134::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.ioshidestatusbar.IosHideStatusBar",49),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R134::p_NewInstance(){
	return ((new c_IosHideStatusBar)->m_new());
}
int c_R134::p_Init(){
	gc_assign(m__methods,Array<c_MethodInfo* >(3));
	gc_assign(m__methods.At(0),((new c_R135)->m_new()));
	gc_assign(m__methods.At(1),((new c_R136)->m_new()));
	gc_assign(m__methods.At(2),((new c_R137)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R138)->m_new()));
	p_InitR();
	return 0;
}
void c_R134::mark(){
	c_ClassInfo::mark();
}
String c_R134::debug(){
	String t="(R134)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R139::c_R139(){
}
c_R139* c_R139::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosicons.IosIcons",33),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R139::p_NewInstance(){
	return ((new c_IosIcons)->m_new());
}
int c_R139::p_Init(){
	gc_assign(m__methods,Array<c_MethodInfo* >(9));
	gc_assign(m__methods.At(0),((new c_R140)->m_new()));
	gc_assign(m__methods.At(1),((new c_R141)->m_new()));
	gc_assign(m__methods.At(2),((new c_R142)->m_new()));
	gc_assign(m__methods.At(3),((new c_R143)->m_new()));
	gc_assign(m__methods.At(4),((new c_R144)->m_new()));
	gc_assign(m__methods.At(5),((new c_R145)->m_new()));
	gc_assign(m__methods.At(6),((new c_R146)->m_new()));
	gc_assign(m__methods.At(7),((new c_R147)->m_new()));
	gc_assign(m__methods.At(8),((new c_R148)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R149)->m_new()));
	p_InitR();
	return 0;
}
void c_R139::mark(){
	c_ClassInfo::mark();
}
String c_R139::debug(){
	String t="(R139)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R150::c_R150(){
}
c_R150* c_R150::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosinterfaceorientation.IosInterfaceOrientation",63),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R150::p_NewInstance(){
	return ((new c_IosInterfaceOrientation)->m_new());
}
int c_R150::p_Init(){
	gc_assign(m__consts,Array<c_ConstInfo* >(3));
	gc_assign(m__consts.At(0),(new c_ConstInfo)->m_new(String(L"BOTH",4),2,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"BOTH",4)))));
	gc_assign(m__consts.At(1),(new c_ConstInfo)->m_new(String(L"LANDSCAPE",9),2,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"LANDSCAPE",9)))));
	gc_assign(m__consts.At(2),(new c_ConstInfo)->m_new(String(L"PORTRAIT",8),2,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"PORTRAIT",8)))));
	gc_assign(m__methods,Array<c_MethodInfo* >(7));
	gc_assign(m__methods.At(0),((new c_R151)->m_new()));
	gc_assign(m__methods.At(1),((new c_R152)->m_new()));
	gc_assign(m__methods.At(2),((new c_R153)->m_new()));
	gc_assign(m__methods.At(3),((new c_R154)->m_new()));
	gc_assign(m__methods.At(4),((new c_R155)->m_new()));
	gc_assign(m__methods.At(5),((new c_R156)->m_new()));
	gc_assign(m__methods.At(6),((new c_R157)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R158)->m_new()));
	p_InitR();
	return 0;
}
void c_R150::mark(){
	c_ClassInfo::mark();
}
String c_R150::debug(){
	String t="(R150)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R159::c_R159(){
}
c_R159* c_R159::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.ioslaunchimage.IosLaunchImage",45),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R159::p_NewInstance(){
	return ((new c_IosLaunchImage)->m_new());
}
int c_R159::p_Init(){
	gc_assign(m__consts,Array<c_ConstInfo* >(3));
	gc_assign(m__consts.At(0),(new c_ConstInfo)->m_new(String(L"IPAD_LANDSCAPE",14),2,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"IPAD-LANDSCAPE",14)))));
	gc_assign(m__consts.At(1),(new c_ConstInfo)->m_new(String(L"IPAD_PORTRAIT",13),2,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"IPAD-PORTRAIT",13)))));
	gc_assign(m__consts.At(2),(new c_ConstInfo)->m_new(String(L"IPHONE",6),2,bb_reflection__stringClass,((new c_StringObject)->m_new3(String(L"IPHONE",6)))));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R160)->m_new()));
	p_InitR();
	return 0;
}
void c_R159::mark(){
	c_ClassInfo::mark();
}
String c_R159::debug(){
	String t="(R159)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R161::c_R161(){
}
c_R161* c_R161::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iospatchcodesigningidentity.IosPatchCodeSigningIdentity",71),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R161::p_NewInstance(){
	return ((new c_IosPatchCodeSigningIdentity)->m_new());
}
int c_R161::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R162)->m_new()));
	p_InitR();
	return 0;
}
void c_R161::mark(){
	c_ClassInfo::mark();
}
String c_R161::debug(){
	String t="(R161)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R163::c_R163(){
}
c_R163* c_R163::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosproductname.IosProductName",45),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R163::p_NewInstance(){
	return ((new c_IosProductName)->m_new());
}
int c_R163::p_Init(){
	gc_assign(m__methods,Array<c_MethodInfo* >(1));
	gc_assign(m__methods.At(0),((new c_R164)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R165)->m_new()));
	p_InitR();
	return 0;
}
void c_R163::mark(){
	c_ClassInfo::mark();
}
String c_R163::debug(){
	String t="(R163)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R166::c_R166(){
}
c_R166* c_R166::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosrevmob.IosRevmob",35),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R166::p_NewInstance(){
	return ((new c_IosRevmob)->m_new());
}
int c_R166::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R167)->m_new()));
	p_InitR();
	return 0;
}
void c_R166::mark(){
	c_ClassInfo::mark();
}
String c_R166::debug(){
	String t="(R166)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R168::c_R168(){
}
c_R168* c_R168::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosversion.IosVersion",37),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R168::p_NewInstance(){
	return ((new c_IosVersion)->m_new());
}
int c_R168::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R169)->m_new()));
	p_InitR();
	return 0;
}
void c_R168::mark(){
	c_ClassInfo::mark();
}
String c_R168::debug(){
	String t="(R168)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R170::c_R170(){
}
c_R170* c_R170::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.iosvungle.IosVungle",35),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R170::p_NewInstance(){
	return ((new c_IosVungle)->m_new());
}
int c_R170::p_Init(){
	gc_assign(m__methods,Array<c_MethodInfo* >(2));
	gc_assign(m__methods.At(0),((new c_R171)->m_new()));
	gc_assign(m__methods.At(1),((new c_R172)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R173)->m_new()));
	p_InitR();
	return 0;
}
void c_R170::mark(){
	c_ClassInfo::mark();
}
String c_R170::debug(){
	String t="(R170)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R174::c_R174(){
}
c_R174* c_R174::m_new(){
	c_ClassInfo* t_[]={bb_reflection__unknownClass};
	c_ClassInfo::m_new(String(L"wizard.commands.samsungpayment.SamsungPayment",45),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
Object* c_R174::p_NewInstance(){
	return ((new c_SamsungPayment)->m_new());
}
int c_R174::p_Init(){
	gc_assign(m__ctors,Array<c_FunctionInfo* >(1));
	gc_assign(m__ctors.At(0),((new c_R175)->m_new()));
	p_InitR();
	return 0;
}
void c_R174::mark(){
	c_ClassInfo::mark();
}
String c_R174::debug(){
	String t="(R174)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_R176::c_R176(){
}
c_R176* c_R176::m_new(){
	c_ClassInfo::m_new(String(L"monkey.boxes.ArrayObject<String>",32),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >());
	return this;
}
Object* c_R176::p_NewInstance(){
	return ((new c_ArrayObject)->m_new2());
}
int c_R176::p_Init(){
	gc_assign(m__fields,Array<c_FieldInfo* >(1));
	gc_assign(m__fields.At(0),((new c_R177)->m_new()));
	gc_assign(m__methods,Array<c_MethodInfo* >(1));
	gc_assign(m__methods.At(0),((new c_R179)->m_new()));
	gc_assign(m__ctors,Array<c_FunctionInfo* >(2));
	gc_assign(m__ctors.At(0),((new c_R178)->m_new()));
	gc_assign(m__ctors.At(1),((new c_R180)->m_new()));
	p_InitR();
	return 0;
}
void c_R176::mark(){
	c_ClassInfo::mark();
}
String c_R176::debug(){
	String t="(R176)\n";
	t=c_ClassInfo::debug()+t;
	return t;
}
c_FunctionInfo::c_FunctionInfo(){
	m__name=String();
	m__attrs=0;
	m__retType=0;
	m__argTypes=Array<c_ClassInfo* >();
}
c_FunctionInfo* c_FunctionInfo::m_new(String t_name,int t_attrs,c_ClassInfo* t_retType,Array<c_ClassInfo* > t_argTypes){
	DBG_ENTER("FunctionInfo.new")
	c_FunctionInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_attrs,"attrs")
	DBG_LOCAL(t_retType,"retType")
	DBG_LOCAL(t_argTypes,"argTypes")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<179>");
	m__name=t_name;
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<180>");
	m__attrs=t_attrs;
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<181>");
	gc_assign(m__retType,t_retType);
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<182>");
	gc_assign(m__argTypes,t_argTypes);
	return this;
}
c_FunctionInfo* c_FunctionInfo::m_new2(){
	DBG_ENTER("FunctionInfo.new")
	c_FunctionInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<176>");
	return this;
}
void c_FunctionInfo::mark(){
	Object::mark();
	gc_mark_q(m__retType);
	gc_mark_q(m__argTypes);
}
String c_FunctionInfo::debug(){
	String t="(FunctionInfo)\n";
	t+=dbg_decl("_name",&m__name);
	t+=dbg_decl("_attrs",&m__attrs);
	t+=dbg_decl("_retType",&m__retType);
	t+=dbg_decl("_argTypes",&m__argTypes);
	return t;
}
Array<c_FunctionInfo* > bb_reflection__functions;
c_R33::c_R33(){
}
c_R33* c_R33::m_new(){
	c_ClassInfo* t_[]={bb_reflection__boolClass};
	c_FunctionInfo::m_new(String(L"monkey.boxes.BoxBool",20),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R33::mark(){
	c_FunctionInfo::mark();
}
String c_R33::debug(){
	String t="(R33)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R34::c_R34(){
}
c_R34* c_R34::m_new(){
	c_ClassInfo* t_[]={bb_reflection__intClass};
	c_FunctionInfo::m_new(String(L"monkey.boxes.BoxInt",19),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R34::mark(){
	c_FunctionInfo::mark();
}
String c_R34::debug(){
	String t="(R34)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R35::c_R35(){
}
c_R35* c_R35::m_new(){
	c_ClassInfo* t_[]={bb_reflection__floatClass};
	c_FunctionInfo::m_new(String(L"monkey.boxes.BoxFloat",21),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R35::mark(){
	c_FunctionInfo::mark();
}
String c_R35::debug(){
	String t="(R35)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R36::c_R36(){
}
c_R36* c_R36::m_new(){
	c_ClassInfo* t_[]={bb_reflection__stringClass};
	c_FunctionInfo::m_new(String(L"monkey.boxes.BoxString",22),0,bb_reflection__classes.At(0),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R36::mark(){
	c_FunctionInfo::mark();
}
String c_R36::debug(){
	String t="(R36)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R37::c_R37(){
}
c_R37* c_R37::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(0)};
	c_FunctionInfo::m_new(String(L"monkey.boxes.UnboxBool",22),0,bb_reflection__boolClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R37::mark(){
	c_FunctionInfo::mark();
}
String c_R37::debug(){
	String t="(R37)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R38::c_R38(){
}
c_R38* c_R38::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(0)};
	c_FunctionInfo::m_new(String(L"monkey.boxes.UnboxInt",21),0,bb_reflection__intClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R38::mark(){
	c_FunctionInfo::mark();
}
String c_R38::debug(){
	String t="(R38)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R39::c_R39(){
}
c_R39* c_R39::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(0)};
	c_FunctionInfo::m_new(String(L"monkey.boxes.UnboxFloat",23),0,bb_reflection__floatClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R39::mark(){
	c_FunctionInfo::mark();
}
String c_R39::debug(){
	String t="(R39)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R40::c_R40(){
}
c_R40* c_R40::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(0)};
	c_FunctionInfo::m_new(String(L"monkey.boxes.UnboxString",24),0,bb_reflection__stringClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R40::mark(){
	c_FunctionInfo::mark();
}
String c_R40::debug(){
	String t="(R40)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R41::c_R41(){
}
c_R41* c_R41::m_new(){
	c_ClassInfo* t_[]={bb_reflection__stringClass};
	c_FunctionInfo::m_new(String(L"monkey.lang.Print",17),1,bb_reflection__intClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R41::mark(){
	c_FunctionInfo::mark();
}
String c_R41::debug(){
	String t="(R41)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R42::c_R42(){
}
c_R42* c_R42::m_new(){
	c_ClassInfo* t_[]={bb_reflection__stringClass};
	c_FunctionInfo::m_new(String(L"monkey.lang.Error",17),1,bb_reflection__intClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R42::mark(){
	c_FunctionInfo::mark();
}
String c_R42::debug(){
	String t="(R42)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R43::c_R43(){
}
c_R43* c_R43::m_new(){
	c_ClassInfo* t_[]={bb_reflection__stringClass};
	c_FunctionInfo::m_new(String(L"monkey.lang.DebugLog",20),1,bb_reflection__intClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R43::mark(){
	c_FunctionInfo::mark();
}
String c_R43::debug(){
	String t="(R43)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R44::c_R44(){
}
c_R44* c_R44::m_new(){
	c_FunctionInfo::m_new(String(L"monkey.lang.DebugStop",21),1,bb_reflection__intClass,Array<c_ClassInfo* >());
	return this;
}
void c_R44::mark(){
	c_FunctionInfo::mark();
}
String c_R44::debug(){
	String t="(R44)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c__GetClass::c__GetClass(){
}
c__GetClass* c__GetClass::m_new(){
	DBG_ENTER("_GetClass.new")
	c__GetClass *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<608>");
	return this;
}
void c__GetClass::mark(){
	Object::mark();
}
String c__GetClass::debug(){
	String t="(_GetClass)\n";
	return t;
}
c___GetClass::c___GetClass(){
}
c___GetClass* c___GetClass::m_new(){
	DBG_ENTER("__GetClass.new")
	c___GetClass *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("$SOURCE<1597>");
	c__GetClass::m_new();
	return this;
}
void c___GetClass::mark(){
	c__GetClass::mark();
}
String c___GetClass::debug(){
	String t="(__GetClass)\n";
	t=c__GetClass::debug()+t;
	return t;
}
c__GetClass* bb_reflection__getClass;
int bb_reflection___init(){
	gc_assign(bb_reflection__classes,Array<c_ClassInfo* >(37));
	gc_assign(bb_reflection__classes.At(0),((new c_R45)->m_new()));
	gc_assign(bb_reflection__classes.At(1),((new c_R46)->m_new()));
	gc_assign(bb_reflection__classes.At(2),((new c_R52)->m_new()));
	gc_assign(bb_reflection__classes.At(3),((new c_R62)->m_new()));
	gc_assign(bb_reflection__classes.At(4),((new c_R72)->m_new()));
	gc_assign(bb_reflection__classes.At(5),((new c_R81)->m_new()));
	gc_assign(bb_reflection__classes.At(6),((new c_R82)->m_new()));
	gc_assign(bb_reflection__classes.At(7),((new c_R84)->m_new()));
	gc_assign(bb_reflection__classes.At(8),((new c_R86)->m_new()));
	gc_assign(bb_reflection__classes.At(9),((new c_R88)->m_new()));
	gc_assign(bb_reflection__classes.At(10),((new c_R90)->m_new()));
	gc_assign(bb_reflection__classes.At(11),((new c_R92)->m_new()));
	gc_assign(bb_reflection__classes.At(12),((new c_R101)->m_new()));
	gc_assign(bb_reflection__classes.At(13),((new c_R103)->m_new()));
	gc_assign(bb_reflection__classes.At(14),((new c_R106)->m_new()));
	gc_assign(bb_reflection__classes.At(15),((new c_R108)->m_new()));
	gc_assign(bb_reflection__classes.At(16),((new c_R110)->m_new()));
	gc_assign(bb_reflection__classes.At(17),((new c_R112)->m_new()));
	gc_assign(bb_reflection__classes.At(18),((new c_R114)->m_new()));
	gc_assign(bb_reflection__classes.At(19),((new c_R117)->m_new()));
	gc_assign(bb_reflection__classes.At(20),((new c_R119)->m_new()));
	gc_assign(bb_reflection__classes.At(21),((new c_R121)->m_new()));
	gc_assign(bb_reflection__classes.At(22),((new c_R126)->m_new()));
	gc_assign(bb_reflection__classes.At(23),((new c_R128)->m_new()));
	gc_assign(bb_reflection__classes.At(24),((new c_R130)->m_new()));
	gc_assign(bb_reflection__classes.At(25),((new c_R132)->m_new()));
	gc_assign(bb_reflection__classes.At(26),((new c_R134)->m_new()));
	gc_assign(bb_reflection__classes.At(27),((new c_R139)->m_new()));
	gc_assign(bb_reflection__classes.At(28),((new c_R150)->m_new()));
	gc_assign(bb_reflection__classes.At(29),((new c_R159)->m_new()));
	gc_assign(bb_reflection__classes.At(30),((new c_R161)->m_new()));
	gc_assign(bb_reflection__classes.At(31),((new c_R163)->m_new()));
	gc_assign(bb_reflection__classes.At(32),((new c_R166)->m_new()));
	gc_assign(bb_reflection__classes.At(33),((new c_R168)->m_new()));
	gc_assign(bb_reflection__classes.At(34),((new c_R170)->m_new()));
	gc_assign(bb_reflection__classes.At(35),((new c_R174)->m_new()));
	gc_assign(bb_reflection__classes.At(36),((new c_R176)->m_new()));
	bb_reflection__classes.At(0)->p_Init();
	bb_reflection__classes.At(1)->p_Init();
	bb_reflection__classes.At(2)->p_Init();
	bb_reflection__classes.At(3)->p_Init();
	bb_reflection__classes.At(4)->p_Init();
	bb_reflection__classes.At(5)->p_Init();
	bb_reflection__classes.At(6)->p_Init();
	bb_reflection__classes.At(7)->p_Init();
	bb_reflection__classes.At(8)->p_Init();
	bb_reflection__classes.At(9)->p_Init();
	bb_reflection__classes.At(10)->p_Init();
	bb_reflection__classes.At(11)->p_Init();
	bb_reflection__classes.At(12)->p_Init();
	bb_reflection__classes.At(13)->p_Init();
	bb_reflection__classes.At(14)->p_Init();
	bb_reflection__classes.At(15)->p_Init();
	bb_reflection__classes.At(16)->p_Init();
	bb_reflection__classes.At(17)->p_Init();
	bb_reflection__classes.At(18)->p_Init();
	bb_reflection__classes.At(19)->p_Init();
	bb_reflection__classes.At(20)->p_Init();
	bb_reflection__classes.At(21)->p_Init();
	bb_reflection__classes.At(22)->p_Init();
	bb_reflection__classes.At(23)->p_Init();
	bb_reflection__classes.At(24)->p_Init();
	bb_reflection__classes.At(25)->p_Init();
	bb_reflection__classes.At(26)->p_Init();
	bb_reflection__classes.At(27)->p_Init();
	bb_reflection__classes.At(28)->p_Init();
	bb_reflection__classes.At(29)->p_Init();
	bb_reflection__classes.At(30)->p_Init();
	bb_reflection__classes.At(31)->p_Init();
	bb_reflection__classes.At(32)->p_Init();
	bb_reflection__classes.At(33)->p_Init();
	bb_reflection__classes.At(34)->p_Init();
	bb_reflection__classes.At(35)->p_Init();
	bb_reflection__classes.At(36)->p_Init();
	gc_assign(bb_reflection__functions,Array<c_FunctionInfo* >(12));
	gc_assign(bb_reflection__functions.At(0),((new c_R33)->m_new()));
	gc_assign(bb_reflection__functions.At(1),((new c_R34)->m_new()));
	gc_assign(bb_reflection__functions.At(2),((new c_R35)->m_new()));
	gc_assign(bb_reflection__functions.At(3),((new c_R36)->m_new()));
	gc_assign(bb_reflection__functions.At(4),((new c_R37)->m_new()));
	gc_assign(bb_reflection__functions.At(5),((new c_R38)->m_new()));
	gc_assign(bb_reflection__functions.At(6),((new c_R39)->m_new()));
	gc_assign(bb_reflection__functions.At(7),((new c_R40)->m_new()));
	gc_assign(bb_reflection__functions.At(8),((new c_R41)->m_new()));
	gc_assign(bb_reflection__functions.At(9),((new c_R42)->m_new()));
	gc_assign(bb_reflection__functions.At(10),((new c_R43)->m_new()));
	gc_assign(bb_reflection__functions.At(11),((new c_R44)->m_new()));
	gc_assign(bb_reflection__getClass,((new c___GetClass)->m_new()));
	return 0;
}
int bb_reflection__init;
Array<c_ClassInfo* > bb_reflection_GetClasses(){
	DBG_ENTER("GetClasses")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<459>");
	return bb_reflection__classes;
}
c_Map2::c_Map2(){
	m_root=0;
}
c_Map2* c_Map2::m_new(){
	DBG_ENTER("Map.new")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<7>");
	return this;
}
int c_Map2::p_RotateLeft2(c_Node4* t_node){
	DBG_ENTER("Map.RotateLeft")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<251>");
	c_Node4* t_child=t_node->m_right;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<252>");
	gc_assign(t_node->m_right,t_child->m_left);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<253>");
	if((t_child->m_left)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<254>");
		gc_assign(t_child->m_left->m_parent,t_node);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<256>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<257>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<258>");
		if(t_node==t_node->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<259>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<261>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<264>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<266>");
	gc_assign(t_child->m_left,t_node);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<267>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map2::p_RotateRight2(c_Node4* t_node){
	DBG_ENTER("Map.RotateRight")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<271>");
	c_Node4* t_child=t_node->m_left;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<272>");
	gc_assign(t_node->m_left,t_child->m_right);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<273>");
	if((t_child->m_right)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<274>");
		gc_assign(t_child->m_right->m_parent,t_node);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<276>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<277>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<278>");
		if(t_node==t_node->m_parent->m_right){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<279>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<281>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<284>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<286>");
	gc_assign(t_child->m_right,t_node);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<287>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map2::p_InsertFixup2(c_Node4* t_node){
	DBG_ENTER("Map.InsertFixup")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<212>");
	while(((t_node->m_parent)!=0) && t_node->m_parent->m_color==-1 && ((t_node->m_parent->m_parent)!=0)){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<213>");
		if(t_node->m_parent==t_node->m_parent->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<214>");
			c_Node4* t_uncle=t_node->m_parent->m_parent->m_right;
			DBG_LOCAL(t_uncle,"uncle")
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<215>");
			if(((t_uncle)!=0) && t_uncle->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<216>");
				t_node->m_parent->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<217>");
				t_uncle->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<218>");
				t_uncle->m_parent->m_color=-1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<219>");
				t_node=t_uncle->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<221>");
				if(t_node==t_node->m_parent->m_right){
					DBG_BLOCK();
					DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<222>");
					t_node=t_node->m_parent;
					DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<223>");
					p_RotateLeft2(t_node);
				}
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<225>");
				t_node->m_parent->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<226>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<227>");
				p_RotateRight2(t_node->m_parent->m_parent);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<230>");
			c_Node4* t_uncle2=t_node->m_parent->m_parent->m_left;
			DBG_LOCAL(t_uncle2,"uncle")
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<231>");
			if(((t_uncle2)!=0) && t_uncle2->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<232>");
				t_node->m_parent->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<233>");
				t_uncle2->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<234>");
				t_uncle2->m_parent->m_color=-1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<235>");
				t_node=t_uncle2->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<237>");
				if(t_node==t_node->m_parent->m_left){
					DBG_BLOCK();
					DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<238>");
					t_node=t_node->m_parent;
					DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<239>");
					p_RotateRight2(t_node);
				}
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<241>");
				t_node->m_parent->m_color=1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<242>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<243>");
				p_RotateLeft2(t_node->m_parent->m_parent);
			}
		}
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<247>");
	m_root->m_color=1;
	return 0;
}
bool c_Map2::p_Add(String t_key,c_ClassInfo* t_value){
	DBG_ENTER("Map.Add")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<61>");
	c_Node4* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<62>");
	c_Node4* t_parent=0;
	int t_cmp=0;
	DBG_LOCAL(t_parent,"parent")
	DBG_LOCAL(t_cmp,"cmp")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<64>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<65>");
		t_parent=t_node;
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<66>");
		t_cmp=p_Compare4(t_key,t_node->m_key);
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<67>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<68>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<69>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<70>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<72>");
				return false;
			}
		}
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<76>");
	t_node=(new c_Node4)->m_new(t_key,t_value,-1,t_parent);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<78>");
	if((t_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<79>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<80>");
			gc_assign(t_parent->m_right,t_node);
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<82>");
			gc_assign(t_parent->m_left,t_node);
		}
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<84>");
		p_InsertFixup2(t_node);
	}else{
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<86>");
		gc_assign(m_root,t_node);
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<88>");
	return true;
}
c_MapKeys* c_Map2::p_Keys(){
	DBG_ENTER("Map.Keys")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<113>");
	c_MapKeys* t_=(new c_MapKeys)->m_new(this);
	return t_;
}
c_Node4* c_Map2::p_FirstNode(){
	DBG_ENTER("Map.FirstNode")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<125>");
	if(!((m_root)!=0)){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<127>");
	c_Node4* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<128>");
	while((t_node->m_left)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<129>");
		t_node=t_node->m_left;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<131>");
	return t_node;
}
c_Node4* c_Map2::p_FindNode(String t_key){
	DBG_ENTER("Map.FindNode")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<157>");
	c_Node4* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<159>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<160>");
		int t_cmp=p_Compare4(t_key,t_node->m_key);
		DBG_LOCAL(t_cmp,"cmp")
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<161>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<162>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<163>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<164>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<166>");
				return t_node;
			}
		}
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<169>");
	return t_node;
}
c_ClassInfo* c_Map2::p_Get2(String t_key){
	DBG_ENTER("Map.Get")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<101>");
	c_Node4* t_node=p_FindNode(t_key);
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<102>");
	if((t_node)!=0){
		DBG_BLOCK();
		return t_node->m_value;
	}
	return 0;
}
void c_Map2::mark(){
	Object::mark();
	gc_mark_q(m_root);
}
String c_Map2::debug(){
	String t="(Map)\n";
	t+=dbg_decl("root",&m_root);
	return t;
}
c_StringMap2::c_StringMap2(){
}
c_StringMap2* c_StringMap2::m_new(){
	DBG_ENTER("StringMap.new")
	c_StringMap2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<551>");
	c_Map2::m_new();
	return this;
}
int c_StringMap2::p_Compare4(String t_lhs,String t_rhs){
	DBG_ENTER("StringMap.Compare")
	c_StringMap2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<554>");
	int t_=t_lhs.Compare(t_rhs);
	return t_;
}
void c_StringMap2::mark(){
	c_Map2::mark();
}
String c_StringMap2::debug(){
	String t="(StringMap)\n";
	t=c_Map2::debug()+t;
	return t;
}
c_Node4::c_Node4(){
	m_key=String();
	m_right=0;
	m_left=0;
	m_value=0;
	m_color=0;
	m_parent=0;
}
c_Node4* c_Node4::m_new(String t_key,c_ClassInfo* t_value,int t_color,c_Node4* t_parent){
	DBG_ENTER("Node.new")
	c_Node4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_color,"color")
	DBG_LOCAL(t_parent,"parent")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<364>");
	this->m_key=t_key;
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<365>");
	gc_assign(this->m_value,t_value);
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<366>");
	this->m_color=t_color;
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<367>");
	gc_assign(this->m_parent,t_parent);
	return this;
}
c_Node4* c_Node4::m_new2(){
	DBG_ENTER("Node.new")
	c_Node4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<361>");
	return this;
}
c_Node4* c_Node4::p_NextNode(){
	DBG_ENTER("Node.NextNode")
	c_Node4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<385>");
	c_Node4* t_node=0;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<386>");
	if((m_right)!=0){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<387>");
		t_node=m_right;
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<388>");
		while((t_node->m_left)!=0){
			DBG_BLOCK();
			DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<389>");
			t_node=t_node->m_left;
		}
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<391>");
		return t_node;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<393>");
	t_node=this;
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<394>");
	c_Node4* t_parent=this->m_parent;
	DBG_LOCAL(t_parent,"parent")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<395>");
	while(((t_parent)!=0) && t_node==t_parent->m_right){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<396>");
		t_node=t_parent;
		DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<397>");
		t_parent=t_parent->m_parent;
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<399>");
	return t_parent;
}
void c_Node4::mark(){
	Object::mark();
	gc_mark_q(m_right);
	gc_mark_q(m_left);
	gc_mark_q(m_value);
	gc_mark_q(m_parent);
}
String c_Node4::debug(){
	String t="(Node)\n";
	t+=dbg_decl("key",&m_key);
	t+=dbg_decl("value",&m_value);
	t+=dbg_decl("color",&m_color);
	t+=dbg_decl("parent",&m_parent);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("right",&m_right);
	return t;
}
c_MapKeys::c_MapKeys(){
	m_map=0;
}
c_MapKeys* c_MapKeys::m_new(c_Map2* t_map){
	DBG_ENTER("MapKeys.new")
	c_MapKeys *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_map,"map")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<503>");
	gc_assign(this->m_map,t_map);
	return this;
}
c_MapKeys* c_MapKeys::m_new2(){
	DBG_ENTER("MapKeys.new")
	c_MapKeys *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<500>");
	return this;
}
c_KeyEnumerator* c_MapKeys::p_ObjectEnumerator(){
	DBG_ENTER("MapKeys.ObjectEnumerator")
	c_MapKeys *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<507>");
	c_KeyEnumerator* t_=(new c_KeyEnumerator)->m_new(m_map->p_FirstNode());
	return t_;
}
void c_MapKeys::mark(){
	Object::mark();
	gc_mark_q(m_map);
}
String c_MapKeys::debug(){
	String t="(MapKeys)\n";
	t+=dbg_decl("map",&m_map);
	return t;
}
c_KeyEnumerator::c_KeyEnumerator(){
	m_node=0;
}
c_KeyEnumerator* c_KeyEnumerator::m_new(c_Node4* t_node){
	DBG_ENTER("KeyEnumerator.new")
	c_KeyEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<459>");
	gc_assign(this->m_node,t_node);
	return this;
}
c_KeyEnumerator* c_KeyEnumerator::m_new2(){
	DBG_ENTER("KeyEnumerator.new")
	c_KeyEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<456>");
	return this;
}
bool c_KeyEnumerator::p_HasNext(){
	DBG_ENTER("KeyEnumerator.HasNext")
	c_KeyEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<463>");
	bool t_=m_node!=0;
	return t_;
}
String c_KeyEnumerator::p_NextObject(){
	DBG_ENTER("KeyEnumerator.NextObject")
	c_KeyEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<467>");
	c_Node4* t_t=m_node;
	DBG_LOCAL(t_t,"t")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<468>");
	gc_assign(m_node,m_node->p_NextNode());
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<469>");
	return t_t->m_key;
}
void c_KeyEnumerator::mark(){
	Object::mark();
	gc_mark_q(m_node);
}
String c_KeyEnumerator::debug(){
	String t="(KeyEnumerator)\n";
	t+=dbg_decl("node",&m_node);
	return t;
}
c_MapValues::c_MapValues(){
	m_map=0;
}
c_MapValues* c_MapValues::m_new(c_Map* t_map){
	DBG_ENTER("MapValues.new")
	c_MapValues *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_map,"map")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<519>");
	gc_assign(this->m_map,t_map);
	return this;
}
c_MapValues* c_MapValues::m_new2(){
	DBG_ENTER("MapValues.new")
	c_MapValues *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<516>");
	return this;
}
c_ValueEnumerator* c_MapValues::p_ObjectEnumerator(){
	DBG_ENTER("MapValues.ObjectEnumerator")
	c_MapValues *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<523>");
	c_ValueEnumerator* t_=(new c_ValueEnumerator)->m_new(m_map->p_FirstNode());
	return t_;
}
void c_MapValues::mark(){
	Object::mark();
	gc_mark_q(m_map);
}
String c_MapValues::debug(){
	String t="(MapValues)\n";
	t+=dbg_decl("map",&m_map);
	return t;
}
c_ValueEnumerator::c_ValueEnumerator(){
	m_node=0;
}
c_ValueEnumerator* c_ValueEnumerator::m_new(c_Node* t_node){
	DBG_ENTER("ValueEnumerator.new")
	c_ValueEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<481>");
	gc_assign(this->m_node,t_node);
	return this;
}
c_ValueEnumerator* c_ValueEnumerator::m_new2(){
	DBG_ENTER("ValueEnumerator.new")
	c_ValueEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<478>");
	return this;
}
bool c_ValueEnumerator::p_HasNext(){
	DBG_ENTER("ValueEnumerator.HasNext")
	c_ValueEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<485>");
	bool t_=m_node!=0;
	return t_;
}
c_File* c_ValueEnumerator::p_NextObject(){
	DBG_ENTER("ValueEnumerator.NextObject")
	c_ValueEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<489>");
	c_Node* t_t=m_node;
	DBG_LOCAL(t_t,"t")
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<490>");
	gc_assign(m_node,m_node->p_NextNode());
	DBG_INFO("/Applications/Monkey/modules/monkey/map.monkey<491>");
	return t_t->m_value;
}
void c_ValueEnumerator::mark(){
	Object::mark();
	gc_mark_q(m_node);
}
String c_ValueEnumerator::debug(){
	String t="(ValueEnumerator)\n";
	t+=dbg_decl("node",&m_node);
	return t;
}
int bbMain(){
	DBG_ENTER("Main")
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard.monkey<10>");
	(new c_App)->m_new();
	DBG_INFO("/Users/jochenheizmann/Dropbox/Monkey/modules_ext/fairlight/vendor/monkey-wizard/wizard.monkey<11>");
	return 0;
}
c_ConstInfo::c_ConstInfo(){
	m__name=String();
	m__attrs=0;
	m__type=0;
	m__value=0;
}
c_ConstInfo* c_ConstInfo::m_new(String t_name,int t_attrs,c_ClassInfo* t_type,Object* t_value){
	DBG_ENTER("ConstInfo.new")
	c_ConstInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_attrs,"attrs")
	DBG_LOCAL(t_type,"type")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<45>");
	m__name=t_name;
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<46>");
	m__attrs=t_attrs;
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<47>");
	gc_assign(m__type,t_type);
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<48>");
	gc_assign(m__value,t_value);
	return this;
}
c_ConstInfo* c_ConstInfo::m_new2(){
	DBG_ENTER("ConstInfo.new")
	c_ConstInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<42>");
	return this;
}
void c_ConstInfo::mark(){
	Object::mark();
	gc_mark_q(m__type);
	gc_mark_q(m__value);
}
String c_ConstInfo::debug(){
	String t="(ConstInfo)\n";
	t+=dbg_decl("_name",&m__name);
	t+=dbg_decl("_attrs",&m__attrs);
	t+=dbg_decl("_type",&m__type);
	t+=dbg_decl("_value",&m__value);
	return t;
}
c_Stack::c_Stack(){
	m_data=Array<c_ConstInfo* >();
	m_length=0;
}
c_Stack* c_Stack::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack* c_Stack::m_new2(Array<c_ConstInfo* > t_data){
	DBG_ENTER("Stack.new")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack::p_Push(c_ConstInfo* t_value){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<67>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<68>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<70>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<71>");
	m_length+=1;
}
void c_Stack::p_Push2(Array<c_ConstInfo* > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<79>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<80>");
		p_Push(t_values.At(t_offset+t_i));
	}
}
void c_Stack::p_Push3(Array<c_ConstInfo* > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<75>");
	p_Push2(t_values,t_offset,t_values.Length()-t_offset);
}
Array<c_ConstInfo* > c_Stack::p_ToArray(){
	DBG_ENTER("Stack.ToArray")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<18>");
	Array<c_ConstInfo* > t_t=Array<c_ConstInfo* >(m_length);
	DBG_LOCAL(t_t,"t")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<19>");
	for(int t_i=0;t_i<m_length;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<20>");
		gc_assign(t_t.At(t_i),m_data.At(t_i));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<22>");
	return t_t;
}
void c_Stack::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_FieldInfo::c_FieldInfo(){
	m__name=String();
	m__attrs=0;
	m__type=0;
}
c_FieldInfo* c_FieldInfo::m_new(String t_name,int t_attrs,c_ClassInfo* t_type){
	DBG_ENTER("FieldInfo.new")
	c_FieldInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_attrs,"attrs")
	DBG_LOCAL(t_type,"type")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<111>");
	m__name=t_name;
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<112>");
	m__attrs=t_attrs;
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<113>");
	gc_assign(m__type,t_type);
	return this;
}
c_FieldInfo* c_FieldInfo::m_new2(){
	DBG_ENTER("FieldInfo.new")
	c_FieldInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<108>");
	return this;
}
void c_FieldInfo::mark(){
	Object::mark();
	gc_mark_q(m__type);
}
String c_FieldInfo::debug(){
	String t="(FieldInfo)\n";
	t+=dbg_decl("_name",&m__name);
	t+=dbg_decl("_attrs",&m__attrs);
	t+=dbg_decl("_type",&m__type);
	return t;
}
c_Stack2::c_Stack2(){
	m_data=Array<c_FieldInfo* >();
	m_length=0;
}
c_Stack2* c_Stack2::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack2* c_Stack2::m_new2(Array<c_FieldInfo* > t_data){
	DBG_ENTER("Stack.new")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack2::p_Push4(c_FieldInfo* t_value){
	DBG_ENTER("Stack.Push")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<67>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<68>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<70>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<71>");
	m_length+=1;
}
void c_Stack2::p_Push5(Array<c_FieldInfo* > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<79>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<80>");
		p_Push4(t_values.At(t_offset+t_i));
	}
}
void c_Stack2::p_Push6(Array<c_FieldInfo* > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<75>");
	p_Push5(t_values,t_offset,t_values.Length()-t_offset);
}
Array<c_FieldInfo* > c_Stack2::p_ToArray(){
	DBG_ENTER("Stack.ToArray")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<18>");
	Array<c_FieldInfo* > t_t=Array<c_FieldInfo* >(m_length);
	DBG_LOCAL(t_t,"t")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<19>");
	for(int t_i=0;t_i<m_length;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<20>");
		gc_assign(t_t.At(t_i),m_data.At(t_i));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<22>");
	return t_t;
}
void c_Stack2::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack2::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_GlobalInfo::c_GlobalInfo(){
}
void c_GlobalInfo::mark(){
	Object::mark();
}
String c_GlobalInfo::debug(){
	String t="(GlobalInfo)\n";
	return t;
}
c_Stack3::c_Stack3(){
	m_data=Array<c_GlobalInfo* >();
	m_length=0;
}
c_Stack3* c_Stack3::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack3* c_Stack3::m_new2(Array<c_GlobalInfo* > t_data){
	DBG_ENTER("Stack.new")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack3::p_Push7(c_GlobalInfo* t_value){
	DBG_ENTER("Stack.Push")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<67>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<68>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<70>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<71>");
	m_length+=1;
}
void c_Stack3::p_Push8(Array<c_GlobalInfo* > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<79>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<80>");
		p_Push7(t_values.At(t_offset+t_i));
	}
}
void c_Stack3::p_Push9(Array<c_GlobalInfo* > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<75>");
	p_Push8(t_values,t_offset,t_values.Length()-t_offset);
}
Array<c_GlobalInfo* > c_Stack3::p_ToArray(){
	DBG_ENTER("Stack.ToArray")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<18>");
	Array<c_GlobalInfo* > t_t=Array<c_GlobalInfo* >(m_length);
	DBG_LOCAL(t_t,"t")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<19>");
	for(int t_i=0;t_i<m_length;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<20>");
		gc_assign(t_t.At(t_i),m_data.At(t_i));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<22>");
	return t_t;
}
void c_Stack3::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack3::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_MethodInfo::c_MethodInfo(){
	m__name=String();
	m__attrs=0;
	m__retType=0;
	m__argTypes=Array<c_ClassInfo* >();
}
c_MethodInfo* c_MethodInfo::m_new(String t_name,int t_attrs,c_ClassInfo* t_retType,Array<c_ClassInfo* > t_argTypes){
	DBG_ENTER("MethodInfo.new")
	c_MethodInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_attrs,"attrs")
	DBG_LOCAL(t_retType,"retType")
	DBG_LOCAL(t_argTypes,"argTypes")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<143>");
	m__name=t_name;
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<144>");
	m__attrs=t_attrs;
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<145>");
	gc_assign(m__retType,t_retType);
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<146>");
	gc_assign(m__argTypes,t_argTypes);
	return this;
}
c_MethodInfo* c_MethodInfo::m_new2(){
	DBG_ENTER("MethodInfo.new")
	c_MethodInfo *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/reflection/reflection.monkey<140>");
	return this;
}
void c_MethodInfo::mark(){
	Object::mark();
	gc_mark_q(m__retType);
	gc_mark_q(m__argTypes);
}
String c_MethodInfo::debug(){
	String t="(MethodInfo)\n";
	t+=dbg_decl("_name",&m__name);
	t+=dbg_decl("_attrs",&m__attrs);
	t+=dbg_decl("_retType",&m__retType);
	t+=dbg_decl("_argTypes",&m__argTypes);
	return t;
}
c_Stack4::c_Stack4(){
	m_data=Array<c_MethodInfo* >();
	m_length=0;
}
c_Stack4* c_Stack4::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack4* c_Stack4::m_new2(Array<c_MethodInfo* > t_data){
	DBG_ENTER("Stack.new")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack4::p_Push10(c_MethodInfo* t_value){
	DBG_ENTER("Stack.Push")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<67>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<68>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<70>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<71>");
	m_length+=1;
}
void c_Stack4::p_Push11(Array<c_MethodInfo* > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<79>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<80>");
		p_Push10(t_values.At(t_offset+t_i));
	}
}
void c_Stack4::p_Push12(Array<c_MethodInfo* > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<75>");
	p_Push11(t_values,t_offset,t_values.Length()-t_offset);
}
Array<c_MethodInfo* > c_Stack4::p_ToArray(){
	DBG_ENTER("Stack.ToArray")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<18>");
	Array<c_MethodInfo* > t_t=Array<c_MethodInfo* >(m_length);
	DBG_LOCAL(t_t,"t")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<19>");
	for(int t_i=0;t_i<m_length;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<20>");
		gc_assign(t_t.At(t_i),m_data.At(t_i));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<22>");
	return t_t;
}
void c_Stack4::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack4::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_Stack5::c_Stack5(){
	m_data=Array<c_FunctionInfo* >();
	m_length=0;
}
c_Stack5* c_Stack5::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack5 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack5* c_Stack5::m_new2(Array<c_FunctionInfo* > t_data){
	DBG_ENTER("Stack.new")
	c_Stack5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack5::p_Push13(c_FunctionInfo* t_value){
	DBG_ENTER("Stack.Push")
	c_Stack5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<67>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<68>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<70>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<71>");
	m_length+=1;
}
void c_Stack5::p_Push14(Array<c_FunctionInfo* > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<79>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<80>");
		p_Push13(t_values.At(t_offset+t_i));
	}
}
void c_Stack5::p_Push15(Array<c_FunctionInfo* > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<75>");
	p_Push14(t_values,t_offset,t_values.Length()-t_offset);
}
Array<c_FunctionInfo* > c_Stack5::p_ToArray(){
	DBG_ENTER("Stack.ToArray")
	c_Stack5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<18>");
	Array<c_FunctionInfo* > t_t=Array<c_FunctionInfo* >(m_length);
	DBG_LOCAL(t_t,"t")
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<19>");
	for(int t_i=0;t_i<m_length;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<20>");
		gc_assign(t_t.At(t_i),m_data.At(t_i));
	}
	DBG_INFO("/Applications/Monkey/modules/monkey/stack.monkey<22>");
	return t_t;
}
void c_Stack5::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack5::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_R47::c_R47(){
}
c_R47* c_R47::m_new(){
	c_FieldInfo::m_new(String(L"value",5),0,bb_reflection__boolClass);
	return this;
}
void c_R47::mark(){
	c_FieldInfo::mark();
}
String c_R47::debug(){
	String t="(R47)\n";
	t=c_FieldInfo::debug()+t;
	return t;
}
c_R49::c_R49(){
}
c_R49* c_R49::m_new(){
	c_MethodInfo::m_new(String(L"ToBool",6),0,bb_reflection__boolClass,Array<c_ClassInfo* >());
	return this;
}
void c_R49::mark(){
	c_MethodInfo::mark();
}
String c_R49::debug(){
	String t="(R49)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R50::c_R50(){
}
c_R50* c_R50::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(1)};
	c_MethodInfo::m_new(String(L"Equals",6),0,bb_reflection__boolClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R50::mark(){
	c_MethodInfo::mark();
}
String c_R50::debug(){
	String t="(R50)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R48::c_R48(){
}
c_R48* c_R48::m_new(){
	c_ClassInfo* t_[]={bb_reflection__boolClass};
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(1),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R48::mark(){
	c_FunctionInfo::mark();
}
String c_R48::debug(){
	String t="(R48)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R51::c_R51(){
}
c_R51* c_R51::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(1),Array<c_ClassInfo* >());
	return this;
}
void c_R51::mark(){
	c_FunctionInfo::mark();
}
String c_R51::debug(){
	String t="(R51)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R53::c_R53(){
}
c_R53* c_R53::m_new(){
	c_FieldInfo::m_new(String(L"value",5),0,bb_reflection__intClass);
	return this;
}
void c_R53::mark(){
	c_FieldInfo::mark();
}
String c_R53::debug(){
	String t="(R53)\n";
	t=c_FieldInfo::debug()+t;
	return t;
}
c_R56::c_R56(){
}
c_R56* c_R56::m_new(){
	c_MethodInfo::m_new(String(L"ToInt",5),0,bb_reflection__intClass,Array<c_ClassInfo* >());
	return this;
}
void c_R56::mark(){
	c_MethodInfo::mark();
}
String c_R56::debug(){
	String t="(R56)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R57::c_R57(){
}
c_R57* c_R57::m_new(){
	c_MethodInfo::m_new(String(L"ToFloat",7),0,bb_reflection__floatClass,Array<c_ClassInfo* >());
	return this;
}
void c_R57::mark(){
	c_MethodInfo::mark();
}
String c_R57::debug(){
	String t="(R57)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R58::c_R58(){
}
c_R58* c_R58::m_new(){
	c_MethodInfo::m_new(String(L"ToString",8),0,bb_reflection__stringClass,Array<c_ClassInfo* >());
	return this;
}
void c_R58::mark(){
	c_MethodInfo::mark();
}
String c_R58::debug(){
	String t="(R58)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R59::c_R59(){
}
c_R59* c_R59::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(2)};
	c_MethodInfo::m_new(String(L"Equals",6),0,bb_reflection__boolClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R59::mark(){
	c_MethodInfo::mark();
}
String c_R59::debug(){
	String t="(R59)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R60::c_R60(){
}
c_R60* c_R60::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(2)};
	c_MethodInfo::m_new(String(L"Compare",7),0,bb_reflection__intClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R60::mark(){
	c_MethodInfo::mark();
}
String c_R60::debug(){
	String t="(R60)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R54::c_R54(){
}
c_R54* c_R54::m_new(){
	c_ClassInfo* t_[]={bb_reflection__intClass};
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(2),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R54::mark(){
	c_FunctionInfo::mark();
}
String c_R54::debug(){
	String t="(R54)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R55::c_R55(){
}
c_R55* c_R55::m_new(){
	c_ClassInfo* t_[]={bb_reflection__floatClass};
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(2),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R55::mark(){
	c_FunctionInfo::mark();
}
String c_R55::debug(){
	String t="(R55)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R61::c_R61(){
}
c_R61* c_R61::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(2),Array<c_ClassInfo* >());
	return this;
}
void c_R61::mark(){
	c_FunctionInfo::mark();
}
String c_R61::debug(){
	String t="(R61)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R63::c_R63(){
}
c_R63* c_R63::m_new(){
	c_FieldInfo::m_new(String(L"value",5),0,bb_reflection__floatClass);
	return this;
}
void c_R63::mark(){
	c_FieldInfo::mark();
}
String c_R63::debug(){
	String t="(R63)\n";
	t=c_FieldInfo::debug()+t;
	return t;
}
c_R66::c_R66(){
}
c_R66* c_R66::m_new(){
	c_MethodInfo::m_new(String(L"ToInt",5),0,bb_reflection__intClass,Array<c_ClassInfo* >());
	return this;
}
void c_R66::mark(){
	c_MethodInfo::mark();
}
String c_R66::debug(){
	String t="(R66)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R67::c_R67(){
}
c_R67* c_R67::m_new(){
	c_MethodInfo::m_new(String(L"ToFloat",7),0,bb_reflection__floatClass,Array<c_ClassInfo* >());
	return this;
}
void c_R67::mark(){
	c_MethodInfo::mark();
}
String c_R67::debug(){
	String t="(R67)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R68::c_R68(){
}
c_R68* c_R68::m_new(){
	c_MethodInfo::m_new(String(L"ToString",8),0,bb_reflection__stringClass,Array<c_ClassInfo* >());
	return this;
}
void c_R68::mark(){
	c_MethodInfo::mark();
}
String c_R68::debug(){
	String t="(R68)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R69::c_R69(){
}
c_R69* c_R69::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(3)};
	c_MethodInfo::m_new(String(L"Equals",6),0,bb_reflection__boolClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R69::mark(){
	c_MethodInfo::mark();
}
String c_R69::debug(){
	String t="(R69)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R70::c_R70(){
}
c_R70* c_R70::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(3)};
	c_MethodInfo::m_new(String(L"Compare",7),0,bb_reflection__intClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R70::mark(){
	c_MethodInfo::mark();
}
String c_R70::debug(){
	String t="(R70)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R64::c_R64(){
}
c_R64* c_R64::m_new(){
	c_ClassInfo* t_[]={bb_reflection__intClass};
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(3),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R64::mark(){
	c_FunctionInfo::mark();
}
String c_R64::debug(){
	String t="(R64)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R65::c_R65(){
}
c_R65* c_R65::m_new(){
	c_ClassInfo* t_[]={bb_reflection__floatClass};
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(3),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R65::mark(){
	c_FunctionInfo::mark();
}
String c_R65::debug(){
	String t="(R65)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R71::c_R71(){
}
c_R71* c_R71::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(3),Array<c_ClassInfo* >());
	return this;
}
void c_R71::mark(){
	c_FunctionInfo::mark();
}
String c_R71::debug(){
	String t="(R71)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R73::c_R73(){
}
c_R73* c_R73::m_new(){
	c_FieldInfo::m_new(String(L"value",5),0,bb_reflection__stringClass);
	return this;
}
void c_R73::mark(){
	c_FieldInfo::mark();
}
String c_R73::debug(){
	String t="(R73)\n";
	t=c_FieldInfo::debug()+t;
	return t;
}
c_R77::c_R77(){
}
c_R77* c_R77::m_new(){
	c_MethodInfo::m_new(String(L"ToString",8),0,bb_reflection__stringClass,Array<c_ClassInfo* >());
	return this;
}
void c_R77::mark(){
	c_MethodInfo::mark();
}
String c_R77::debug(){
	String t="(R77)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R78::c_R78(){
}
c_R78* c_R78::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(4)};
	c_MethodInfo::m_new(String(L"Equals",6),0,bb_reflection__boolClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R78::mark(){
	c_MethodInfo::mark();
}
String c_R78::debug(){
	String t="(R78)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R79::c_R79(){
}
c_R79* c_R79::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(4)};
	c_MethodInfo::m_new(String(L"Compare",7),0,bb_reflection__intClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R79::mark(){
	c_MethodInfo::mark();
}
String c_R79::debug(){
	String t="(R79)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R74::c_R74(){
}
c_R74* c_R74::m_new(){
	c_ClassInfo* t_[]={bb_reflection__intClass};
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(4),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R74::mark(){
	c_FunctionInfo::mark();
}
String c_R74::debug(){
	String t="(R74)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R75::c_R75(){
}
c_R75* c_R75::m_new(){
	c_ClassInfo* t_[]={bb_reflection__floatClass};
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(4),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R75::mark(){
	c_FunctionInfo::mark();
}
String c_R75::debug(){
	String t="(R75)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R76::c_R76(){
}
c_R76* c_R76::m_new(){
	c_ClassInfo* t_[]={bb_reflection__stringClass};
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(4),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R76::mark(){
	c_FunctionInfo::mark();
}
String c_R76::debug(){
	String t="(R76)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R80::c_R80(){
}
c_R80* c_R80::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(4),Array<c_ClassInfo* >());
	return this;
}
void c_R80::mark(){
	c_FunctionInfo::mark();
}
String c_R80::debug(){
	String t="(R80)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R83::c_R83(){
}
c_R83* c_R83::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(6),Array<c_ClassInfo* >());
	return this;
}
void c_R83::mark(){
	c_FunctionInfo::mark();
}
String c_R83::debug(){
	String t="(R83)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R85::c_R85(){
}
c_R85* c_R85::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(7),Array<c_ClassInfo* >());
	return this;
}
void c_R85::mark(){
	c_FunctionInfo::mark();
}
String c_R85::debug(){
	String t="(R85)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R87::c_R87(){
}
c_R87* c_R87::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(8),Array<c_ClassInfo* >());
	return this;
}
void c_R87::mark(){
	c_FunctionInfo::mark();
}
String c_R87::debug(){
	String t="(R87)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R89::c_R89(){
}
c_R89* c_R89::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(9),Array<c_ClassInfo* >());
	return this;
}
void c_R89::mark(){
	c_FunctionInfo::mark();
}
String c_R89::debug(){
	String t="(R89)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R91::c_R91(){
}
c_R91* c_R91::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(10),Array<c_ClassInfo* >());
	return this;
}
void c_R91::mark(){
	c_FunctionInfo::mark();
}
String c_R91::debug(){
	String t="(R91)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R93::c_R93(){
}
c_R93* c_R93::m_new(){
	c_FieldInfo::m_new(String(L"VALID_TYPES",11),2,bb_reflection__classes.At(36));
	return this;
}
void c_R93::mark(){
	c_FieldInfo::mark();
}
String c_R93::debug(){
	String t="(R93)\n";
	t=c_FieldInfo::debug()+t;
	return t;
}
c_R94::c_R94(){
}
c_R94* c_R94::m_new(){
	c_MethodInfo::m_new(String(L"CheckArgs",9),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_R94::mark(){
	c_MethodInfo::mark();
}
String c_R94::debug(){
	String t="(R94)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R95::c_R95(){
}
c_R95* c_R95::m_new(){
	c_MethodInfo::m_new(String(L"IsValidType",11),0,bb_reflection__boolClass,Array<c_ClassInfo* >());
	return this;
}
void c_R95::mark(){
	c_MethodInfo::mark();
}
String c_R95::debug(){
	String t="(R95)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R96::c_R96(){
}
c_R96* c_R96::m_new(){
	c_MethodInfo::m_new(String(L"IsValidFilename",15),0,bb_reflection__boolClass,Array<c_ClassInfo* >());
	return this;
}
void c_R96::mark(){
	c_MethodInfo::mark();
}
String c_R96::debug(){
	String t="(R96)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R97::c_R97(){
}
c_R97* c_R97::m_new(){
	c_MethodInfo::m_new(String(L"GetType",7),0,bb_reflection__stringClass,Array<c_ClassInfo* >());
	return this;
}
void c_R97::mark(){
	c_MethodInfo::mark();
}
String c_R97::debug(){
	String t="(R97)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R98::c_R98(){
}
c_R98* c_R98::m_new(){
	c_MethodInfo::m_new(String(L"GetShortType",12),0,bb_reflection__stringClass,Array<c_ClassInfo* >());
	return this;
}
void c_R98::mark(){
	c_MethodInfo::mark();
}
String c_R98::debug(){
	String t="(R98)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R99::c_R99(){
}
c_R99* c_R99::m_new(){
	c_MethodInfo::m_new(String(L"GetFilename",11),0,bb_reflection__stringClass,Array<c_ClassInfo* >());
	return this;
}
void c_R99::mark(){
	c_MethodInfo::mark();
}
String c_R99::debug(){
	String t="(R99)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R100::c_R100(){
}
c_R100* c_R100::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(11),Array<c_ClassInfo* >());
	return this;
}
void c_R100::mark(){
	c_FunctionInfo::mark();
}
String c_R100::debug(){
	String t="(R100)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R102::c_R102(){
}
c_R102* c_R102::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(12),Array<c_ClassInfo* >());
	return this;
}
void c_R102::mark(){
	c_FunctionInfo::mark();
}
String c_R102::debug(){
	String t="(R102)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R104::c_R104(){
}
c_R104* c_R104::m_new(){
	c_MethodInfo::m_new(String(L"PatchPermissions",16),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_R104::mark(){
	c_MethodInfo::mark();
}
String c_R104::debug(){
	String t="(R104)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R105::c_R105(){
}
c_R105* c_R105::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(13),Array<c_ClassInfo* >());
	return this;
}
void c_R105::mark(){
	c_FunctionInfo::mark();
}
String c_R105::debug(){
	String t="(R105)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R107::c_R107(){
}
c_R107* c_R107::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(14),Array<c_ClassInfo* >());
	return this;
}
void c_R107::mark(){
	c_FunctionInfo::mark();
}
String c_R107::debug(){
	String t="(R107)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R109::c_R109(){
}
c_R109* c_R109::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(15),Array<c_ClassInfo* >());
	return this;
}
void c_R109::mark(){
	c_FunctionInfo::mark();
}
String c_R109::debug(){
	String t="(R109)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R111::c_R111(){
}
c_R111* c_R111::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(16),Array<c_ClassInfo* >());
	return this;
}
void c_R111::mark(){
	c_FunctionInfo::mark();
}
String c_R111::debug(){
	String t="(R111)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R113::c_R113(){
}
c_R113* c_R113::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(17),Array<c_ClassInfo* >());
	return this;
}
void c_R113::mark(){
	c_FunctionInfo::mark();
}
String c_R113::debug(){
	String t="(R113)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R115::c_R115(){
}
c_R115* c_R115::m_new(){
	c_FunctionInfo::m_new(String(L"GetRegions",10),0,bb_reflection__classes.At(36),Array<c_ClassInfo* >());
	return this;
}
void c_R115::mark(){
	c_FunctionInfo::mark();
}
String c_R115::debug(){
	String t="(R115)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R116::c_R116(){
}
c_R116* c_R116::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(18),Array<c_ClassInfo* >());
	return this;
}
void c_R116::mark(){
	c_FunctionInfo::mark();
}
String c_R116::debug(){
	String t="(R116)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R118::c_R118(){
}
c_R118* c_R118::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(19),Array<c_ClassInfo* >());
	return this;
}
void c_R118::mark(){
	c_FunctionInfo::mark();
}
String c_R118::debug(){
	String t="(R118)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R120::c_R120(){
}
c_R120* c_R120::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(20),Array<c_ClassInfo* >());
	return this;
}
void c_R120::mark(){
	c_FunctionInfo::mark();
}
String c_R120::debug(){
	String t="(R120)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R122::c_R122(){
}
c_R122* c_R122::m_new(){
	c_MethodInfo::m_new(String(L"RemoveOldSettings",17),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_R122::mark(){
	c_MethodInfo::mark();
}
String c_R122::debug(){
	String t="(R122)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R123::c_R123(){
}
c_R123* c_R123::m_new(){
	c_ClassInfo* t_[]={bb_reflection__boolClass};
	c_MethodInfo::m_new(String(L"AddSettings",11),0,0,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R123::mark(){
	c_MethodInfo::mark();
}
String c_R123::debug(){
	String t="(R123)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R124::c_R124(){
}
c_R124* c_R124::m_new(){
	c_MethodInfo::m_new(String(L"IsEnabled",9),0,bb_reflection__boolClass,Array<c_ClassInfo* >());
	return this;
}
void c_R124::mark(){
	c_MethodInfo::mark();
}
String c_R124::debug(){
	String t="(R124)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R125::c_R125(){
}
c_R125* c_R125::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(21),Array<c_ClassInfo* >());
	return this;
}
void c_R125::mark(){
	c_FunctionInfo::mark();
}
String c_R125::debug(){
	String t="(R125)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R127::c_R127(){
}
c_R127* c_R127::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(22),Array<c_ClassInfo* >());
	return this;
}
void c_R127::mark(){
	c_FunctionInfo::mark();
}
String c_R127::debug(){
	String t="(R127)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R129::c_R129(){
}
c_R129* c_R129::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(23),Array<c_ClassInfo* >());
	return this;
}
void c_R129::mark(){
	c_FunctionInfo::mark();
}
String c_R129::debug(){
	String t="(R129)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R131::c_R131(){
}
c_R131* c_R131::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(24),Array<c_ClassInfo* >());
	return this;
}
void c_R131::mark(){
	c_FunctionInfo::mark();
}
String c_R131::debug(){
	String t="(R131)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R133::c_R133(){
}
c_R133* c_R133::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(25),Array<c_ClassInfo* >());
	return this;
}
void c_R133::mark(){
	c_FunctionInfo::mark();
}
String c_R133::debug(){
	String t="(R133)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R135::c_R135(){
}
c_R135* c_R135::m_new(){
	c_MethodInfo::m_new(String(L"RemoveOldSettings",17),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_R135::mark(){
	c_MethodInfo::mark();
}
String c_R135::debug(){
	String t="(R135)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R136::c_R136(){
}
c_R136* c_R136::m_new(){
	c_MethodInfo::m_new(String(L"AddSettings",11),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_R136::mark(){
	c_MethodInfo::mark();
}
String c_R136::debug(){
	String t="(R136)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R137::c_R137(){
}
c_R137* c_R137::m_new(){
	c_MethodInfo::m_new(String(L"IsEnabled",9),0,bb_reflection__boolClass,Array<c_ClassInfo* >());
	return this;
}
void c_R137::mark(){
	c_MethodInfo::mark();
}
String c_R137::debug(){
	String t="(R137)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R138::c_R138(){
}
c_R138* c_R138::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(26),Array<c_ClassInfo* >());
	return this;
}
void c_R138::mark(){
	c_FunctionInfo::mark();
}
String c_R138::debug(){
	String t="(R138)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R140::c_R140(){
}
c_R140* c_R140::m_new(){
	c_ClassInfo* t_[]={bb_reflection__boolClass};
	c_MethodInfo::m_new(String(L"AddPrerenderedFlag",18),0,0,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R140::mark(){
	c_MethodInfo::mark();
}
String c_R140::debug(){
	String t="(R140)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R141::c_R141(){
}
c_R141* c_R141::m_new(){
	c_MethodInfo::m_new(String(L"RemovePrerenderedFlag",21),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_R141::mark(){
	c_MethodInfo::mark();
}
String c_R141::debug(){
	String t="(R141)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R142::c_R142(){
}
c_R142* c_R142::m_new(){
	c_MethodInfo::m_new(String(L"RemoveIcons",11),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_R142::mark(){
	c_MethodInfo::mark();
}
String c_R142::debug(){
	String t="(R142)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R143::c_R143(){
}
c_R143* c_R143::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(36)};
	c_MethodInfo::m_new(String(L"ParseRowsAndRemoveFiles",23),0,0,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R143::mark(){
	c_MethodInfo::mark();
}
String c_R143::debug(){
	String t="(R143)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R144::c_R144(){
}
c_R144* c_R144::m_new(){
	c_ClassInfo* t_[]={bb_reflection__stringClass};
	c_MethodInfo::m_new(String(L"RemoveKeyWithValues",19),0,bb_reflection__classes.At(36),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R144::mark(){
	c_MethodInfo::mark();
}
String c_R144::debug(){
	String t="(R144)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R145::c_R145(){
}
c_R145* c_R145::m_new(){
	c_ClassInfo* t_[]={bb_reflection__stringClass};
	c_MethodInfo::m_new(String(L"ExtractFileName",15),0,bb_reflection__stringClass,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R145::mark(){
	c_MethodInfo::mark();
}
String c_R145::debug(){
	String t="(R145)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R146::c_R146(){
}
c_R146* c_R146::m_new(){
	c_ClassInfo* t_[]={bb_reflection__stringClass};
	c_MethodInfo::m_new(String(L"RemoveFileDefinition",20),0,0,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R146::mark(){
	c_MethodInfo::mark();
}
String c_R146::debug(){
	String t="(R146)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R147::c_R147(){
}
c_R147* c_R147::m_new(){
	c_ClassInfo* t_[]={bb_reflection__stringClass};
	c_MethodInfo::m_new(String(L"RemoveFilePhysical",18),0,0,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R147::mark(){
	c_MethodInfo::mark();
}
String c_R147::debug(){
	String t="(R147)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R148::c_R148(){
}
c_R148* c_R148::m_new(){
	c_MethodInfo::m_new(String(L"IsPrerendered",13),0,bb_reflection__boolClass,Array<c_ClassInfo* >());
	return this;
}
void c_R148::mark(){
	c_MethodInfo::mark();
}
String c_R148::debug(){
	String t="(R148)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R149::c_R149(){
}
c_R149* c_R149::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(27),Array<c_ClassInfo* >());
	return this;
}
void c_R149::mark(){
	c_FunctionInfo::mark();
}
String c_R149::debug(){
	String t="(R149)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R151::c_R151(){
}
c_R151* c_R151::m_new(){
	c_MethodInfo::m_new(String(L"AddOrientationBoth",18),0,bb_reflection__stringClass,Array<c_ClassInfo* >());
	return this;
}
void c_R151::mark(){
	c_MethodInfo::mark();
}
String c_R151::debug(){
	String t="(R151)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R152::c_R152(){
}
c_R152* c_R152::m_new(){
	c_MethodInfo::m_new(String(L"AddOrientationLandscape",23),0,bb_reflection__stringClass,Array<c_ClassInfo* >());
	return this;
}
void c_R152::mark(){
	c_MethodInfo::mark();
}
String c_R152::debug(){
	String t="(R152)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R153::c_R153(){
}
c_R153* c_R153::m_new(){
	c_MethodInfo::m_new(String(L"AddOrientationPortrait",22),0,bb_reflection__stringClass,Array<c_ClassInfo* >());
	return this;
}
void c_R153::mark(){
	c_MethodInfo::mark();
}
String c_R153::debug(){
	String t="(R153)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R154::c_R154(){
}
c_R154* c_R154::m_new(){
	c_MethodInfo::m_new(String(L"GetOrientation",14),0,bb_reflection__stringClass,Array<c_ClassInfo* >());
	return this;
}
void c_R154::mark(){
	c_MethodInfo::mark();
}
String c_R154::debug(){
	String t="(R154)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R155::c_R155(){
}
c_R155* c_R155::m_new(){
	c_MethodInfo::m_new(String(L"RemoveOldSettings",17),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_R155::mark(){
	c_MethodInfo::mark();
}
String c_R155::debug(){
	String t="(R155)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R156::c_R156(){
}
c_R156* c_R156::m_new(){
	c_ClassInfo* t_[]={bb_reflection__intClass};
	c_MethodInfo::m_new(String(L"RemoveKeyWithValues",19),0,0,Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R156::mark(){
	c_MethodInfo::mark();
}
String c_R156::debug(){
	String t="(R156)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R157::c_R157(){
}
c_R157* c_R157::m_new(){
	c_MethodInfo::m_new(String(L"AddOrientationBothPlist",23),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_R157::mark(){
	c_MethodInfo::mark();
}
String c_R157::debug(){
	String t="(R157)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R158::c_R158(){
}
c_R158* c_R158::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(28),Array<c_ClassInfo* >());
	return this;
}
void c_R158::mark(){
	c_FunctionInfo::mark();
}
String c_R158::debug(){
	String t="(R158)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R160::c_R160(){
}
c_R160* c_R160::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(29),Array<c_ClassInfo* >());
	return this;
}
void c_R160::mark(){
	c_FunctionInfo::mark();
}
String c_R160::debug(){
	String t="(R160)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R162::c_R162(){
}
c_R162* c_R162::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(30),Array<c_ClassInfo* >());
	return this;
}
void c_R162::mark(){
	c_FunctionInfo::mark();
}
String c_R162::debug(){
	String t="(R162)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R164::c_R164(){
}
c_R164* c_R164::m_new(){
	c_MethodInfo::m_new(String(L"GetNewName",10),0,bb_reflection__stringClass,Array<c_ClassInfo* >());
	return this;
}
void c_R164::mark(){
	c_MethodInfo::mark();
}
String c_R164::debug(){
	String t="(R164)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R165::c_R165(){
}
c_R165* c_R165::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(31),Array<c_ClassInfo* >());
	return this;
}
void c_R165::mark(){
	c_FunctionInfo::mark();
}
String c_R165::debug(){
	String t="(R165)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R167::c_R167(){
}
c_R167* c_R167::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(32),Array<c_ClassInfo* >());
	return this;
}
void c_R167::mark(){
	c_FunctionInfo::mark();
}
String c_R167::debug(){
	String t="(R167)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R169::c_R169(){
}
c_R169* c_R169::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(33),Array<c_ClassInfo* >());
	return this;
}
void c_R169::mark(){
	c_FunctionInfo::mark();
}
String c_R169::debug(){
	String t="(R169)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R171::c_R171(){
}
c_R171* c_R171::m_new(){
	c_MethodInfo::m_new(String(L"AddOrientationPortrait",22),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_R171::mark(){
	c_MethodInfo::mark();
}
String c_R171::debug(){
	String t="(R171)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R172::c_R172(){
}
c_R172* c_R172::m_new(){
	c_MethodInfo::m_new(String(L"AddLibZ",7),0,0,Array<c_ClassInfo* >());
	return this;
}
void c_R172::mark(){
	c_MethodInfo::mark();
}
String c_R172::debug(){
	String t="(R172)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R173::c_R173(){
}
c_R173* c_R173::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(34),Array<c_ClassInfo* >());
	return this;
}
void c_R173::mark(){
	c_FunctionInfo::mark();
}
String c_R173::debug(){
	String t="(R173)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R175::c_R175(){
}
c_R175* c_R175::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(35),Array<c_ClassInfo* >());
	return this;
}
void c_R175::mark(){
	c_FunctionInfo::mark();
}
String c_R175::debug(){
	String t="(R175)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R177::c_R177(){
}
c_R177* c_R177::m_new(){
	c_FieldInfo::m_new(String(L"value",5),0,bb_reflection__classes.At(36));
	return this;
}
void c_R177::mark(){
	c_FieldInfo::mark();
}
String c_R177::debug(){
	String t="(R177)\n";
	t=c_FieldInfo::debug()+t;
	return t;
}
c_R179::c_R179(){
}
c_R179* c_R179::m_new(){
	c_MethodInfo::m_new(String(L"ToArray",7),0,bb_reflection__classes.At(36),Array<c_ClassInfo* >());
	return this;
}
void c_R179::mark(){
	c_MethodInfo::mark();
}
String c_R179::debug(){
	String t="(R179)\n";
	t=c_MethodInfo::debug()+t;
	return t;
}
c_R178::c_R178(){
}
c_R178* c_R178::m_new(){
	c_ClassInfo* t_[]={bb_reflection__classes.At(36)};
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(36),Array<c_ClassInfo* >(t_,1));
	return this;
}
void c_R178::mark(){
	c_FunctionInfo::mark();
}
String c_R178::debug(){
	String t="(R178)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
c_R180::c_R180(){
}
c_R180* c_R180::m_new(){
	c_FunctionInfo::m_new(String(L"new",3),0,bb_reflection__classes.At(36),Array<c_ClassInfo* >());
	return this;
}
void c_R180::mark(){
	c_FunctionInfo::mark();
}
String c_R180::debug(){
	String t="(R180)\n";
	t=c_FunctionInfo::debug()+t;
	return t;
}
int bbInit(){
	GC_CTOR
	c_Android::m_app=0;
	DBG_GLOBAL("app",&c_Android::m_app);
	c_Ios::m_app=0;
	DBG_GLOBAL("app",&c_Ios::m_app);
	bb_random_Seed=1234;
	DBG_GLOBAL("Seed",&bb_random_Seed);
	bb_reflection__classes=Array<c_ClassInfo* >();
	DBG_GLOBAL("_classes",&bb_reflection__classes);
	bb_reflection__boolClass=0;
	DBG_GLOBAL("_boolClass",&bb_reflection__boolClass);
	bb_reflection__intClass=0;
	DBG_GLOBAL("_intClass",&bb_reflection__intClass);
	bb_reflection__floatClass=0;
	DBG_GLOBAL("_floatClass",&bb_reflection__floatClass);
	bb_reflection__stringClass=0;
	DBG_GLOBAL("_stringClass",&bb_reflection__stringClass);
	bb_reflection__unknownClass=((new c_UnknownClass)->m_new());
	DBG_GLOBAL("_unknownClass",&bb_reflection__unknownClass);
	bb_reflection__functions=Array<c_FunctionInfo* >();
	DBG_GLOBAL("_functions",&bb_reflection__functions);
	bb_reflection__getClass=0;
	DBG_GLOBAL("_getClass",&bb_reflection__getClass);
	bb_reflection__init=bb_reflection___init();
	DBG_GLOBAL("_init",&bb_reflection__init);
	return 0;
}
void gc_mark(){
	gc_mark_q(c_Android::m_app);
	gc_mark_q(c_Ios::m_app);
	gc_mark_q(bb_reflection__classes);
	gc_mark_q(bb_reflection__boolClass);
	gc_mark_q(bb_reflection__intClass);
	gc_mark_q(bb_reflection__floatClass);
	gc_mark_q(bb_reflection__stringClass);
	gc_mark_q(bb_reflection__unknownClass);
	gc_mark_q(bb_reflection__functions);
	gc_mark_q(bb_reflection__getClass);
}
//${TRANSCODE_END}

String BBPathToFilePath( String path ){
	return path;
}

int main( int argc,const char **argv ){

	new BBGame();

	try{
	
		bb_std_main( argc,argv );
		
	}catch( ThrowableObject *ex ){
	
		bbPrint( "Monkey Runtime Error : Uncaught Monkey Exception" );
	
	}catch( const char *err ){
	
	}
}
