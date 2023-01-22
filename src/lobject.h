/*
 * @文件作用: 对象操作的一些函数。包括数据类型<->字符串转换
 * @功能分类: 虚拟机运转的核心功能
 * @注释者: frog-game
 * @LastEditTime: 2023-01-22 18:16:55
 */

/*
** $Id: lobject.h $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"


/*
** Extra types for collectable non-values
*/
#define LUA_TUPVAL	LUA_NUMTYPES  /* upvalues *///上值
#define LUA_TPROTO	(LUA_NUMTYPES+1)  /* function prototypes *///函数原型 不是一个公开类型
#define LUA_TDEADKEY	(LUA_NUMTYPES+2)  /* removed keys in tables *///DEADKEY



/*
** number of all possible types (including LUA_TNONE but excluding DEADKEY)
*/
 
#define LUA_TOTALTYPES		(LUA_TPROTO + 2)//所有类型的总数量（包括LUA_TNONE但不包括DEADKEY）


/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* constant)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/

// 这里是对TValue里面的lu_byte类型tt做的解释
// tt是一个unsigned char数据
// 0-3表示数据的基础类型（9种类型）
// 4-5表示（这个数据的扩展，比如说下面的长字符串、短字符串、浮点数、整数、lua闭包、c函数和c闭包这些数据扩展）
// 6表示是否是垃圾回收对象

/* add variant bits to a type */
#define makevariant(t,v)	((t) | ((v) << 4))//向类型添加bit位



/*
** Union of all Lua values
*/

/// @brief 实际存储的值
// GCObject和其他不需要进行进行GC的数据放在一个联合体里面构成了Value类型
// lua5.3以后 number类型就有了两个类型float和int 也就是下面的 lua_Number n和 lua_Integer i
typedef union Value {
  struct GCObject *gc;    /* collectable objects */// 可回收的对象
  /*下面都是不可回收类型*/
  void *p;         /* light userdata *///轻量级userdata
  lua_CFunction f; /* light C functions *///函数指针 例如（typedef int (*lua_CFunction)(lua_State *L)）
  lua_Integer i;   /* integer numbers *///int整型值 
  lua_Number n;    /* float numbers *///n是float类型的值 
} Value;


/*
** Tagged Values. This is the basic representation of values in Lua:
** an actual value plus a tag with its type.
*/

//Value 联合体 + 类型标签
//tt_来标记数据到底是什么类型的
#define TValuefields	Value value_; lu_byte tt_

/// @brief 基础数据类型
typedef struct TValue {
  TValuefields;
} TValue;


#define val_(o)		((o)->value_)//获取TValue的Value部分
#define valraw(o)	(val_(o))


/* raw type tag of a TValue */
#define rawtt(o)	((o)->tt_)//获取数据类型

/* tag with no variants (bits 0-3) */
#define novariant(t)	((t) & 0x0F)//获取低四位 也就是0-3位

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
#define withvariant(t)	((t) & 0x3F)//获取0,5位 数据的基础类型 + 数据的扩展扩展类型
#define ttypetag(o)	withvariant(rawtt(o))

/* type of a TValue */
#define ttype(o)	(novariant(rawtt(o)))//数据的基础类型


/* Macros to test type */
#define checktag(o,t)		(rawtt(o) == (t))//检测类型是否相等
#define checktype(o,t)		(ttype(o) == (t))//检测数据基础类型是否相等


/* Macros for internal tests */

/* collectable object has the same tag as the original value */
///返回obj的tt_是否等于gc里面的tt
#define righttt(obj)		(ttypetag(obj) == gcvalue(obj)->tt)

/*
** Any value being manipulated by the program either is non
** collectable, or the collectable object has the right tag
** and it is not dead. The option 'L == NULL' allows other
** macros using this one to be used where L is not available.
*/

// 检查obj的生存期
// iscollectable(obj)检查obj是否为GC对象
// righttt(obj)返回obj的tt_是否等于gc里面的tt
// isdead(obj)返回obj是否已经被清理
// 总而言之，返回true代表未被GC的和不需要GC的，返回false代表已经被GC了的
#define checkliveness(L,obj) \
	((void)L, lua_longassert(!iscollectable(obj) || \
		(righttt(obj) && (L == NULL || !isdead(G(L),gcvalue(obj))))))


/* Macros to set values */

/* set a value's tag */

// 在lua里面的值是无关类型的，就是靠下面的操作来让变量类型相互变化
// 通过改变TValue的tt_和Value里面的具体值（比如：i,n,f,b等）
// set函数是设置新的值，改变的有tt_
// chg函数也是改变新的值，但是没有改变tt_
// 不需要GC的值复制操作 直接替换Value和tt，需要GC的值复制操作还要检查一下生存期
#define settt_(o,t)	((o)->tt_=(t))


/* main macro to copy values (from 'obj2' to 'obj1') */

