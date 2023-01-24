/*
 * @文件作用: 字符串池
 * @功能分类: 虚拟机运转的核心功能
 * @注释者: frog-game
 * @LastEditTime: 2023-01-24 22:18:28
 */
/*
** $Id: lstring.c $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define lstring_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"


/*
** Maximum size for string table.
*/
#define MAXSTRTB	cast_int(luaM_limitN(MAX_INT, TString*))


/*
** equality for long strings
*/

/// @brief 长串-比较字符串是否相等
/// @param a 
/// @param b 
/// @return 
int luaS_eqlngstr (TString *a, TString *b) {
  size_t len = a->u.lnglen;
  lua_assert(a->tt == LUA_VLNGSTR && b->tt == LUA_VLNGSTR);//assert同类型
  return (a == b) ||  /* same instance or... */
    ((len == b->u.lnglen) &&  /* equal length and ... */
     (memcmp(getstr(a), getstr(b), len) == 0));  /* equal contents *///先看是否指向同一对象, 在比较长度是否相等并且利用字符串长度, 用memcmp比较内存内容
}

/// @brief 计算字符串的hash值
//加密用的哈希函数，例如SHA-256, SHA-512, MD5, RIPEMD-160等
// lua hash 是非加密用的哈希函数
// ^[异或]这个符号能把二进制位的01均匀分散，&[与] |[或] 做不到 同或虽然能做到但是c语言没有这个符号，需要用异或去转,详细见下面
//  a | b | a AND b
// ---+---+--------
//  0 | 0 |    0
//  0 | 1 |    0
//  1 | 0 |    0
//  1 | 1 |    1

//  a | b | a OR b
// ---+---+--------
//  0 | 0 |    0
//  0 | 1 |    1
//  1 | 0 |    1
//  1 | 1 |    1

//  a | b | a XOR b
// ---+---+--------
//  0 | 0 |    0
//  0 | 1 |    1
//  1 | 0 |    1
//  1 | 1 |    0

//更详细的原理去 Knuth 在《计算机编程艺术》第 3 卷 第 2 版的 6.4 节查找讨论的hash算法
/// @param str 待哈希的字符串
/// @param l 待哈希的字符串长度（字符数）
/// @param seed 哈希算法随机种子
/// @return 
unsigned int luaS_hash (const char *str, size_t l, unsigned int seed) {
  unsigned int h = seed ^ cast_uint(l);
  for (; l > 0; l--)
    h ^= ((h<<5) + (h>>2) + cast_byte(str[l - 1]));
  return h;
}

/// @brief 如果长串没有计算过hash，则调用luaS_hashlongstr来计算
/// @param ts 
/// @return 
unsigned int luaS_hashlongstr (TString *ts) {
  lua_assert(ts->tt == LUA_VLNGSTR);//必须是长字符串
  if (ts->extra == 0) {  /* no hash? *///没算出hash
    size_t len = ts->u.lnglen;
    ts->hash = luaS_hash(getstr(ts), len, ts->hash);//设置hash
    ts->extra = 1;  /* now it has its hash *///标识设置成已设置
  }
  return ts->hash;//返回hash
}

/// @brief 对短字符串散列表重新计算哈希
/// @param vect 
/// @param osize 旧的散列表大小
/// @param nsize 新的大小
static void tablerehash (TString **vect, int osize, int nsize) {
  int i;
  for (i = osize; i < nsize; i++)  /* clear new elements *///清除掉新增的桶数据 
    vect[i] = NULL;
  for (i = 0; i < osize; i++) {  /* rehash old part of the array *///重新散列旧的数据 
    TString *p = vect[i];
    vect[i] = NULL;
    while (p) {  /* for each string in the list *///遍历每个一个桶链表中的字符串
      TString *hnext = p->u.hnext;  /* save next */
      unsigned int h = lmod(p->hash, nsize);  /* new position *///得到新的hash位置
      p->u.hnext = vect[h];  /* chain it into array */// 把指针指向新的hash桶里面元素h位置
      vect[h] = p;//附上vect[i]里面的内容
      p = hnext;//往前走一位
    }
  }
}


/*
** Resize the string table. If allocation fails, keep the current size.
** (This can degrade performance, but any non-zero size should work
** correctly.)
*/

