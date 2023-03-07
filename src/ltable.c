/*
 * @文件作用: 表类型的相关操作。Lua表（哈希） 
 * @功能分类: 虚拟机运转的核心功能
 * @注释者: frog-game
 * @LastEditTime: 2023-03-07 14:40:35
 */
/*
** $Id: ltable.c $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#define ltable_c
#define LUA_CORE

#include "lprefix.h"


/*
** Implementation of tables (aka arrays, objects, or hash tables).
** Tables keep its elements in two parts: an array part and a hash part.
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest 'n' such that
** more than half the slots between 1 and n are in use.
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the 'original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
*/

#include <math.h>
#include <limits.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"


/*
** MAXABITS is the largest integer such that MAXASIZE fits in an
** unsigned int.
*/

////MAXABITS是数组size的以2为底的对数
#define MAXABITS	cast_int(sizeof(int) * CHAR_BIT - 1) //31


/*
** MAXASIZE is the maximum size of the array part. It is the minimum
** between 2^MAXABITS and the maximum size that, measured in bytes,
** fits in a 'size_t'.
*/

//数组部分最大size
#define MAXASIZE	luaM_limitN(1u << MAXABITS, TValue)

/*
** MAXHBITS is the largest integer such that 2^MAXHBITS fits in a
** signed int.
*/


// 哈希部分的最大size是2^MAXHBITS. MAXHBITS是最大的整数，2^MAXHBITS是对于signed int而言的
// table里面最大的元素数量是 2^MAXABITS + 2^MAXHBITS
// 类型是unsigned int
#define MAXHBITS	(MAXABITS - 1)


/*
** MAXHSIZE is the maximum size of the hash part. It is the minimum
** between 2^MAXHBITS and the maximum size such that, measured in bytes,
** it fits in a 'size_t'.
*/

//hash部分最大size
#define MAXHSIZE	luaM_limitN(1u << MAXHBITS, Node)


/*
** When the original hash value is good, hashing by a power of 2
** avoids the cost of '%'.
*/

//根据n的值，对其t进行lmod操作
#define hashpow2(t,n)		(gnode(t, lmod((n), sizenode(t))))

/*
** for other types, it is better to avoid modulo by power of 2, as
** they can have many 2 factors.
*/

//对lsizenode2次幂减1取模（最后按位或1，是为了保持要用来取模的数字((sizenode(t)-1)|1)这段保证不为0，是大于等于1的数）
//返回值是 0 ~ (size - 1) 下标下的hash值
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1)|1))))

//字符串hash
#define hashstr(t,str)		hashpow2(t, (str)->hash)
//bool值hash
#define hashboolean(t,p)	hashpow2(t, p)

//数据的地址来求hash
#define hashpointer(t,p)	hashmod(t, point2uint(p))


// 宏定义一个虚拟节点，用于空哈希部分的元素
#define dummynode		(&dummynode_)

static const Node dummynode_ = {
  {{NULL}, LUA_VEMPTY,  /* value's value and type */
   LUA_VNIL, 0, {NULL}}  /* key type, next, and key value */
};

//通过next顺着hash冲突链查找。如果找到就范围对应TValue，找不到返回TValue常量absentkey
static const TValue absentkey = {ABSTKEYCONSTANT};


/*
** Hash for integers. To allow a good hash, use the remainder operator
** ('%'). If integer fits as a non-negative int, compute an int
** remainder, which is faster. Otherwise, use an unsigned-integer
** remainder, which uses all bits and ensures a non-negative result.
*/

/// @brief 返回对应整数值对散列表大小取余的key对应的节点 
/// 为什么hashmod(t, cast_int(ui))地方要使用cast_int(ui)强制,主要原因是lua作者认为32位的除法运算比64位的除法运算要快,原话如下
/* Roberto Ierusalimschy   星期一, 23 一月 07:20

  ...
  So the reason for writing this is because signed is faster than
  unsigned ~~

  Not as much as 32-bit division is faster than 64-bit division. ///重点这句话

-- Roberto */
// 这是lua mail list上原话地址:http://lua-users.org/lists/lua-l/2023-01/msg00100.html
/// @param t 
/// @param i key
/// @return 
static Node *hashint (const Table *t, lua_Integer i) {
  lua_Unsigned ui = l_castS2U(i);//如果要对负数计算哈希值的话，先转成正数方便计算
  if (ui <= (unsigned int)INT_MAX)//没有超过无符号int最大值 (unsigned int)INT_MAX = 2147483647
    return hashmod(t, cast_int(ui));
  else
    return hashmod(t, ui);
}


/*
** Hash for floating-point numbers.
** The main computation should be just
**     n = frexp(n, &i); return (n * INT_MAX) + i
** but there are some numerical subtleties.
** In a two-complement representation, INT_MAX does not has an exact
** representation as a float, but INT_MIN does; because the absolute
** value of 'frexp' is smaller than 1 (unless 'n' is inf/NaN), the
** absolute value of the product 'frexp * -INT_MIN' is smaller or equal
** to INT_MAX. Next, the use of 'unsigned int' avoids overflows when
** adding 'i'; the use of '~u' (instead of '-u') avoids problems with
** INT_MIN.
*/
#if !defined(l_hashfloat)
/// @brief 浮点型数据的哈希算法
// 在一个两个组合的表示中，INT_MAX没有一个精确的浮点数表示。但是INT_MAX有
// 因为frexp的绝对值是一个比一小的数字（除非n是无穷大或无效数字）
// 'frexp * -INT_MIN'的绝对值小于或等于INT_MAX。通过使用unsigned int可以避免加i时溢出，通过使用~u可以避免INT_MIN的问题

// l_mathop:将返回值强转为lua_Number
// rexp()用来把一个数分解为尾数和指数，其原型为：
// double frexp(double x, int *exp);
// 【参数】x 为待分解的浮点数，exp 为存储指数的指针。
// 设返回值为 ret，则 x = ret * 2 exp，其中 exp 为整数，ret 的绝对值在 0.5（含） 到 1（不含） 之间。
// 如果 x = 0，则 ret = exp = 0
// 【返回值】将尾数 ret 返回。

// frexp是一个C语言函数，功能是把一个浮点数分解为尾数和指数
// 第一个参数是要分解的浮点数据，第二个参数是存储指数的指针
// 返回的是尾数
// 然后拿着返回的尾数来乘以-INT_MIN,结果用n保存
// 下面判断n能不能转换为int,不能的话就报错
// 可以的话就用unsigned int类型的u来保存ni加上之前的指数
// 如果u超出了INT_MAX就返回~u,不然直接u
/// @param n 
/// @return 
static int l_hashfloat (lua_Number n) {
  int i;
  lua_Integer ni;
  n = l_mathop(frexp)(n, &i) * -cast_num(INT_MIN);
  if (!lua_numbertointeger(n, &ni)) {  /* is 'n' inf/-inf/NaN? *///不能转换成整型
    lua_assert(luai_numisnan(n) || l_mathop(fabs)(n) == cast_num(HUGE_VAL));
    return 0;
  }
  else {  /* normal case */
    unsigned int u = cast_uint(i) + cast_uint(ni);
    return cast_int(u <= cast_uint(INT_MAX) ? u : ~u);
  }
}
#endif