///把obj2复制给obj1
#define setobj(L,obj1,obj2) \
	{ TValue *io1=(obj1); const TValue *io2=(obj2); \
          io1->value_ = io2->value_; settt_(io1, io2->tt_); \
	  checkliveness(L,io1); lua_assert(!isnonstrictnil(io1)); }

/*
** Different types of assignments, according to source and destination.
** (They are mostly equal now, but may be different in the future.)
*/

/* from stack to stack */
#define setobjs2s(L,o1,o2)	setobj(L,s2v(o1),s2v(o2))//stack->stack
/* to stack (not from same stack) */
#define setobj2s(L,o1,o2)	setobj(L,s2v(o1),o2)//obj->stack
/* from table to same table */
#define setobjt2t	setobj //table->table
/* to new object */
#define setobj2n	setobj //object->new object
/* to table */
#define setobj2t	setobj //object->table


/*
** Entries in a Lua stack. Field 'tbclist' forms a list of all
** to-be-closed variables active in this stack. Dummy entries are
** used when the distance between two tbc variables does not fit
** in an unsigned short. They are represented by delta==0, and
** their real delta is always the maximum value that fits in
** that field.
*/

/// @brief 数据栈
、、
typedef union StackValue {
  TValue val;
  struct {
    TValuefields;
    unsigned short delta;//当两个tbc变量之间的距离不适合无符号短型时，将使用虚拟条目 它们用delta == 0表示，其实际delta始终是该字段中适合的最大值
  } tbclist;//此堆栈中所有活动的将要关闭的变量的列表
} StackValue;


/* index to stack elements */
//第一个自由索引堆栈中的元素槽
typedef StackValue *StkId;

/* convert a 'StackValue' to a 'TValue' */
//StackValue转Tvalue类型
#define s2v(o)	(&(o)->val)



/*
** {==================================================================
** Nil
** ===================================================================
*/

/* Standard nil */
#define LUA_VNIL	makevariant(LUA_TNIL, 0) //nil类型

/* Empty slot (which might be different from a slot containing nil) */
#define LUA_VEMPTY	makevariant(LUA_TNIL, 1)//空槽位

/* Value returned for a key not found in a table (absent key) */
#define LUA_VABSTKEY	makevariant(LUA_TNIL, 2) //表中没有找到key时候返回的类型


/* macro to test for (any kind of) nil */
#define ttisnil(v)		checktype((v), LUA_TNIL) //基础类型是不是nil类型


/* macro to test for a standard nil */
#define ttisstrictnil(o)	checktag((o), LUA_VNIL)//Tvalue的tt_是不是nil类型


#define setnilvalue(obj) settt_(obj, LUA_VNIL)//设置tt_为nil类型


#define isabstkey(v)		checktag((v), LUA_VABSTKEY) //Tvalue的tt_是不是LUA_VABSTKEY类型


/*
** macro to detect non-standard nils (used only in assertions)
*/

//用来检测基础类型和Tvalue的tt_是不是nil类型
#define isnonstrictnil(v)	(ttisnil(v) && !ttisstrictnil(v))


/*
** By default, entries with any kind of nil are considered empty.
** (In any definition, values associated with absent keys must also
** be accepted as empty.)
*/
//是不是nil类型
#define isempty(v)		ttisnil(v)


/* macro defining a value corresponding to an absent key */
// 散列表查找规则为计算出key的hash，根据hash访问Node数组得到slot所在的位置，
// 然后通过next顺着hash冲突链查找。
// 如果找到就范围对应TValue，找不到返回TValue常量absentkey，值为{NULL, LUA_TNIL}
#define ABSTKEYCONSTANT		{NULL}, LUA_VABSTKEY


/* mark an entry as empty */
// 设置Tvalue的tt_为LUA_VEMPTY类型
#define setempty(v)		settt_(v, LUA_VEMPTY)



/* }================================================================== */


/*
** {==================================================================
** Booleans
** ===================================================================
*/


#define LUA_VFALSE	makevariant(LUA_TBOOLEAN, 0)// 标记 bool false
#define LUA_VTRUE	makevariant(LUA_TBOOLEAN, 1)// 标记 bool true

#define ttisboolean(o)		checktype((o), LUA_TBOOLEAN) //检测TValue->tt_是不是bool
#define ttisfalse(o)		checktag((o), LUA_VFALSE)//检测TValue->tt_是不是false
#define ttistrue(o)		checktag((o), LUA_VTRUE)//检测TValue->tt_是不是true


#define l_isfalse(o)	(ttisfalse(o) || ttisnil(o)) //检测TValue->tt_是不是false 或者nil


#define setbfvalue(obj)		settt_(obj, LUA_VFALSE) //设置TValue->tt_为false
#define setbtvalue(obj)		settt_(obj, LUA_VTRUE)//设置TValue->tt_为true

/* }================================================================== */


/*
** {==================================================================
** Threads
** ===================================================================
*/

#define LUA_VTHREAD		makevariant(LUA_TTHREAD, 0) //线程

#define ttisthread(o)		checktag((o), ctb(LUA_VTHREAD)) //是不是可回收的线程类型