/// @brief 该函数可以扩大或缩小hash表。扩大hash表时，则需要重新计算原有对象的hash值，调整原有元素的位置
/// @param L 
/// @param nsize 新的空间大小
void luaS_resize (lua_State *L, int nsize) {
  stringtable *tb = &G(L)->strt;
  int osize = tb->size;
  TString **newvect;
  if (nsize < osize)  /* shrinking table? *///收缩表
    tablerehash(tb->hash, osize, nsize);  /* depopulate shrinking part */
  newvect = luaM_reallocvector(L, tb->hash, osize, nsize, TString*);//如果osize>=nsize缩小原来的内存块，如果nsize>osize重新分配一块新的内存并将原来的内存块内容copy到新的中
  if (l_unlikely(newvect == NULL)) {  /* reallocation failed? *///分配内存失败
    if (nsize < osize)  /* was it shrinking table? *///如果是缩小表
      tablerehash(tb->hash, nsize, osize);  /* restore to original size *///回退
    /* leave table as it was */
  }
  else {  /* allocation succeeded *///分配成功
    tb->hash = newvect;
    tb->size = nsize;
    if (nsize > osize)
      tablerehash(newvect, osize, nsize);  /* rehash for new size *///如果是扩展内存，则重新散列之前的字符串
  }
}


/*
** Clear API string cache. (Entries cannot be empty, so fill them with
** a non-collectable string.)
*/

/// @brief  清除字符串缓冲区中将被GC的字符串
/// @param g 
void luaS_clearcache (global_State *g) {
  int i, j;
  for (i = 0; i < STRCACHE_N; i++)
    for (j = 0; j < STRCACHE_M; j++) {
      if (iswhite(g->strcache[i][j]))  /* will entry be collected? *////白色的就回收
        g->strcache[i][j] = g->memerrmsg;  /* replace it with something fixed */
    }
}


/*
** Initialize the string table and the string cache
*/

/// @brief 初始化短字符串表和字符串缓冲
/// @param L 
void luaS_init (lua_State *L) {
  global_State *g = G(L);
  int i, j;
  stringtable *tb = &G(L)->strt;
  tb->hash = luaM_newvector(L, MINSTRTABSIZE, TString*);//分配一个128大小的短字符串散列表
  tablerehash(tb->hash, 0, MINSTRTABSIZE);  /* clear array *///初始化散列表
  tb->size = MINSTRTABSIZE;
  /* pre-create memory-error message */
  g->memerrmsg = luaS_newliteral(L, MEMERRMSG);//创建内存错误信息字符串
  luaC_fix(L, obj2gco(g->memerrmsg));  /* it should never be collected *///设置为不会被GC对象
  for (i = 0; i < STRCACHE_N; i++)  /* fill cache with valid strings *///用上面创建的字符串填充字符串缓冲区内容
    for (j = 0; j < STRCACHE_M; j++)
      g->strcache[i][j] = g->memerrmsg;
}



/*
** creates a new string object
*/

/// @brief 创建一个字符串对象
/// @param L 
/// @param l 长度
/// @param tag LUA_VLNGSTR或者LUA_VSHRSTR类型
/// @param h hash 值
/// @return 
static TString *createstrobj (lua_State *L, size_t l, int tag, unsigned int h) {
  TString *ts;
  GCObject *o;
  size_t totalsize;  /* total size of TString object */
  totalsize = sizelstring(l);
  o = luaC_newobj(L, tag, totalsize);//创建字符串类型的GC对象 创建的GC对象会添加到g->allgc链表中
  ts = gco2ts(o);
  ts->hash = h;
  ts->extra = 0;
  getstr(ts)[l] = '\0';  /* ending 0 */
  return ts;
}

/// @brief 创建一个长字符串对象
/// @param L 
/// @param l 
/// @return 
TString *luaS_createlngstrobj (lua_State *L, size_t l) {
  TString *ts = createstrobj(L, l, LUA_VLNGSTR, G(L)->seed);//这个createstrobj是直接创建一份malloc指定大小的内存空间
  ts->u.lnglen = l;
  return ts;
}

/// @brief 将指定的短字符串从字符串表里移除
/// @param L 
/// @param ts 
void luaS_remove (lua_State *L, TString *ts) {
  stringtable *tb = &G(L)->strt;
  TString **p = &tb->hash[lmod(ts->hash, tb->size)];
  while (*p != ts)  /* find previous element *///从桶链表中查找指定的字符串
    p = &(*p)->u.hnext;
  *p = (*p)->u.hnext;  /* remove element from its list *///从桶链表中移除
  tb->nuse--;
}

/// @brief 字符串哈希表生长
/// @param L 
/// @param tb 
static void growstrtab (lua_State *L, stringtable *tb) {
  if (l_unlikely(tb->nuse == MAX_INT)) {  /* too many strings? *////当元素等于最大值时强制gc回收
    luaC_fullgc(L, 1);  /* try to free some... *///强制回收
    if (tb->nuse == MAX_INT)  /* still too many? *///回收一次后,还是很多
      luaM_error(L);  /* cannot even create a message... *///抛出error
  }
  if (tb->size <= MAXSTRTB / 2)  /* can grow string table? *///可以增长字符串表
    luaS_resize(L, tb->size * 2);//扩大空间
}


/*
** Checks whether short string exists and reuses it or creates a new one.
*/