/*
** returns the 'main' position of an element in a table (that is,
** the index of its hash value).
*/

/// @brief mainposition函数通过key找到主位置的node
/// @param t 
/// @param key 
/// @return 
static Node *mainpositionTV (const Table *t, const TValue *key) {
  switch (ttypetag(key)) {
    case LUA_VNUMINT: {//key为整数类型
      lua_Integer i = ivalue(key);
      return hashint(t, i);//返回对应整数值对散列表大小取余的key对应的节点
    }
    case LUA_VNUMFLT: {//key为浮点类型
      lua_Number n = fltvalue(key);
      return hashmod(t, l_hashfloat(n));//返回值是 0 ~ (size - 1) 下标下的hash node
    }
    case LUA_VSHRSTR: {//key为短字符串类型
      TString *ts = tsvalue(key);
      return hashstr(t, ts);//字符串对应的hash
    }
    case LUA_VLNGSTR: {//key为长字符串类型
      TString *ts = tsvalue(key);
      return hashpow2(t, luaS_hashlongstr(ts));//如果长串没有计算过hash，则调用luaS_hashlongstr来计算，然后再使用hash & (2^t->lsizenode - 1)来获取hash node
    }
    case LUA_VFALSE://bool false
      return hashboolean(t, 0);
    case LUA_VTRUE://bool true
      return hashboolean(t, 1);
    case LUA_VLIGHTUSERDATA: {// 指针类型（不需要GC）
      void *p = pvalue(key);//获取对应的指针
      return hashpointer(t, p);//获取p指针指向的地址值对表t的散列表Node大小的余,对应的节点
    }
    case LUA_VLCF: {// key为c函数类型
      lua_CFunction f = fvalue(key);//获取轻量C函数 
      return hashpointer(t, f);// 获取f函数指针指向的地址值对表t的散列表Node大小的余,对应的节点 
    }
    default: {
      GCObject *o = gcvalue(key);//默认情况获取key作为GC对象
      return hashpointer(t, o);//获取GC对象o指针指向的地址值对表t的散列表Node大小的余,对应的节点
    }
  }
}

/// @brief 函数通过key找到主位置
/// @param t 
/// @param nd 
/// @return 
l_sinline Node *mainpositionfromnode (const Table *t, Node *nd) {
  TValue key;
  getnodekey(cast(lua_State *, NULL), &key, nd);
  return mainpositionTV(t, &key);
}


/*
** Check whether key 'k1' is equal to the key in node 'n2'. This
** equality is raw, so there are no metamethods. Floats with integer
** values have been normalized, so integers cannot be equal to
** floats. It is assumed that 'eqshrstr' is simply pointer equality, so
** that short strings are handled in the default case.
** A true 'deadok' means to accept dead keys as equal to their original
** values. All dead keys are compared in the default case, by pointer
** identity. (Only collectable objects can produce dead keys.) Note that
** dead long strings are also compared by identity.
** Once a key is dead, its corresponding value may be collected, and
** then another value can be created with the same address. If this
** other value is given to 'next', 'equalkey' will signal a false
** positive. In a regular traversal, this situation should never happen,
** as all keys given to 'next' came from the table itself, and therefore
** could not have been collected. Outside a regular traversal, we
** have garbage in, garbage out. What is relevant is that this false
** positive does not break anything.  (In particular, 'next' will return
** some other valid item on the table or nil.)
*/

/// @brief 根据n2不同类型，判断k1和n2是否相等
/// @param k1 是否为GC对象
/// @param n2 
/// @param deadok 用来标识是否需要检查n2是否被释放
/// @return 
static int equalkey (const TValue *k1, const Node *n2, int deadok) {
  if ((rawtt(k1) != keytt(n2)) &&  /* not the same variants? *///不是相同类型，包含易变位 
       !(deadok && keyisdead(n2) && iscollectable(k1)))//是否需要检查n2是否被释放，k1是否为GC对象 
   return 0;  /* cannot be same key */
  switch (keytt(n2)) {
    case LUA_VNIL: case LUA_VFALSE: case LUA_VTRUE://是nil,false,true
      return 1;
    case LUA_VNUMINT://整数类型
      return (ivalue(k1) == keyival(n2));//值部分相等
    case LUA_VNUMFLT://float类型
      return luai_numeq(fltvalue(k1), fltvalueraw(keyval(n2)));
    case LUA_VLIGHTUSERDATA://light userdata
      return pvalue(k1) == pvalueraw(keyval(n2));
    case LUA_VLCF://轻量C函数
      return fvalue(k1) == fvalueraw(keyval(n2));
    case ctb(LUA_VLNGSTR)://是长字符串,并且标记回收类型
      return luaS_eqlngstr(tsvalue(k1), keystrval(n2));
    default:
      return gcvalue(k1) == gcvalueraw(keyval(n2));
  }
}


/*
** True if value of 'alimit' is equal to the real size of the array
** part of table 't'. (Otherwise, the array part must be larger than
** 'alimit'.)
*/

// 如果alimit的值等于数组表t部分的实际大小，则为 true。否则，数组部分必须大于'alimit'
#define limitequalsasize(t)	(isrealasize(t) || ispow2((t)->alimit))


/*
** Returns the real size of the 'array' array
*/

/// @brief 通过一系列的位运算，计算出大于且最接近alimit的2的n次方长度
/// @param t 
/// @return 计算出大于且最接近alimit的2的n次方长度
LUAI_FUNC unsigned int luaH_realasize (const Table *t) {
  if (limitequalsasize(t))
    return t->alimit;  /* this is the size */
  else {
    unsigned int size = t->alimit;//假设size是10
    /* compute the smallest power of 2 not smaller than 'n' */
    //计算不小于n的最小2的指数倍
    size |= (size >> 1);//10 |(10 >> 1) ---> 1010 | （1010>> 1）   ---> 1010 | （0101）  ---> 1111   size = 15
    size |= (size >> 2);//1111|(1111 >> 2)---> 1111 | （11）---> 1111 size = 15
    size |= (size >> 4);//1111|(1111 >> 4)---> 1111 | （0）---> 1111 size = 15
    size |= (size >> 8);//1111|(1111 >> 8)---> 1111 | （0）---> 1111 size = 15
    size |= (size >> 16);//1111|(1111 >> 16)---> 1111 | （0）---> 1111 size = 15
#if (UINT_MAX >> 30) > 3//右移30位如果大于3 举例11111111111111111111111111111111 加入右移30剩下2位11如果2位11前面还有1那么肯定大于3
    size |= (size >> 32);  /* unsigned int has more than 32 bits *///无符号 int 超过 32 位 要取32位以上的指数
#endif
    size++;//16
    lua_assert(ispow2(size) && size/2 < t->alimit && t->alimit < size);
    return size;
  }
}