#define thvalue(o)	check_exp(ttisthread(o), gco2th(val_(o).gc)) //如果是可回收的线程类型,那么就吧GCobject转换成线程类型

// 将obj指向的对象 Value 的 union 元素设为(GCObjec *)类型, 并指向 x 指向的对象;
// Tvalue->tt_ 设为 LUA_VTHREAD 类型 并添加可回收属性
#define setthvalue(L,obj,x) \
  { TValue *io = (obj); lua_State *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_VTHREAD)); \
    checkliveness(L,io); }

// 对上面setthvalue宏的封装 唯一区别就是 将StackValue 的o转Tvalue的o类型
#define setthvalue2s(L,o,t)	setthvalue(L,s2v(o),t)

/* }================================================================== */


/*
** {==================================================================
** Collectable Objects
** ===================================================================
*/

/*
** Common Header for all collectable objects (in macro form, to be
** included in other objects)
*/

// 所有需要GC操作的数据都会加一个CommonHeader类型的宏定义
// next指向下一个GC链表的数据
// tt代表数据的类型以及扩展类型以及GC位的标志
// marked是执行GC的标记位，就是一坨二进制，用于具体的GC算法
#define CommonHeader	struct GCObject *next; lu_byte tt; lu_byte marked


/* Common type for all collectable objects */
///lua里面所有需要垃圾回收对象的联合体
typedef struct GCObject {
  CommonHeader;
} GCObject;


/* Bit mark for collectable types */
#define BIT_ISCOLLECTABLE	(1 << 6) //复合类型包含标记位：会被GC标记

#define iscollectable(o)	(rawtt(o) & BIT_ISCOLLECTABLE) // 是否需要进行GC操作

/* mark a tag as collectable */
#define ctb(t)			((t) | BIT_ISCOLLECTABLE) //标记位回收类型

#define gcvalue(o)	check_exp(iscollectable(o), val_(o).gc) //获取 GCObject 指针

#define gcvalueraw(v)	((v).gc) //获取原始gc指针


// 将obj指向的对象 Value 的 union 元素设为(GCObjec *)类型, 并指向 x 指向的对象;
// Tvalue->tt_ 设为i_g->tt类型 并添加可回收属性
#define setgcovalue(L,obj,x) \
  { TValue *io = (obj); GCObject *i_g=(x); \
    val_(io).gc = i_g; settt_(io, ctb(i_g->tt)); }

/* }================================================================== */


/*
** {==================================================================
** Numbers
** ===================================================================
*/

/* Variant tags for numbers */
#define LUA_VNUMINT	makevariant(LUA_TNUMBER, 0)  /* integer numbers *///整数类型
#define LUA_VNUMFLT	makevariant(LUA_TNUMBER, 1)  /* float numbers *///float类型

#define ttisnumber(o)		checktype((o), LUA_TNUMBER)//是不是number类型
#define ttisfloat(o)		checktag((o), LUA_VNUMFLT)//是不是float类型
#define ttisinteger(o)		checktag((o), LUA_VNUMINT)//是不是int类型

#define nvalue(o)	check_exp(ttisnumber(o), \
	(ttisinteger(o) ? cast_num(ivalue(o)) : fltvalue(o)))//获取number的值,如果是int 那么就返回int 如果是float 那么就返回float
#define fltvalue(o)	check_exp(ttisfloat(o), val_(o).n) //获取float
#define ivalue(o)	check_exp(ttisinteger(o), val_(o).i)//获取int

#define fltvalueraw(v)	((v).n) //获取原生float值
#define ivalueraw(v)	((v).i)//获取原生int值

// 将obj指向的对象 Value 的 union 元素设为(lua_Number *)类型, 并指向 x 指向的对象;
// Tvalue->tt_ 设为 LUA_VNUMFLT类型
#define setfltvalue(obj,x) \
  { TValue *io=(obj); val_(io).n=(x); settt_(io, LUA_VNUMFLT); }

// 将obj指向的对象 Value 的 union 元素设为(lua_Number)类型, 并指向 x 指向的对象;
#define chgfltvalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisfloat(io)); val_(io).n=(x); }

// 将obj指向的对象 Value 的 union 元素设为(lua_Integer)类型, 并指向 x 指向的对象;
// Tvalue->tt_ 设为 LUA_VNUMINT类型
#define setivalue(obj,x) \
  { TValue *io=(obj); val_(io).i=(x); settt_(io, LUA_VNUMINT); }

// 将obj指向的对象 Value 的 union 元素设为(lua_Integer)类型, 并指向 x 指向的对象;
#define chgivalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisinteger(io)); val_(io).i=(x); }

/* }================================================================== */


/*
** {==================================================================
** Strings
** ===================================================================
*/

/* Variant tags for strings */
#define LUA_VSHRSTR	makevariant(LUA_TSTRING, 0)  /* short strings *///短字符串
#define LUA_VLNGSTR	makevariant(LUA_TSTRING, 1)  /* long strings *///长字符串