/// @brief 创建短字符串
/// @param L 
/// @param str 
/// @param l 
/// @return 返回短字符串
static TString *internshrstr (lua_State *L, const char *str, size_t l) {
  TString *ts;
  global_State *g = G(L);
  stringtable *tb = &g->strt;
  unsigned int h = luaS_hash(str, l, g->seed);//得到一个hash值
  TString **list = &tb->hash[lmod(h, tb->size)];//用hash得到的模,得到hash桶链表
  lua_assert(str != NULL);  /* otherwise 'memcmp'/'memcpy' are undefined *///不能是空
  for (ts = *list; ts != NULL; ts = ts->u.hnext) {//进行遍历查找
    if (l == ts->shrlen && (memcmp(str, getstr(ts), l * sizeof(char)) == 0)) {//如果找到了
      /* found! */
      if (isdead(g, ts))  /* dead (but not collected yet)? *///如果是死亡状态，但是还没有回收
        changewhite(ts);  /* resurrect it *///复活他
      return ts;
    }
  }
  /* else must create a new string *///否则必须创建一个新字符串
  if (tb->nuse >= tb->size) {  /* need to grow string table? *///需要扩容
    growstrtab(L, tb);//扩容
    list = &tb->hash[lmod(h, tb->size)];  /* rehash with new size *///用新尺寸重新散列
  }
  ts = createstrobj(L, l, LUA_VSHRSTR, h);//创建一个短字符串
  memcpy(getstr(ts), str, l * sizeof(char));//进行拷贝
  ts->shrlen = cast_byte(l);//设置大小
  ts->u.hnext = *list;//进行链接
  *list = ts;//指向新串
  tb->nuse++;//元素加1
  return ts;//返回短字符串
}


/*
** new string (with explicit length)
*/

/// @brief 长度小于等于LUAI_MAXSHORTLEN的字符串，调用 internshrstr。对于长度大于LUAI_MAXSHORTLEN的则一定创建一个TString对象
/// @param L 
/// @param str 
/// @param l 长度
/// @return 
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  if (l <= LUAI_MAXSHORTLEN)  /* short string? *///小于等于40个字符长度的
    return internshrstr(L, str, l);//创建短串
  else {
    TString *ts;
    if (l_unlikely(l >= (MAX_SIZE - sizeof(TString))/sizeof(char)))//检测一下字符串是不是太大
      luaM_toobig(L);//报错
    ts = luaS_createlngstrobj(L, l);//创建长字符串
    memcpy(getstr(ts), str, l * sizeof(char));//进行拷贝
    return ts;
  }
}


/*
** Create or reuse a zero-terminated string, first checking in the
** cache (using the string address as a key). The cache can contain
** only zero-terminated strings, so it is safe to use 'strcmp' to
** check hits.
*/

/// @brief 创建字符串对外接口
// 创建或重用一个以'0'结尾的字符串，首先查找 global_state 的 strcache 缓存是否已存在该字符串，
// 若存在则直接返回，否则创建一个新的，并加入到缓存中
/// @param L 
/// @param str 
/// @return 
TString *luaS_new (lua_State *L, const char *str) {
  unsigned int i = point2uint(str) % STRCACHE_N;  /* hash *///i 是保存strcache数组行的位置
  int j;
  TString **p = G(L)->strcache[i];
  for (j = 0; j < STRCACHE_M; j++) {//开始找有没有这个string
    if (strcmp(str, getstr(p[j])) == 0)  /* hit? */
      return p[j];  /* that is it */
  }
  /* normal route */

  //如果没有找到创建新的Tstring,并放到第一个位置
  //同时也意味着最后一个元素会被移除strcache
  for (j = STRCACHE_M - 1; j > 0; j--) //将i行对应链表的最后一个元素设为前一个的值
    p[j] = p[j - 1];  /* move out last element */
  /* new element is first in the list */
  p[0] = luaS_newlstr(L, str, strlen(str));	//这样就能把新创建的字符串插在该行的最开头
  return p[0];
}

/// @brief 创建userdata
/// @param L 
/// @param s 
/// @param nuvalue 
/// @return 
Udata *luaS_newudata (lua_State *L, size_t s, int nuvalue) {
  Udata *u;
  int i;
  GCObject *o;
  if (l_unlikely(s > MAX_SIZE - udatamemoffset(nuvalue)))//长度太大
    luaM_toobig(L);//报错
  o = luaC_newobj(L, LUA_VUSERDATA, sizeudata(nuvalue, s));//生成一个userdata
  u = gco2u(o);//转换
  u->len = s;//设置长度
  u->nuvalue = nuvalue;//设置上值个数
  u->metatable = NULL;//元方法初始化
  for (i = 0; i < nuvalue; i++)//初始化上值value
    setnilvalue(&u->uv[i].uv);
  return u;
}