/*
** Check whether real size of the array is a power of 2.
** (If it is not, 'alimit' cannot be changed to any other value
** without changing the real size.)
*/

/// @brief 检查数组的实际大小是否为 2 的幂。如果不是，则在不更改实际大小的情况下，无法将alimit更改为任何其他值
/// @param t 
/// @return 
static int ispow2realasize (const Table *t) {
  return (!isrealasize(t) || ispow2(t->alimit));
}

/// @brief 设置alimit为表数组部分的实际大小，并设置对应的flags，返回表数组部分的实际大小
/// @param t 
/// @return 
static unsigned int setlimittosize (Table *t) {
  t->alimit = luaH_realasize(t);
  setrealasize(t);
  return t->alimit;
}

// 获取表的数组部分的大小
#define limitasasize(t)	check_exp(isrealasize(t), t->alimit)



/*
** "Generic" get version. (Not that generic: not valid for integers,
** which may be in array part, nor for floats with integral values.)
** See explanation about 'deadok' in function 'equalkey'.
*/

/// @brief 从表t的散列表部分查找键为key的值是否存在，存在则返回
/// @param t 
/// @param key 
/// @param deadok 检查搜到的点是否被释放
/// @return 
static const TValue *getgeneric (Table *t, const TValue *key, int deadok) {
  Node *n = mainpositionTV(t, key);//初始的n就是主位置结点
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    if (equalkey(key, n, deadok))//检查n的key值和传入的key是否相等 */
      return gval(n);  /* that's it */
    else {//散列值冲突的解决 
      int nx = gnext(n);//否则取链接的下一个结点的偏移
      if (nx == 0)//无偏移，说明没有下一个结点，直接返回nil对象
        return &absentkey;  /* not found */
      n += nx;//取下一个结点给n
    }
  }
}


/*
** returns the index for 'k' if 'k' is an appropriate key to live in
** the array part of a table, 0 otherwise.
*/

/// @brief 得到数组的索引
/// @param k 
/// @return 
static unsigned int arrayindex (lua_Integer k) {
  if (l_castS2U(k) - 1u < MAXASIZE)  /* 'k' in [1, MAXASIZE]? */
    return cast_uint(k);  /* 'key' is an appropriate array index */
  else
    return 0;
}


/*
** returns the index of a 'key' for table traversals. First goes all
** elements in the array part, then elements in the hash part. The
** beginning of a traversal is signaled by 0.
*/

/// @brief /1~alimit在数组   >alimit在hash
/// @param L 
/// @param t 
/// @param key 
/// @param asize 
/// @return 
static unsigned int findindex (lua_State *L, Table *t, TValue *key,
                               unsigned int asize) {
  unsigned int i;
  if (ttisnil(key)) return 0;  /* first iteration */
  i = ttisinteger(key) ? arrayindex(ivalue(key)) : 0;
  if (i - 1u < asize)  /* is 'key' inside array part? *///在数组部分
    return i;  /* yes; that's the index */
  else {
    const TValue *n = getgeneric(t, key, 1);//找到hash中的位置
    if (l_unlikely(isabstkey(n)))
      luaG_runerror(L, "invalid key to 'next'");  /* key not found */
    i = cast_int(nodefromval(n) - gnode(t, 0));  /* key index in hash table *///哈希表中的键索引
    /* hash elements are numbered after array ones */
    return (i + 1) + asize;
  }
}

/// @brief 根据key找到下一个key，迭代器的实现是用key去遍历的
// 对lua表进行迭代访问，每次访问的时候 ，会调用luaH_next
/// @param L 
/// @param t 
/// @param key 
/// @return 
int luaH_next (lua_State *L, Table *t, StkId key) {
  unsigned int asize = luaH_realasize(t);//得到数组真实长度
  unsigned int i = findindex(L, t, s2v(key), asize);  /* find original key *///得到索引
  for (; i < asize; i++) {  /* try first array part *///在数组中查找
    if (!isempty(&t->array[i])) {  /* a non-empty entry? *///如果不是nil
      setivalue(s2v(key), i + 1);
      setobj2s(L, key + 1, &t->array[i]);
      return 1;
    }
  }
  for (i -= asize; cast_int(i) < sizenode(t); i++) {  /* hash part *///hash部分
    if (!isempty(gval(gnode(t, i)))) {  /* a non-empty entry? *///不是nil
      Node *n = gnode(t, i);//获取node
      getnodekey(L, s2v(key), n);
      setobj2s(L, key + 1, gval(n));
      return 1;
    }
  }
  return 0;  /* no more elements */
}

/// @brief 释放hash
/// @param L 
/// @param t 
static void freehash (lua_State *L, Table *t) {
  if (!isdummy(t))//hash不是空
    luaM_freearray(L, t->node, cast_sizet(sizenode(t)));//释放hash
}


/*
** {=============================================================
** Rehash
** ==============================================================
*/

/*
** Compute the optimal size for the array part of table 't'. 'nums' is a
** "count array" where 'nums[i]' is the number of integers in the table
** between 2^(i - 1) + 1 and 2^i. 'pna' enters with the total number of
** integer keys in the table and leaves with the number of keys that
** will go to the array part; return the optimal size.  (The condition
** 'twotoi > 0' in the for loop stops the loop if 'twotoi' overflows.)
*/

/// @brief 计算应该分配给数组部分的空间大小
// 只有利用率超过50%的数组元素进入数组,否则进去hash
// 首先，初始值，twotoi为1，i为0，a在循环初始时为0，它表示的是循环到目前为止数据小于2^i的数据数量。
// 假设
//     nums[0] = 1（1落在此区间）
//     nums[1] = 1（2落在此区间）
//     nums[2] = 1（3落在此区间）
//     nums[3] = 0
//     nums[4] = 0
//     nums[5] = 1（20落在此区间）
//     nums[6] = 0
//     ...
//     nums[n] = 0（其中n > 5 且 n <= MAXBITS）