#define ttisstring(o)		checktype((o), LUA_TSTRING)//tt是不是string类型
#define ttisshrstring(o)	checktag((o), ctb(LUA_VSHRSTR))//tt是不是短字符串类型
#define ttislngstring(o)	checktag((o), ctb(LUA_VLNGSTR))//tt是不是长字符串类型

#define tsvalueraw(v)	(gco2ts((v).gc))//获取原生的gc类型

//将GCobject转成字符串
#define tsvalue(o)	check_exp(ttisstring(o), gco2ts(val_(o).gc))


// 将obj指向的对象 Value 的 union 元素设为(GCObjec *)类型, 并指向 x 指向的对象;
// Tvalue->tt_ 设为 x_->tt类型，并添加可回收属性
#define setsvalue(L,obj,x) \
  { TValue *io = (obj); TString *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(x_->tt)); \
    checkliveness(L,io); }

/* set a string to the stack */
// 对上面的封装 唯一区别就是 将StackValue 的o转Tvalue的o类型
#define setsvalue2s(L,o,s)	setsvalue(L,s2v(o),s)

/* set a string to a new object */
// 对setsvalue宏的封装
#define setsvalue2n	setsvalue


/*
** Header for a string value.
*/

/// @brief lua字符串的数据结构
typedef struct TString {
  CommonHeader;//代表需要GC
  lu_byte extra;  /* reserved words for short strings; "has hash" for longs */// 用于标记是否是虚拟机保留的字符串，如果这个值为1，那么不会GC（保留字符串即是lua中的关键字）
  lu_byte shrlen;  /* length for short strings *///shrlen保留字符串的长度(因为lua并不以\0结尾来识别字符串的长度，故需要一个len域来记录其长度)
  unsigned int hash;//字符串的hash值。
  // 短串：该hash值是在创建时就计算出来的
 	// 长串：只有真正需要它的hash值时，才会手动调用luaS_hashlongstr函数生成该值,lua内部现在只有在把长串作为table的key时，才会去计算它。
  union {
    size_t lnglen;  /* length for long strings *///表示长字符的长度
    struct TString *hnext;  /* linked list for hash table *///代表链接下一个字符串
  } u;
  char contents[1];//字符串存储的地方,因为是c语言后面还会在加个'\0' 所以字符串的总大小是 #define sizelstring(l)  (offsetof(TString, contents) + ((l) + 1) * sizeof(char))
} TString;



/*
** Get the actual string (array of bytes) from a 'TString'.
*/
#define getstr(ts)  ((ts)->contents)//获取内容


/* get the actual string (array of bytes) from a Lua value */
#define svalue(o)       getstr(tsvalue(o))//从 Lua 值中获取实际字符串

/* get string length from 'TString *s' */
#define tsslen(s)	((s)->tt == LUA_VSHRSTR ? (s)->shrlen : (s)->u.lnglen) //从TString *s'获取字符串长度

/* get string length from 'TValue *o' */
#define vslen(o)	tsslen(tsvalue(o))//从TValue *o'中获取字符串长度

/* }================================================================== */


/*
** {==================================================================
** Userdata
** ===================================================================
*/


/*
** Light userdata should be a variant of userdata, but for compatibility
** reasons they are also different types.
*/
#define LUA_VLIGHTUSERDATA	makevariant(LUA_TLIGHTUSERDATA, 0) //标记为light userdata

#define LUA_VUSERDATA		makevariant(LUA_TUSERDATA, 0)//标记位full userdata

#define ttislightuserdata(o)	checktag((o), LUA_VLIGHTUSERDATA) //检测 TValue->tt 是不是 light userdata
#define ttisfulluserdata(o)	checktag((o), ctb(LUA_VUSERDATA))//检测 TValue->tt 是不是 full userdata 并且是回收类型

#define pvalue(o)	check_exp(ttislightuserdata(o), val_(o).p) //获取light userdata的指针
#define uvalue(o)	check_exp(ttisfulluserdata(o), gco2u(val_(o).gc))//获取full userdata指针  userdata指针是通过gco2u函数转换过来的

#define pvalueraw(v)	((v).p)//获取原生的light userdata指针

// 将obj指向的对象 Value 的 union 元素设为(void *)类型 void*这里是light userdata类型, 并指向 x 指向的对象;
// Tvalue->tt_ 设为LUA_VLIGHTUSERDATA类型
#define setpvalue(obj,x) \
  { TValue *io=(obj); val_(io).p=(x); settt_(io, LUA_VLIGHTUSERDATA); }


// 将obj指向的对象 Value 的 union 元素设为(GCObjec *)类型 void*这里是light userdata类型, 并指向 x 指向的对象;
// Tvalue->tt_ 设为LUA_VUSERDATA类型 并添加可回收属性
#define setuvalue(L,obj,x) \
  { TValue *io = (obj); Udata *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_VUSERDATA)); \
    checkliveness(L,io); }


