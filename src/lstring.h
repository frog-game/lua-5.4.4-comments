/*
 * @文件作用: 字符串池
 * @功能分类: 虚拟机运转的核心功能
 * @注释者: frog-game
 * @LastEditTime: 2023-01-21 20:52:27
 */
/*
** $Id: lstring.h $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


// 短字符串
//     1. 短字符串在生成时就已经计算好哈希值，因此在查找时直接用哈希值取模即可。
//     2. 因为整个虚拟机相同内容的短字符串就一份，那么完全可以比较其地址，地址相同就表示Key相同(可查看代码中的这个宏eqshrstr)。


//长字符串
//     1. 一开始长字符串并不会马上计算哈希值，它将TString的extra设为0表示哈希值还未计算，对该字符串第一次调用luaS_hashlongstr才会去计算
//     2. 计算长字符串的哈希值，为了尽量减少计算的消耗，它用采样的方式
//     3. 长字符串的比较：先比较地址，再比较长度，最后比较内容
/*
** Memory-allocation error message must be preallocated (it cannot
** be created after memory is exhausted)
*/
#define MEMERRMSG       "not enough memory"//没有足够的内存


/*
** Size of a TString: Size of the header plus space for the string
** itself (including final '\0').
*/

///字符串的总大小 
// + 1 是因为是c语言后面还会在加个'\0'
#define sizelstring(l)  (offsetof(TString, contents) + ((l) + 1) * sizeof(char))

///目前来看是创建系统保留字的接口
#define luaS_newliteral(L, s)	(luaS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))


/*
** test whether a string is a reserved word
*/

///字符串是否为保留字
#define isreserved(s)	((s)->tt == LUA_VSHRSTR && (s)->extra > 0)


/*
** equality for short strings, which are always internalized
*/

///短字符串会放入字符串常量池中，因此短串在内存中总是只有一份，直接比较地址即可
#define eqshrstr(a,b)	check_exp((a)->tt == LUA_VSHRSTR, (a) == (b))


LUAI_FUNC unsigned int luaS_hash (const char *str, size_t l, unsigned int seed);
LUAI_FUNC unsigned int luaS_hashlongstr (TString *ts);
LUAI_FUNC int luaS_eqlngstr (TString *a, TString *b);
LUAI_FUNC void luaS_resize (lua_State *L, int newsize);
LUAI_FUNC void luaS_clearcache (global_State *g);
LUAI_FUNC void luaS_init (lua_State *L);
LUAI_FUNC void luaS_remove (lua_State *L, TString *ts);
LUAI_FUNC Udata *luaS_newudata (lua_State *L, size_t s, int nuvalue);
LUAI_FUNC TString *luaS_newlstr (lua_State *L, const char *str, size_t l);
LUAI_FUNC TString *luaS_new (lua_State *L, const char *str);
LUAI_FUNC TString *luaS_createlngstrobj (lua_State *L, size_t l);


#endif