//     pna = 4
//   遍历第一个切片时  i = 0，twotoi = 1，满足（twotoi > 0 && *pna > twotoi / 2），nums[i] = 1 > 0成立，a += nums[i], a = 1，满足a > twotoi/2，也就是满足这个范围内数组利用率大于50%的原则，此时记录下这个范围，也就是 optimal = twotoi = 1，到目前为止的数据数量 na = a = 1
//   遍历第二个切片时  i = 1，twotoi = 2，满足（twotoi > 0 && *pna > twotoi / 2），nums[i] = 1 > 0成立，a += nums[i], a = 2，满足a > twotoi/2，记录下这个范围，也就是 optimal = twotoi = 2，到目前为止的数据数量 na = a = 2
//   遍历第三个切片时  i = 2，twotoi = 4，满足（twotoi > 0 && *pna > twotoi / 2），nums[i] = 1 > 0成立，a += nums[i], a = 3，此时满足a > twotoi/2，记录下这个范围，也就是 optimal = twotoi = 4，到目前为止的数据数量 na = a = 3
//   遍历第四个切片时  i = 3，twotoi = 8，不满足（twotoi > 0 && *pna > twotoi / 2），结束，返回 optimal 为4。
/// @param nums 这些整数key的分布情况
/// @param pna  就是已经使用的数组部分元素个数+hash部分元素个数
/// @return 
static unsigned int computesizes (unsigned int nums[], unsigned int *pna) {
  int i;//扩大的次数,对应numusearry中的lg
  unsigned int twotoi;  /* 2^i (candidate for optimal size) *///第i次扩大后数组的长度,对应numusearray中的ttlg
  unsigned int a = 0;  /* number of elements smaller than 2^i *////累计计数，记录第i次扩大后，能够被数组装下的有效整数key的总数量
  unsigned int na = 0;  /* number of elements to go to array part *///累计计数，记录实际被放到数组部分内的有效整数key的总数量（value不为空）
  unsigned int optimal = 0;  /* optimal size for array part *///数组部分最优长度
  /* loop while keys can fill more than half of total size *///一直循环,直到键可以填充总大小的一半以上
  for (i = 0, twotoi = 1;
       twotoi > 0 && *pna > twotoi / 2;
       i++, twotoi *= 2) {
    a += nums[i];//加上当前数量
    if (a > twotoi/2) {  /* more than half elements present? *///经过本次扩大之后，如果放到数组中的整数key超过数组容量的一半，认为此时数组部分的利用率是可以接受的
      optimal = twotoi;  /* optimal size (till now) *///此时更新应该分配给数组的长度，
      na = a;  /* all elements up to 'optimal' will go to array part *///并记录实际放到数组部分的数量
    }
  }
  lua_assert((optimal == 0 || optimal / 2 < na) && na <= optimal);
  *pna = na;//pna中记录了最终被放到数组部分key的数量
  return optimal;//返回数组部分最终的长度
}

/// @brief key若落在[1,INT32]，则nums对应的域++ ,返回1反之返回0
/// @param key 
/// @param nums 数组是哈希列表的分片统计数组
/// @return 
static int countint (lua_Integer key, unsigned int *nums) {
  unsigned int k = arrayindex(key);//判断key是不是数值型且数值
  if (k != 0) {  /* is 'key' an appropriate array index? */
    nums[luaO_ceillog2(k)]++;  /* count as such *///如果K落在这个分片则加1 
    return 1;
  }
  else
    return 0;
}


/*
** Count keys in array part of table 't': Fill 'nums[i]' with
** number of keys that will go into corresponding slice and return
** total number of non-nil keys.
*/

/// @brief a:统计数组部分总数  b:数组中key在(2^0,2^1],(2^1,2^2],(2^2,2^3],(2^3,2^4]...等区间内的元素个数
/// @param t 
/// @param nums 
/// @return 
static unsigned int numusearray (const Table *t, unsigned int *nums) {
  int lg;//数组默认是空,初始增加1个,之后每次按照2倍的速度扩大,lg用于标识是第几次扩大
  unsigned int ttlg;  /* 2^lg *///数值上等于2^lg,也即低lg次扩大时候,数组对应的长度
  unsigned int ause = 0;  /* summation of 'nums' *///累计计数,数组内元素的总数量
  unsigned int i = 1;  /* count to traverse all array keys *///累计计数,记录对数组遍历到的位置,因而在数组扩大以后,即代表本次扩大部分的起始位置
  unsigned int asize = limitasasize(t);  /* real array size *///真实数组的长度,2的n次方
  /* traverse each slice */
  //按照2倍的速度扩大,每扩大一次,进行一轮统计
  //将本轮扩大部分内的非空元素数量记录到nums数组对应位置中
  //将本轮扩大部分内的非空元素数量累加到总计数中
  for (lg = 0, ttlg = 1; lg <= MAXABITS; lg++, ttlg *= 2) {
    unsigned int lc = 0;  /* counter *///每次2倍扩大时清空,用于统计本轮扩大新增部分所包含的非空元素数量
    unsigned int lim = ttlg;//本轮扩大的上限(由于变量i在循环外定义,每次进行新一轮循环时候,i即为新扩大部分的下限)
    if (lim > asize) {//统计到超过数组部分的真实大小时候,停止,后面就没必要在统计了,
      lim = asize;  /* adjust upper limit */
      if (i > lim)
        break;  /* no more elements to count */
    }
    /* count elements in range (2^(lg - 1), 2^lg] */
    for (; i <= lim; i++) {//统计本轮扩大新增部分内非空元素的数量,并累加到lc中,本轮扩大新增部分的下标在数值上等于 (2^(lg - 1), 2^lg] 
      if (!isempty(&t->array[i-1]))
        lc++;
    }
    nums[lg] += lc;//将本轮扩大新增部分内非空元素的数量记录到nums中
    ause += lc;//数量累加到总的计数中
  }
  return ause;//经过多轮的2倍扩大,最终统计出数组部分内包含的非空元素的数量（也就是有效整数key的数量）
}

/// @brief 统计散列表部分内包含的整数key数量和分布情况，并得到总共key的数量
/// @param t 
/// @param nums 收集[1,uint32Max]的key到对应的区间
/// @param pna 累加[1,uint32Max]的key的总数
/// @return 所有类型的key的总数
static int numusehash (const Table *t, unsigned int *nums, unsigned int *pna) {
  int totaluse = 0;  /* total number of elements *///元素总数
  int ause = 0;  /* elements added to 'nums' (can go to array part) *///整数key的计数
  int i = sizenode(t);
  while (i--) {
    Node *n = &t->node[i];
    if (!isempty(gval(n))) {//遍历所有节点，对于整数key，通过countint函数检查，如果是有效的整数key，则统计到nums数组对应的位置，并累加到totaluse变量
      if (keyisinteger(n))
        ause += countint(keyival(n), nums);
      totaluse++;//所有非空值都会索计到totaluse
    }
  }
  *pna += ause;//记录hash表部分的有效整数锥数量
  return totaluse;//返回的是hash中所有键总数量
}


/*
** Creates an array for the hash part of a table with the given
** size, or reuses the dummy node if size is zero.
** The computation for size overflow is in two steps: the first
** comparison ensures that the shift in the second one does not
** overflow.
*/