/* Ensures that addresses after this type are always fully aligned. */

/// @brief 这里是给Udata可以额外存储一组数据的结构体,就是下面 Udata结构体里面的UValue uv[1]这个玩意的类型
typedef union UValue {
  TValue uv;
  LUAI_MAXALIGN;  /* ensures maximum alignment for udata bytes */
} UValue;


/*
** Header for userdata with user values;
** memory area follows the end of this structure.
*/

/// @brief 使用了Uvalue数组的 userdata 
// 有点像这样的内存布局 | Udata | 内存块... |
typedef struct Udata {
  CommonHeader;
  unsigned short nuvalue;  /* number of user values *///用户自定义值长度
  size_t len;  /* number of bytes *///记录UserData长度
  struct Table *metatable;//UserData数据独立元表
  GCObject *gclist;//GC相关的链表
  UValue uv[1];  /* user values *///用户自定义值内容
} Udata;


/*
** Header for userdata with no user values. These userdata do not need
** to be gray during GC, and therefore do not need a 'gclist' field.
** To simplify, the code always use 'Udata' for both kinds of userdata,
** making sure it never accesses 'gclist' on userdata with no user values.
** This structure here is used only to compute the correct size for
** this representation. (The 'bindata' field in its end ensures correct
** alignment for binary data following this header.)
*/

/// @brief 没有使用Uvalue数组的 userdata
typedef struct Udata0 {
  CommonHeader;
  unsigned short nuvalue;  /* number of user values *////用户自定义值长度
  size_t len;  /* number of bytes *///记录UserData长度
  struct Table *metatable;//UserData数据独立元表
  union {LUAI_MAXALIGN;} bindata;//指向C语言结构体的指针,便于后面直接将这部分内存直接转换成结构体 例如: xxxxx = (struct xxxxx *)lua_newuserdata(L, sizeof(struct xxxxx));
} Udata0;


/* compute the offset of the memory area of a userdata */

///计算用户数据的内存区域的偏移量
// offsetof 是结构成员相对于结构开头的字节偏移量
#define udatamemoffset(nuv) \
	((nuv) == 0 ? offsetof(Udata0, bindata)  \
                    : offsetof(Udata, uv) + (sizeof(UValue) * (nuv)))

/* get the address of the memory block inside 'Udata' */
#define getudatamem(u)	(cast_charp(u) + udatamemoffset((u)->nuvalue)) //返回内存块的地址

/* compute the size of a userdata */
#define sizeudata(nuv,nb)	(udatamemoffset(nuv) + (nb))//计算userdata的总大小

/* }================================================================== */


/*
** {==================================================================
** Prototypes
** ===================================================================
*/

#define LUA_VPROTO	makevariant(LUA_TPROTO, 0)//标记位函数类型


/*
** Description of an upvalue for function prototypes
*/

/// @brief 函数原型的上值描述
typedef struct Upvaldesc {
  TString *name;  /* upvalue name (for debug information) *///上值的名字
  lu_byte instack;  /* whether it is in stack (register) */// 是否是在函数栈中local变量（函数中的local变量存在寄存器中；instack 为0时代表的是闭包中的upvalue, instack 为1时 代表引用的是栈上的寄存器 
  lu_byte idx;  /* index of upvalue (in stack or in outer function's list) *///instack 为0时 如果是闭包中的upvalue idx 是upvalue列表的索引值；instack 为1时 如果是函数栈中的local变量，idx 是寄存器的索引
  lu_byte kind;  /* kind of corresponding variable *///变量类型 VDKREG,VDKREG,RDKCONST,RDKTOCLOSE,RDKCTC这几个类型
} Upvaldesc;

/// @brief 局部变量
typedef struct LocVar {
  TString *varname;//变量名字
  /*局部变量的作用域信息*/
  int startpc;  /* first point where variable is active *///该局部变量存活的第一个指令序号
  int endpc;    /* first point where variable is dead *///该局部变量不存活的第一个指令序号
} LocVar;


/*
** Associates the absolute line source for a given instruction ('pc').
** The array 'lineinfo' gives, for each instruction, the difference in
** lines from the previous instruction. When that difference does not
** fit into a byte, Lua saves the absolute line for that instruction.
** (Lua also saves the absolute line periodically, to speed up the
** computation of a line number: we can use binary search in the
** absolute-line array, but we must traverse the 'lineinfo' array
** linearly to compute a line.)
*/

/// @brief 指令与绝对行的关联
typedef struct AbsLineInfo {
  int pc;//指令索引
  int line;//关联的代码行
} AbsLineInfo;

/*
** Function Prototypes
*/

/// @brief 函数原型
///Proto主要存放二进制指令集Opcode
//为什么定义全局常量变量名前面往往加上一个k 因为constant 的发音[ˈkɑ:nstənt]为了简写就使用了k而不是c