/// @brief 创建新的Node数组,lastfree会在这里重新指向数组尾
// node数组的大小为size向上取整为2的幂
// 如果要保留table的旧node则应该在本函数被调用前保存
/// @param L 
/// @param t 
/// @param size 
static void setnodevector (lua_State *L, Table *t, unsigned int size) {
  if (size == 0) {  /* no elements to hash part? *///没有元素值放入hash
    t->node = cast(Node *, dummynode);  /* use common 'dummynode' */
    t->lsizenode = 0;
    t->lastfree = NULL;  /* signal that it is using dummy node */
  }
  else {
    int i;
    int lsize = luaO_ceillog2(size);// 整理成特殊要求的size 得到size的以2为底的对数
    if (lsize > MAXHBITS || (1u << lsize) > MAXHSIZE)
      luaG_runerror(L, "table overflow");
    size = twoto(lsize);//还原成 1024这样的普通数
    t->node = luaM_newvector(L, size, Node);//申请全新的MEM.Node
    for (i = 0; i < (int)size; i++) {//对新的Node填nil 
      Node *n = gnode(t, i);
      gnext(n) = 0;
      setnilkey(n);
      setempty(gval(n));
    }
    t->lsizenode = cast_byte(lsize);//设置lsizenode大小
    t->lastfree = gnode(t, size);  /* all positions are free *///设置lastfree位置
  }
}


/*
** (Re)insert all elements from the hash part of 'ot' into table 't'.
*/

/// @brief hash重新插入
/// @param L 
/// @param ot 需要重新插入的元素
/// @param t 
static void reinsert (lua_State *L, Table *ot, Table *t) {
  int j;
  int size = sizenode(ot);
  for (j = 0; j < size; j++) {
    Node *old = gnode(ot, j);
    if (!isempty(gval(old))) {
      /* doesn't need barrier/invalidate cache, as entry was
         already present in the table */
      TValue k;
      getnodekey(L, &k, old);
      luaH_set(L, t, &k, gval(old));
    }
  }
}


/*
** Exchange the hash part of 't1' and 't2'.
*/

/// @brief 交换 t1 和 t2 的哈希部分
/// @param t1 
/// @param t2 
static void exchangehashpart (Table *t1, Table *t2) {
  lu_byte lsizenode = t1->lsizenode;
  Node *node = t1->node;
  Node *lastfree = t1->lastfree;
  t1->lsizenode = t2->lsizenode;
  t1->node = t2->node;
  t1->lastfree = t2->lastfree;
  t2->lsizenode = lsizenode;
  t2->node = node;
  t2->lastfree = lastfree;
}


/*
** Resize table 't' for the new given sizes. Both allocations (for
** the hash part and for the array part) can fail, which creates some
** subtleties. If the first allocation, for the hash part, fails, an
** error is raised and that is it. Otherwise, it copies the elements from
** the shrinking part of the array (if it is shrinking) into the new
** hash. Then it reallocates the array part.  If that fails, the table
** is in its original state; the function frees the new hash part and then
** raises the allocation error. Otherwise, it sets the new hash part
** into the table, initializes the new part of the array (if any) with
** nils and reinserts the elements of the old hash back into the new
** parts of the table.
*/

/// @brief 按照numusehash,computesizes之前计算的结果重新分配空间
/// @param L 
/// @param t 
/// @param newasize 数组部分长度
/// @param nhsize 需要放到散列表中key的数量
void luaH_resize (lua_State *L, Table *t, unsigned int newasize,
                                          unsigned int nhsize) {
  unsigned int i;
  Table newt;  /* to keep the new hash part *///newt用来作为中转，并按照所需长度进行初始化
  unsigned int oldasize = setlimittosize(t);
  TValue *newarray;
  /* create new hash part with appropriate size into 'newt' */
  setnodevector(L, &newt, nhsize);
  if (newasize < oldasize) {  /* will array shrink? *///数组部分需要缩小的情况
    t->alimit = newasize;  /* pretend array has new size... */
    exchangehashpart(t, &newt);  /* and new hash *///将旧的散列表中的内容放到中转散列表中
    /* re-insert into the new hash the elements from vanishing slice */
    for (i = newasize; i < oldasize; i++) {
      if (!isempty(&t->array[i]))//由于数组部分缩短了，所以有一部分原来在数组中的内容需要移到散列表中
        luaH_setint(L, t, i + 1, &t->array[i]);
    }
    t->alimit = oldasize;  /* restore current size... */
    exchangehashpart(t, &newt);  /* and hash (in case of errors) *///再把老的散列表中的内容换回来，此时中转散列表中存放的是从数组中移出来的内容
  }
  /* allocate new array */
  newarray = luaM_reallocvector(L, t->array, oldasize, newasize, TValue);//按照新数组部分的长度申请内存 这里已经将老数组的内容拷贝到新数组中了
  if (l_unlikely(newarray == NULL && newasize > 0)) {  /* allocation failed? */
    freehash(L, &newt);  /* release new hash part *///分配内存失败
    luaM_error(L);  /* raise error (with array unchanged) */
  }
  /* allocation ok; initialize new part of the array */
  //将中转散列表中的内容和当前老的散列表中的内容互换
  //互换后，中转散列表中存放的是老的散列表中的内容
  //当前散列表中存放的，可能是空(数组部分不缩短的情况)，也可能是从数组中移出来的一部分内容(数组部分需要缩短的情况)
  exchangehashpart(t, &newt);  /* 't' has the new hash ('newt' has the old) */
  t->array = newarray;  /* set new array part *///数组部分指向新申请的内存
  t->alimit = newasize;
  for (i = oldasize; i < newasize; i++)  /* clear new slice of the array */
     setempty(&t->array[i]);
  /* re-insert elements from old hash part into new parts */
  //把中转散列表中存放的老的散列部分的内容，重新插回新的散列表中,
  //此时散列表中既包括了老的散列表的内容，也包括了从数组中移出来的部分
  reinsert(L, &newt, t);  /* 'newt' now has the old hash */
  freehash(L, &newt);  /* free old hash part *///释放中转散列表
}

/// @brief 调整数组大小
/// @param L 
/// @param t 
/// @param nasize 
void luaH_resizearray (lua_State *L, Table *t, unsigned int nasize) {
  int nsize = allocsizenode(t);
  luaH_resize(L, t, nasize, nsize);
}

/*
** nums[i] = number of keys 'k' where 2^(i - 1) < k <= 2^i
*/

/// @brief 对table 进行重新划分hash和数组部分的大小
/// @param L 
/// @param t 
/// @param ek 
static void rehash (lua_State *L, Table *t, const TValue *ek) {
  unsigned int asize;  /* optimal size for array part *///优化后的数组部分的大小
  unsigned int na;  /* number of keys in the array part *///记录数组部分存的元素个数
  unsigned int nums[MAXABITS + 1];
  int i;
  int totaluse;//记录表中key元素的总数 
  for (i = 0; i <= MAXABITS; i++) nums[i] = 0;  /* reset counts */
  setlimittosize(t);//设置alimit为表数组部分的实际大小，并设置对应的flags，返回表数组部分的实际大小
  na = numusearray(t, nums);  /* count keys in array part *///统计数组部分已经使用的元素数量
  totaluse = na;  /* all those keys are integer keys */
  totaluse += numusehash(t, nums, &na);  /* count keys in hash part *///统计散列表部分已经使用的节点数量
  /* count extra key */
  if (ttisinteger(ek))//需要插入的ek如果是整型
    na += countint(ivalue(ek), nums);//新增的key也要考虑进去
  totaluse++;
  /* compute new size for array part */
  asize = computesizes(nums, &na);//计算当前应该分配给数组部分的长度,以及放到数组部分中整数key的数量
  /* resize the table to new computed sizes */
  luaH_resize(L, t, asize, totaluse - na);//根据新的数组大小和散列表部分存入的元素个数调整数组大小和散列表大小
}



/*
** }=============================================================
*/

/// @brief 构造一张表
/// @param L 
/// @return 
Table *luaH_new (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_VTABLE, sizeof(Table));
  Table *t = gco2t(o);
  t->metatable = NULL;//元表相关
  t->flags = cast_byte(maskflags);  /* table has no metamethod fields */
  t->array = NULL;//处理数组部分
  t->alimit = 0;
  setnodevector(L, t, 0);//处理node部分
  return t;
}

/// @brief 释放表占用的MEM
/// @param L 
/// @param t 
void luaH_free (lua_State *L, Table *t) {
  freehash(L, t);
  luaM_freearray(L, t->array, luaH_realasize(t));
  luaM_free(L, t);
}

/// @brief 从这里可以看到freepos是倒序来的,主要是从后往前找,找到第一个free位置
/// @param t 
/// @return 
static Node *getfreepos (Table *t) {
  if (!isdummy(t)) {
    while (t->lastfree > t->node) {
      t->lastfree--;
      if (keyisnil(t->lastfree))
        return t->lastfree;
    }
  }
  return NULL;  /* could not find a free place */
}



/*
** inserts a new key into a hash table; first, check whether key's main
** position is free. If not, check whether colliding node is in its main
** position or not: if it is not, move colliding node to an empty place and
** put new key in its main position; otherwise (colliding node is in its main
** position), new key goes to an empty position.
*/

/// @brief  这个函数的主要功能将一个key插入哈希表，并返回key关联的value指针。
/// @param L 
/// @param t 
/// @param key 
/// @param value 
void luaH_newkey (lua_State *L, Table *t, const TValue *key, TValue *value) {
  Node *mp;
  TValue aux;
  if (l_unlikely(ttisnil(key)))//key是空值 报错 看到了么，tbl不支持nil的key
    luaG_runerror(L, "table index is nil");
  else if (ttisfloat(key)) {//key是float 转成int 不能就报错
    lua_Number f = fltvalue(key);
    lua_Integer k;
    if (luaV_flttointeger(f, &k, F2Ieq)) {  /* does key fit in an integer? *///float 转int
      setivalue(&aux, k);
      key = &aux;  /* insert it as an integer */
    }
    else if (l_unlikely(luai_numisnan(f)))//因为根据IEEE 754，nan值被认为不等于任何值，包括它自己
      luaG_runerror(L, "table index is NaN");
  }
  if (ttisnil(value))//值是nil类型
    return;  /* do not insert nil values */
  mp = mainpositionTV(t, key);//mp位根据哈希值得到的应该插入位置的节点
  if (!isempty(gval(mp)) || isdummy(t)) {  /* main position is taken? */
    // mp节点已经有值
    // 此时存在两种情况
    //   1.本次要新加的对象和现在存在这里的对象出现了碰撞,这个时候需要吧新的对象放到后面的第一个空位中,并从逻辑上将其链接到该位置的链表上
    //   2.现在存放在这里的对象的hash值不在这里,是因为跟其他位置发送的碰撞后存放在这里的,这时需要将现在存放在这里的对象放到后面第一个空位上
    //     新加的对象存放在这里

    Node *othern;
    Node *f = getfreepos(t);  /* get a free place *///无论以上那种情况,都需要寻找下一个空的位置
    if (f == NULL) {  /* cannot find a free place? *///slot全满，则只能重新rehash扩容了
      rehash(L, t, key);  /* grow table */// 扩容
      /* whatever called 'newkey' takes care of TM cache */
      luaH_set(L, t, key, value);  /* insert key into grown table *///将value添加到表里
      return;
    }
    lua_assert(!isdummy(t));//hash表不空
    othern = mainpositionfromnode(t, mp);//计算现在存放在这里的对象mp本来应该放在那个位置
    if (othern != mp) {  /* is colliding node out of its main position? *///对象本来不应该放在这里,说明在别处碰撞后放在了这里
      /* yes; move colliding node into free position */
      while (othern + gnext(othern) != mp)  /* find previous *///顺着othern的一路找到mp的前置节点
        othern += gnext(othern);
      gnext(othern) = cast_int(f - othern);  /* rechain to point to 'f' *///本来前置节点指向的是mp的位置,现在改为指向新找的空位f
      *f = *mp;  /* copy colliding node into free pos. (mp->next also goes) *///将mp存到f
      if (gnext(mp) != 0) {    //处理mp的后继节点
        gnext(f) += cast_int(mp - f);  /* correct 'next' */
        gnext(mp) = 0;  /* now 'mp' is free */
      }
      setempty(gval(mp));//将它腾出来的，原本属于我的位置的value域填入nil值(擦除残留的值) 
    }
    else {  /* colliding node is in its own main position *///对象本来就应该放在这里,说明跟本次要新加的对象发生了碰撞
      /* new node will go into free position */
      if (gnext(mp) != 0)// 这里从逻辑上,新加的对象插到mp和后继链表的中间,避免遍历链表的消耗,f的下一个位置指向mp的下一个位置
        gnext(f) = cast_int((mp + gnext(mp)) - f);  /* chain new position */
      else lua_assert(gnext(f) == 0);//链表的要求，这里必须gnext(f) == 0
      gnext(mp) = cast_int(f - mp);//mp的下一个位置指向f
      mp = f;
    }
  }

  // 把key的值复制给mp节点,并返回节点的指针
  setnodekey(L, mp, key);
  luaC_barrierback(L, obj2gco(t), key);//进行GC的barrierback操作，确保black不会指向white 
  lua_assert(isempty(gval(mp)));//函数名为newkey，所以这里判断下val==nil，确保上面将对应的pos的val置空了
  setobj2t(L, gval(mp), value);//最后将新value赋值给mp
}