typedef struct Proto {
  CommonHeader;
  lu_byte numparams;  /* number of fixed (named) parameters *///固定参数个数
  lu_byte is_vararg;//是否支持变参:1 表示使用了变参作为最后一个参数  三个点“...”表示这是一个可变参数的函数
  lu_byte maxstacksize;  /* number of registers needed by this function *///该函数需要的栈大小
  int sizeupvalues;  /* size of 'upvalues' *///upvalues数量
  int sizek;  /* size of 'k' *///常量数量
  int sizecode;//指令数量
  int sizelineinfo;//相对行号信息数量
  int sizep;  /* size of 'p' *///子函数数量
  int sizelocvars;//局部变量调试信息大小
  int sizeabslineinfo;  /* size of 'abslineinfo' *///绝对行号abslineinfo长度
  int linedefined;  /* debug information  *///函数定义开始处的行号
  int lastlinedefined;  /* debug information  *///函数定义结束处的行号
  TValue *k;  /* constants used by the function *///常量表
  Instruction *code;  /* opcodes *///存储的指令集数组, 起始位置
  struct Proto **p;  /* functions defined inside the function *///子函数表
  Upvaldesc *upvalues;  /* upvalue information *///是个数组指针
  ls_byte *lineinfo;  /* information about source lines (debug information) *///相对行号信息
  AbsLineInfo *abslineinfo;  /* idem *///绝对行号信息
  LocVar *locvars;  /* information about local variables (debug information) *///本地变量调试信息
  TString  *source;  /* used for debug information *///源代码文件
  GCObject *gclist;//灰对象列表，最后由g->gray串连起来
} Proto;

/* }================================================================== */


/*
** {==================================================================
** Functions
** ===================================================================
*/

#define LUA_VUPVAL	makevariant(LUA_TUPVAL, 0)//上值


/* Variant tags for functions */
#define LUA_VLCL	makevariant(LUA_TFUNCTION, 0)  /* Lua closure *///Lua闭包
#define LUA_VLCF	makevariant(LUA_TFUNCTION, 1)  /* light C function *///C函数指针
#define LUA_VCCL	makevariant(LUA_TFUNCTION, 2)  /* C closure *///C语言闭包

#define ttisfunction(o)		checktype(o, LUA_TFUNCTION) //检测是不是函数
#define ttisLclosure(o)		checktag((o), ctb(LUA_VLCL))//检测是不是lua闭包,并且是回收属性
#define ttislcf(o)		checktag((o), LUA_VLCF)//检测是不是函数指针
#define ttisCclosure(o)		checktag((o), ctb(LUA_VCCL))//检测是不是c闭包,并且是回收属性
#define ttisclosure(o)         (ttisLclosure(o) || ttisCclosure(o))//检测是不是闭包,并且是回收属性


#define isLfunction(o)	ttisLclosure(o)//检测是不是lua闭包,并且是回收属性

#define clvalue(o)	check_exp(ttisclosure(o), gco2cl(val_(o).gc))//GCobject转换成函数
#define clLvalue(o)	check_exp(ttisLclosure(o), gco2lcl(val_(o).gc))//GCobject转换成lua闭包
#define fvalue(o)	check_exp(ttislcf(o), val_(o).f)//获取c函数指针
#define clCvalue(o)	check_exp(ttisCclosure(o), gco2ccl(val_(o).gc))//GCobject转换成c闭包

#define fvalueraw(v)	((v).f)//获取原生的c函数指针

// 将obj指向的对象 Value 的 union 元素设为(GCobject *)类型, 并指向 x 指向的对象;
// Tvalue->tt_ 设为LUA_VLCL类型,并设置回收属性
#define setclLvalue(L,obj,x) \
  { TValue *io = (obj); LClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_VLCL)); \
    checkliveness(L,io); }

// 对上面setclLvalue宏的封装 唯一区别就是 将StackValue 的o转Tvalue的o类型
#define setclLvalue2s(L,o,cl)	setclLvalue(L,s2v(o),cl)

// 将obj指向的对象 Value 的 union 元素设为(lua_CFunction)类型, 并指向 x 指向的对象;
// Tvalue->tt_ 设为LUA_VLCF类型
#define setfvalue(obj,x) \
  { TValue *io=(obj); val_(io).f=(x); settt_(io, LUA_VLCF); }


// 将obj指向的对象 Value 的 union 元素设为(GCobject *)类型, 并指向 x 指向的对象;
// Tvalue->tt_ 设为LUA_VCCL类型,并设置回收属性
#define setclCvalue(L,obj,x) \
  { TValue *io = (obj); CClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_VCCL)); \
    checkliveness(L,io); }


/*
** Upvalues for Lua closures
*/

/// @brief 上值
// 一个UpVal当它所属的那个函数返回之后（调用了return）
// 或者Lua运行堆栈发生改变
// 函数已经不处于合理堆栈下标的时候，
// 该函数所包含的UpVal即会切换到close状态
typedef struct UpVal {
  CommonHeader;
  lu_byte tbc;  /* true if it represents a to-be-closed variable */// to-be-closed类型变量时候他为true
  TValue *v;  /* points to stack or to its own value *///指向堆栈或指向其自身值
  union {
    struct {  /* (when open) */ //open状态时候
      struct UpVal *next;  /* linked list */
      struct UpVal **previous;
    } open;//和lua_State结构体中的UpVal *openupval字段一一对应
    TValue value;  /* the value (when closed) *///close状态时候生效
  } u;
} UpVal;