/*
** Search function for integers. If integer is inside 'alimit', get it
** directly from the array part. Otherwise, if 'alimit' is not equal to
** the real size of the array, key still can be in the array part. In
** this case, try to avoid a call to 'luaH_realasize' when key is just
** one more than the limit (so that it can be incremented without
** changing the real size of the array).
*/

/// @brief 
// 如果key的大小在数组大小范围内，那么就直接在数组中查找值并返回。
// 否则，获取int的hash值对应的 hash node，然后在slot-link上找到key对应的值并返回。（和链地址法的查找是一样的）
// 如果找不到，则返回nil。

// key在[1, sizearray)时在数组部分
// key<=0或key>=sizearray则在哈希部分
/// @param t 
/// @param key 
/// @return 
const TValue *luaH_getint (Table *t, lua_Integer key) {
  if (l_castS2U(key) - 1u < t->alimit)  /* 'key' in [1, t->alimit]? *///在数组大小范围
    return &t->array[key - 1];//返回数组对应的下标内容
  else if (!limitequalsasize(t) &&  /* key still may be in the array part? *///alimit是否为表的数组部分的实际大小 
           (l_castS2U(key) == t->alimit + 1 ||
            l_castS2U(key) - 1u < luaH_realasize(t))) {
    t->alimit = cast_uint(key);  /* probably '#t' is here now *///设置alimit为key 
    return &t->array[key - 1];//返回对应数组的元素
  }
  else {
    // 1. 这里是哈希部分，整型直接key & nodesize得到数组索引，取出结点地址返回
    Node *n = hashint(t, key);
    for (;;) {  /* check whether 'key' is somewhere in the chain */
      if (keyisinteger(n) && keyival(n) == key)//2. 比较该结点的key相等(同为整型且值相同)，是则返回值
        return gval(n);  /* that's it */// 返回n的值的指针
      else {
        int nx = gnext(n);// 3. 如果不是，通过上面所说的next取链接的下一个结点 因为是相对偏移，所以只要n+=nx即可得到连接的结点指针，再回到2
        if (nx == 0) break;
        n += nx;
      }
    }
    return &absentkey;//不存在对应的key
  }
}


/*
** search function for short strings
*/

/// @brief 从表t中查找短字符串为键的值
/// @param t 
/// @param key 
/// @return 
const TValue *luaH_getshortstr (Table *t, TString *key) {
  Node *n = hashstr(t, key);//根据短字符串的散列值对散列表大小取余去获取对应的节点
  lua_assert(key->tt == LUA_VSHRSTR);//保证是短字符串类型
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    if (keyisshrstr(n) && eqshrstr(keystrval(n), key))//判断是否为相同字符串,比较的是地址
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);//获取Node n的next
      if (nx == 0)
        return &absentkey;  /* not found *///没找到
      n += nx;//获取下一个Node在node hash表中索引值n
    }
  }
}

/// @brief 如果key是string就调用luaH_getstr
/// @param t 
/// @param key 
/// @return 
const TValue *luaH_getstr (Table *t, TString *key) {
  if (key->tt == LUA_VSHRSTR)//如果是短字符串
    return luaH_getshortstr(t, key);//从表t中查找短字符串为键的值
  else {  /* for long strings, use generic case *///如果是长字符串
    TValue ko;
    setsvalue(cast(lua_State *, NULL), &ko, key);
    return getgeneric(t, &ko, 0);//从表t的散列表部分查找键为key的值是否存在，存在则返回
  }
}


/*
** main search function
*/

/// @brief 从表中查找对应key的键值对是否存在，存在则返回对应的值对象
/// @param t 
/// @param key 
/// @return 
const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttypetag(key)) {
    case LUA_VSHRSTR: return luaH_getshortstr(t, tsvalue(key));//短串
    case LUA_VNUMINT: return luaH_getint(t, ivalue(key));//整型
    case LUA_VNIL: return &absentkey;//nil
    case LUA_VNUMFLT: {//float
      lua_Integer k;
      if (luaV_flttointeger(fltvalue(key), &k, F2Ieq)) /* integral index? *///如果能float能转换成int
        return luaH_getint(t, k);  /* use specialized version *///走整型方式获取
      /* else... */
    }  /* FALLTHROUGH */
    default:
      return getgeneric(t, key, 0);//从表t的散列表部分查找键为key的值是否存在，存在则返回
  }
}


/*
** Finish a raw "set table" operation, where 'slot' is where the value
** should have been (the result of a previous "get table").
** Beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
*/

/// @brief 进行设置
/// @param L 
/// @param t 
/// @param key 
/// @param slot 
/// @param value 
void luaH_finishset (lua_State *L, Table *t, const TValue *key,
                                   const TValue *slot, TValue *value) {
  if (isabstkey(slot))//找不到key
    luaH_newkey(L, t, key, value);//重新new一个
  else
    setobj2t(L, cast(TValue *, slot), value);//直接设置
}


/*
** beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
*/

/// @brief hash插入值,这里可能会重组table的内存,也就是数组和hash部分会进行重建
/// @param L 
/// @param t 
/// @param key 
/// @param value /
void luaH_set (lua_State *L, Table *t, const TValue *key, TValue *value) {
  const TValue *slot = luaH_get(t, key);//获取槽位
  luaH_finishset(L, t, key, slot, value);//进行设置
}

/// @brief 在Table上设置key为数字类型的节点,主要是处理数组部分
/// @param L 
/// @param t 
/// @param key 
/// @param value 
void luaH_setint (lua_State *L, Table *t, lua_Integer key, TValue *value) {
  const TValue *p = luaH_getint(t, key);//键已存在
  if (isabstkey(p)) {//找不到
    TValue k;
    setivalue(&k, key);
    luaH_newkey(L, t, &k, value);//重新生成一个
  }
  else
    setobj2t(L, cast(TValue *, p), value);
}


/*
** Try to find a boundary in the hash part of table 't'. From the
** caller, we know that 'j' is zero or present and that 'j + 1' is
** present. We want to find a larger key that is absent from the
** table, so that we can do a binary search between the two keys to
** find a boundary. We keep doubling 'j' until we get an absent index.
** If the doubling would overflow, we try LUA_MAXINTEGER. If it is
** absent, we are ready for the binary search. ('j', being max integer,
** is larger or equal to 'i', but it cannot be equal because it is
** absent while 'i' is present; so 'j > i'.) Otherwise, 'j' is a
** boundary. ('j + 1' cannot be a present integer key because it is
** not a valid integer in Lua.)
*/