///nupvalues代表闭包中upvalues数组长度
///gcList代表这个闭包结构体在垃圾清除的时候，要清除包括upvalues在内的一系列可回收对象
#define ClosureHeader \
	CommonHeader; lu_byte nupvalues; GCObject *gclist

/// @brief 使用Lua提供的lua_pushcclosure这个C Api加入到虚拟栈中的C函数闭包，它是对LClosure的一种C模拟
typedef struct CClosure {
  ClosureHeader;
  lua_CFunction f;//c函数指针
  TValue upvalue[1];  /* list of upvalues *///函数的upvalue指针列表，记录了该函数引用的所有upvals。
  // 正是由于该字段的存在，导致函数对upvalue的访问要快于从全局表_G中向下查找。函数对upvalue的访问，
  // 一般就2个步骤：(1)从closure的upvals数组中按索引号取出upvalue。(2)将upvalue加到luastate的stack中
} CClosure;

/// @brief lua闭包
typedef struct LClosure {
  ClosureHeader;//跟GC相关的结构，因为函数与是参与GC的
  struct Proto *p;//因为Closure=函数+upvalue，所以p封装的就是纯粹的函数原型
  UpVal *upvals[1];  /* list of upvalues *///函数的upvalue指针列表，记录了该函数引用的所有upvals。
  // 正是由于该字段的存在，导致函数对upvalue的访问要快于从全局表_G中向下查找。函数对upvalue的访问，
  // 一般就2个步骤：(1)从closure的upvals数组中按索引号取出upvalue。(2)将upvalue加到luastate的stack中
} LClosure;

/// @brief 闭包,注意这里是个联合体
typedef union Closure {
  CClosure c;//c闭包
  LClosure l;//lua闭包
} Closure;


#define getproto(o)	(clLvalue(o)->p)//获取函数原型指针

/* }================================================================== */


/*
** {==================================================================
** Tables
** ===================================================================
*/

#define LUA_VTABLE	makevariant(LUA_TTABLE, 0)//表

#define ttistable(o)		checktag((o), ctb(LUA_VTABLE))//是不是表


#define hvalue(o)	check_exp(ttistable(o), gco2t(val_(o).gc))//GCobject转换成表

// 将obj指向的对象 Value 的 union 元素设为(GCobject *)类型, 并指向 x 指向的对象;
// Tvalue->tt_ 设为LUA_VTABLE类型,并设置回收属性
#define sethvalue(L,obj,x) \
  { TValue *io = (obj); Table *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_VTABLE)); \
    checkliveness(L,io); }

// 对上面sethvalue宏的封装 唯一区别就是 将StackValue 的o转Tvalue的o类型
#define sethvalue2s(L,o,h)	sethvalue(L,s2v(o),h)


/*
** Nodes for Hash tables: A pack of two TValue's (key-value pairs)
** plus a 'next' field to link colliding entries. The distribution
** of the key's fields ('key_tt' and 'key_val') not forming a proper
** 'TValue' allows for a smaller size for 'Node' both in 4-byte
** and 8-byte alignments.
*/

/// @brief Table的元素类型
/// 假如是这样的lua代码
// local t = {}
// t["xxx"] = 2222;
// 那么 上面例子中NodeKey的部分位"xxx"
// key_tt是短字符串类型
// key_val是"xxx"
// i_val里面的tt_为整型
// i_val里面的value_为2222
typedef union Node {
  /// @brief key的部分
  struct NodeKey {
    TValuefields;  /* fields for value *///Value 联合体 + 类型标签
    lu_byte key_tt;  /* key type *///代表key的类型标记
    int next;  /* for chaining *///用于哈希冲突的时候链接向下一个位置
    Value key_val;  /* key value *///代表key的具体数值
  } u;
  TValue i_val;  /* direct access to node's value as a proper 'TValue' *///TValue存储了数据的类型与数据值，就是上面的i_val里面的tt_为整型,i_val里面的value_为2222 其实这里的数据和TValuefields里面的数据一样的，这么写只是为了提供一个快捷访问
} Node;


/* copy a value into a key */
///将值复制到key中
#define setnodekey(L,node,obj) \
	{ Node *n_=(node); const TValue *io_=(obj); \
	  n_->u.key_val = io_->value_; n_->u.key_tt = io_->tt_; \
	  checkliveness(L,io_); }


/* copy a value from a key */
//从node中的key复制值到obj
#define getnodekey(L,obj,node) \
	{ TValue *io_=(obj); const Node *n_=(node); \
	  io_->value_ = n_->u.key_val; io_->tt_ = n_->u.key_tt; \
	  checkliveness(L,io_); }


/*
** About 'alimit': if 'isrealasize(t)' is true, then 'alimit' is the
** real size of 'array'. Otherwise, the real size of 'array' is the
** smallest power of two not smaller than 'alimit' (or zero iff 'alimit'
** is zero); 'alimit' is then used as a hint for #t.
*/

#define BITRAS		(1 << 7)//第8位为1
#define isrealasize(t)		(!((t)->flags & BITRAS))//根据flags的第8位判断alimit是否为数组部分的实际大小
#define setrealasize(t)		((t)->flags &= cast_byte(~BITRAS))//设置flags的第8为0 
#define setnorealasize(t)	((t)->flags |= BITRAS)//根据flags的第8为判断alimit是否不为数组部分的实际大小

/// @brief 按照key的数据类型分成数组部分和散列表部分，
// 数组部分用于存储key值在数组大小范围内的键值对，其余数组部分不能存储的键值对则存储在散列表部分
typedef struct Table {
  CommonHeader;
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present *///第8位为0，则表示alimit为数组的实际大小，否则需重新计算 
  lu_byte lsizenode;  /* log2 of size of 'node' array *///  哈希部分的长度对数 注意不是实际大小 大小总是是2的整数次方    1 << lsizenode才能得到实际的size
  unsigned int alimit;  /* "limit" of 'array' array *///在大部份情况下为数组的容量（2次幂数）
  TValue *array;  /* array part *///指向数组部分的首地址
  Node *node;//指向node数据块（即散列部分）首地址 哈希表存储在这
  Node *lastfree;  /* any free position is before this position *///记录上一次从node数据块（即散列部分）末尾分配空闲Node的位置
  struct Table *metatable;//存放该表的元表
  GCObject *gclist;//GC相关的 
} Table;


/*
** Macros to manipulate keys inserted in nodes
*/
#define keytt(node)		((node)->u.key_tt)//获取key的类型
#define keyval(node)		((node)->u.key_val)//获取key的值

#define keyisnil(node)		(keytt(node) == LUA_TNIL)//key的类型是不是空
#define keyisinteger(node)	(keytt(node) == LUA_VNUMINT)//key的类型是不是整数
#define keyival(node)		(keyval(node).i)//key的值是不是int类型
#define keyisshrstr(node)	(keytt(node) == ctb(LUA_VSHRSTR))//key的类型是不是可回收的短字符串型
#define keystrval(node)		(gco2ts(keyval(node).gc))//GCobject转换成字符串

#define setnilkey(node)		(keytt(node) = LUA_TNIL)//key的类型设置成nil类型

#define keyiscollectable(n)	(keytt(n) & BIT_ISCOLLECTABLE)//key的类型是不是可回收类型

#define gckey(n)	(keyval(n).gc)//获取key值的gc指针
#define gckeyN(n)	(keyiscollectable(n) ? gckey(n) : NULL)//key的类型如果是可回收类型,那么就返回key值的gc指针,否则返回NULL


/*
** Dead keys in tables have the tag DEADKEY but keep their original
** gcvalue. This distinguishes them from regular keys but allows them to
** be found when searched in a special way. ('next' needs that to find
** keys removed from a table during a traversal.)
*/
#define setdeadkey(node)	(keytt(node) = LUA_TDEADKEY)//设置key的类型为deadkey类型
#define keyisdead(node)		(keytt(node) == LUA_TDEADKEY)//key的类型是否为deadkey类型

/* }================================================================== */



/*
** 'module' operation for hashing (size is always a power of 2)
*/
//(size&(size-1))==0 这个是用来判断size是否是2的正整数幂或者0
//比如size位16=10000 size-1=1111
// 那么：
// 10000
// &1111
// ----------
//     0

// (cast_int((s) & ((size)-1))) 看着像保留size-1的位数的值 比如上面size-1是4个1,那么就是保留低的hash值
//总的结合起来看其实就是 s % size
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast_int((s) & ((size)-1)))))


#define twoto(x)	(1<<(x)) //hash表实际的size
// t 为表，返回 Table 的 node 数组大小 等价于求hash表实际的size
#define sizenode(t)	(twoto((t)->lsizenode))


/* size of buffer for 'luaO_utf8esc' function */
#define UTF8BUFFSZ	8

LUAI_FUNC int luaO_utf8esc (char *buff, unsigned long x);
LUAI_FUNC int luaO_ceillog2 (unsigned int x);
LUAI_FUNC int luaO_rawarith (lua_State *L, int op, const TValue *p1,
                             const TValue *p2, TValue *res);
LUAI_FUNC void luaO_arith (lua_State *L, int op, const TValue *p1,
                           const TValue *p2, StkId res);
LUAI_FUNC size_t luaO_str2num (const char *s, TValue *o);
LUAI_FUNC int luaO_hexavalue (int c);
LUAI_FUNC void luaO_tostring (lua_State *L, TValue *obj);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t srclen);


#endif