/// @brief hash二分查找
/// @param t 
/// @param j 
/// @return 
static lua_Unsigned hash_search (Table *t, lua_Unsigned j) {
  lua_Unsigned i;
  if (j == 0) j++;  /* the caller ensures 'j + 1' is present */
  do {
    i = j;  /* 'i' is a present index *///i是当前索引
    if (j <= l_castS2U(LUA_MAXINTEGER) / 2)
      j *= 2;
    else {
      j = LUA_MAXINTEGER;
      if (isempty(luaH_getint(t, j)))  /* t[j] not present? */
        break;  /* 'j' now is an absent index */
      else  /* weird case */
        return j;  /* well, max integer is a boundary... *///最大值为边界
    }
  } while (!isempty(luaH_getint(t, j)));  /* repeat until an absent t[j] *///找到t[j]为nil
  /* i < j  &&  t[i] present  &&  t[j] absent */
  while (j - i > 1u) {  /* do a binary search between them *///一直进行二分查找
    lua_Unsigned m = (i + j) / 2;
    if (isempty(luaH_getint(t, m))) j = m;
    else i = m;
  }
  return i;
}

/// @brief 二分查找
/// @param array 
/// @param i 
/// @param j 
/// @return 
static unsigned int binsearch (const TValue *array, unsigned int i,
                                                    unsigned int j) {
  while (j - i > 1u) {  /* binary search */
    unsigned int m = (i + j) / 2;
    if (isempty(&array[m - 1])) j = m;
    else i = m;
  }
  return i;
}


/*
** Try to find a boundary in table 't'. (A 'boundary' is an integer index
** such that t[i] is present and t[i+1] is absent, or 0 if t[1] is absent
** and 'maxinteger' if t[maxinteger] is present.)
** (In the next explanation, we use Lua indices, that is, with base 1.
** The code itself uses base 0 when indexing the array part of the table.)
** The code starts with 'limit = t->alimit', a position in the array
** part that may be a boundary.
**
** (1) If 't[limit]' is empty, there must be a boundary before it.
** As a common case (e.g., after 't[#t]=nil'), check whether 'limit-1'
** is present. If so, it is a boundary. Otherwise, do a binary search
** between 0 and limit to find a boundary. In both cases, try to
** use this boundary as the new 'alimit', as a hint for the next call.
**
** (2) If 't[limit]' is not empty and the array has more elements
** after 'limit', try to find a boundary there. Again, try first
** the special case (which should be quite frequent) where 'limit+1'
** is empty, so that 'limit' is a boundary. Otherwise, check the
** last element of the array part. If it is empty, there must be a
** boundary between the old limit (present) and the last element
** (absent), which is found with a binary search. (This boundary always
** can be a new limit.)
**
** (3) The last case is when there are no elements in the array part
** (limit == 0) or its last element (the new limit) is present.
** In this case, must check the hash part. If there is no hash part
** or 'limit+1' is absent, 'limit' is a boundary.  Otherwise, call
** 'hash_search' to find a boundary in the hash part of the table.
** (In those cases, the boundary is not inside the array part, and
** therefore cannot be used as a new limit.)
*/

/// @brief 返回第一个本身不为空而后一个元素为空的位置
// 对应的就是table的我们常用的取长度操作：#t这个函数的功能是获取table的边界。而table的边界是这样定义的：
// 边界是针对table的数组部分的，但若哈希部分的key为整数且刚好连着数组部分，则也会一并参与计算。
// 若某个整数下标n，满足table[boundary]不为空，而table[boundary+1]为空，则n为table的边界。
// table[boundary] != nil
// table[boundary+1] == nil

// 为了提高查找效率，lua源码并没有进行遍历查找，而是通过二分查找。（时间复杂度从O(n)降到O(logn)
// (1)如果table数组部分的最后一个元素为nil，那么将在数组部分进行二分查找
// (2)针对table的数组部分的，但若哈希部分的key为整数且刚好连着数组部分，则也会一并参与计算
// (3)最后一种情况是数组部分中没有元素 或如果table数组部分的最后一个元素为nil，那么将在hash部分进行二分查找
/// @param t 
/// @return 
lua_Unsigned luaH_getn (Table *t) {
  unsigned int limit = t->alimit;
  if (limit > 0 && isempty(&t->array[limit - 1])) {  /* (1)? *///如果table数组部分的最后一个元素为nil，那么将在数组部分进行二分查找
    if (limit >= 2 && !isempty(&t->array[limit - 2])) {//数组倒数第二个元素不为空 比如{1,2,3,nil}, 返回值为3
      /* 'limit - 1' is a boundary; can it be a new limit? */
      if (ispow2realasize(t) && !ispow2(limit - 1)) {//[1] //limit - 1是边界,并且不是2的幂 
        t->alimit = limit - 1;//设置新的limit
        setnorealasize(t);  /* now 'alimit' is not the real size *///设置属性为数组真实大小
      }
      return limit - 1;//否则直接返回 这块其实是利用setnorealasize(t)属性设置的特性,也就是其实只要进入编号[1]一次就行了
    }
    else {  /* must search for a boundary in [0, limit] */
      unsigned int boundary = binsearch(t->array, 0, limit);//在数组部分进行二分查找
      /* can this boundary represent the real size of the array? */
      if (ispow2realasize(t) && boundary > luaH_realasize(t) / 2) {//查看下这个边界能不能代表数组的实际大小
        t->alimit = boundary;  /* use it as the new limit */
        setnorealasize(t);
      }
      return boundary;
    }
  }

  /* 'limit' is zero or present in table */
  if (!limitequalsasize(t)) {  /* (2)? */// 针对table的数组部分的，但若哈希部分的key为整数且刚好连着数组部分，则也会一并参与计算
    /* 'limit' > 0 and array has more elements after 'limit' */
    if (isempty(&t->array[limit]))  /* 'limit + 1' is empty? *///limit + 1' 是空的
      return limit;  /* this is the boundary *///直接返回limit
    /* else, try last element in the array */
    limit = luaH_realasize(t);
    if (isempty(&t->array[limit - 1])) {  /* empty? */
      /* there must be a boundary in the array after old limit,
         and it must be a valid new limit */
      unsigned int boundary = binsearch(t->array, t->alimit, limit);//如果table数组部分的最后一个元素为nil，那么将在数组部分进行二分查找
      t->alimit = boundary;
      return boundary;
    }
    /* else, new limit is present in the table; check the hash part */
  }
  /* (3) 'limit' is the last element and either is zero or present in table *///最后一种情况是数组部分中没有元素 或如果table数组部分的最后一个元素为nil，那么将在hash部分进行二分查找
  lua_assert(limit == luaH_realasize(t) &&
             (limit == 0 || !isempty(&t->array[limit - 1])));
  if (isdummy(t) || isempty(luaH_getint(t, cast(lua_Integer, limit + 1))))//如果hash表不连贯返回了absentkey
    return limit;  /* 'limit + 1' is absent */
  else  /* 'limit + 1' is also present */
    return hash_search(t, limit);//hash查找
}



#if defined(LUA_DEBUG)

/* export these functions for the test library */

Node *luaH_mainposition (const Table *t, const TValue *key) {
  return mainpositionTV(t, key);
}

int luaH_isdummy (const Table *t) { return isdummy(t); }

#endif
