/*
** $Id: lapi.c $
** Lua API
** See Copyright Notice in lua.h
*/

/* 基础概念: 
    1.伪索引: 
        Lua栈的正常索引 从栈顶算,栈顶为-1,向栈低递减. 从栈低算,栈低为1,向栈顶递增. 
        伪索引是一种索引,他不在栈的位置中,通过一个宏来定义伪索引的位置. 
        伪索引被用来访问注册表,或者在lua_CFunction中访问upvalue. 
 
    2.注册表: 
        Lua的注册表是一个预定义的table, 可以提供给c api存储一切想要存储的值. 
        注册表通过 LUA_REGISTRYINDEX 伪索引来访问. 
        例如 lua_getfield 函数可以像下面这样使用来获取注册表中的一个以"hello"为key的值 : 
            lua_getfield( L , LUA_REGISTRYINDEX , "hello"); 
         
    3. upvalue: 
        在使用 lua_pushcfunction 或者 luaL_setfuncs 将一个lua_CFunction 注册到Lua环境中时, 
        可以同时为这个函数设置一些upvalue .  
        而后在这些lua_CFunction 中可以使用 lua_upvalueindex(n) 函数来获取对应位置的upvalue. 
*/

#define lapi_c
#define LUA_CORE

#include "lprefix.h"


#include <limits.h>
#include <stdarg.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"



const char lua_ident[] =
  "$LuaVersion: " LUA_COPYRIGHT " $"
  "$LuaAuthors: " LUA_AUTHORS " $";



/*
** Test for a valid index (one that is not the 'nilvalue').
** '!ttisnil(o)' implies 'o != &G(L)->nilvalue', so it is not needed.
** However, it covers the most common cases in a faster way.
*/
/// @brief 是否有效
#define isvalid(L, o)	(!ttisnil(o) || o != &G(L)->nilvalue)


/* test for pseudo index */
/// @brief 是不是假索引 假索引除了他对应的值不在栈中之外，其他都类似于栈中的索引
#define ispseudo(i)		((i) <= LUA_REGISTRYINDEX)

/* test for upvalue */
/// @brief 是不是上值
#define isupvalue(i)		((i) < LUA_REGISTRYINDEX)


/*
** Convert an acceptable index to a pointer to its respective value.
** Non-valid indices return the special nil value 'G(L)->nilvalue'.
*/

/// @brief 获取指定索引的值.并转换成TValue指针
/// @param L 
/// @param idx 
/// @return 
static TValue *index2value (lua_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {//这个索引是当前调用call的栈中的索引
    StkId o = ci->func + idx;
    api_check(L, idx <= L->ci->top - (ci->func + 1), "unacceptable index");
    if (o >= L->top) return &G(L)->nilvalue;
    else return s2v(o);
  }
  else if (!ispseudo(idx)) {  /* negative index */ // > -1001000(LUA_REGISTRYINDEX): 在L的栈中来取值
    api_check(L, idx != 0 && -idx <= L->top - (ci->func + 1), "invalid index");
    return s2v(L->top + idx);
  }
  else if (idx == LUA_REGISTRYINDEX)// == -1001000(LUA_REGISTRYINDEX): 取l_registry（寄存器）
    return &G(L)->l_registry;
  else {  /* upvalues */// < -1001000(LUA_REGISTRYINDEX)
    idx = LUA_REGISTRYINDEX - idx;
    api_check(L, idx <= MAXUPVAL + 1, "upvalue index too large");
    if (ttisCclosure(s2v(ci->func))) {  /* C closure? *///取当前调用（只能是c闭包）的upvalue
      CClosure *func = clCvalue(s2v(ci->func));
      return (idx <= func->nupvalues) ? &func->upvalue[idx-1]
                                      : &G(L)->nilvalue;
    }
    else {  /* light C function or Lua function (through a hook)?) */// 如果不是c闭包，返回nil
      api_check(L, ttislcf(s2v(ci->func)), "caller not a C function");
      return &G(L)->nilvalue;  /* no upvalues */
    }
  }
}

/*
** Convert a valid actual index (not a pseudo-index) to its address.
*/

/// @brief 获取索引对应的栈位置
/// @param L 
/// @param idx 
/// @return 
l_sinline StkId index2stack (lua_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    StkId o = ci->func + idx;
    api_check(L, o < L->top, "invalid index");
    return o;
  }
  else {    /* non-positive index */
    api_check(L, idx != 0 && -idx <= L->top - (ci->func + 1), "invalid index");
    api_check(L, !ispseudo(idx), "invalid index");
    return L->top + idx;
  }
}

/// @brief
/* 初始化分配栈大小的时候是分配了40个StkId，栈头部留5个buf空间，所以栈头部是35个
 * 检查lua_State的大小，如果栈小了，则扩容（默认栈大小：栈的默认尺寸是35）
 * 说明：只会不断扩容，不会缩小
 * 32/64位机器栈最大：1000000 100万个 LUAI_MAXSTACK决定的大小
 * 16位机器栈最大：15000
 */
/// @param L 
/// @param n 
/// @return 
LUA_API int lua_checkstack (lua_State *L, int n) {
  int res;
  CallInfo *ci;
  lua_lock(L);
  ci = L->ci;
  api_check(L, n >= 0, "negative 'n'");
  if (L->stack_last - L->top > n)  /* stack large enough? */
    res = 1;  /* yes; check is OK */
  else {  /* no; need to grow stack */
    int inuse = cast_int(L->top - L->stack) + EXTRA_STACK;
    if (inuse > LUAI_MAXSTACK - n)  /* can grow without overflow? */
      res = 0;  /* no */
    else  /* try to grow stack */
      res = luaD_growstack(L, n, 0);
  }
  if (res && ci->top < L->top + n)
    ci->top = L->top + n;  /* adjust frame top */
  lua_unlock(L);
  return res;
}

/// @brief 将一个堆栈上的从栈顶起的n个元素 移到另一个堆栈上
/// @param from 
/// @param to 
/// @param n 
/// @return 
LUA_API void lua_xmove (lua_State *from, lua_State *to, int n) {
  int i;
  if (from == to) return;
  lua_lock(to);
  api_checknelems(from, n);//检查from中是否有足够n个的元素
  api_check(from, G(from) == G(to), "moving among independent states");//检查from和to是否同属于一个global_state
  api_check(from, to->ci->top - to->top >= n, "stack overflow");//检查to的栈空间是否足够
  from->top -= n;
  for (i = 0; i < n; i++) {
    setobjs2s(to, to->top, from->top + i);//逐个元素进行拷贝
    to->top++;  /* stack already checked by previous 'api_check' */
  }
  lua_unlock(to);
}

/// @brief 设置异常处理方法
/// @param L 
/// @param panicf 
/// @return 
LUA_API lua_CFunction lua_atpanic (lua_State *L, lua_CFunction panicf) {
  lua_CFunction old;
  lua_lock(L);
  old = G(L)->panic;
  G(L)->panic = panicf;
  lua_unlock(L);
  return old;
}

/// @brief 获取lua版本
/// @param L 
/// @return 
LUA_API lua_Number lua_version (lua_State *L) {
  UNUSED(L);
  return LUA_VERSION_NUM;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
*/

/// @brief 获取栈的绝对索引
/// @param L 
/// @param idx 
/// @return 
LUA_API int lua_absindex (lua_State *L, int idx) {
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->top - L->ci->func) + idx;
}

/// @brief 返回栈中元素个数（返回栈顶索引）
/// @param L 
/// @return 
LUA_API int lua_gettop (lua_State *L) {
  return cast_int(L->top - (L->ci->func + 1));
}

/// @brief 设置栈的高度。如果开始的栈顶高于新的栈顶，顶部的值被丢弃
/// 否则，为了得到指定的大小这个函数压入相应个数的空值（nil）到栈上。
/// 特别的，lua_settop(L,0)清空堆栈。
/// @param L 
/// @param idx 
/// @return 
LUA_API void lua_settop (lua_State *L, int idx) {
  CallInfo *ci;
  StkId func, newtop;
  ptrdiff_t diff;  /* difference for new top */
  lua_lock(L);
  ci = L->ci;
  func = ci->func;
  if (idx >= 0) {
    api_check(L, idx <= ci->top - (func + 1), "new top too large");
    diff = ((func + 1) + idx) - L->top;
    for (; diff > 0; diff--)
      setnilvalue(s2v(L->top++));  /* clear new slots */
  }
  else {
    api_check(L, -(idx+1) <= (L->top - (func + 1)), "invalid new top");
    diff = idx + 1;  /* will "subtract" index (as it is negative) */
  }
  api_check(L, L->tbclist < L->top, "previous pop of an unclosed slot");
  newtop = L->top + diff;
  if (diff < 0 && L->tbclist >= newtop) {
    lua_assert(hastocloseCfunc(ci->nresults));
    luaF_close(L, newtop, CLOSEKTOP, 0);
  }
  L->top = newtop;  /* correct top only after closing any upvalue */
  lua_unlock(L);
}

/// @brief 根据给定索引得到对应的栈位置然后调用luaF_close函数删除upvalues和to-be-closed类型的元素，并将其值设置为 nil
/// @param L 
/// @param idx 
/// @return 
LUA_API void lua_closeslot (lua_State *L, int idx) {
  StkId level;
  lua_lock(L);
  level = index2stack(L, idx);
  api_check(L, hastocloseCfunc(L->ci->nresults) && L->tbclist == level,
     "no variable to close at given level");
  luaF_close(L, level, CLOSEKTOP, 0);
  level = index2stack(L, idx);  /* stack may be moved */
  setnilvalue(s2v(level));
  lua_unlock(L);
}


/*
** Reverse the stack segment from 'from' to 'to'
** (auxiliary to 'lua_rotate')
** Note that we move(copy) only the value inside the stack.
** (We do not move additional fields that may exist.)
*/

/// @brief 把栈中索引从from到to之间的数据调转
/// @param L 
/// @param from 
/// @param to 
/// @return 
l_sinline void reverse (lua_State *L, StkId from, StkId to) {
  for (; from < to; from++, to--) {
    TValue temp;
    setobj(L, &temp, s2v(from));
    setobjs2s(L, from, to);
    setobj2s(L, to, &temp);
  }
}

/*
** Let x = AB, where A is a prefix of length 'n'. Then,
** rotate x n == BA. But BA == (A^r . B^r)^r.
*/

/// @brief 将指定索引的元素向栈顶转动n个位置。若n为正数，表示将元素向栈顶方向转动，而n为负数则表示向相反的方向转动
/// @param L 
/// @param idx 
/// @param n 
/// @return 
LUA_API void lua_rotate (lua_State *L, int idx, int n) {
  StkId p, t, m;
  lua_lock(L);
  t = L->top - 1;  /* end of stack segment being rotated */
  p = index2stack(L, idx);  /* start of segment */
  api_check(L, (n >= 0 ? n : -n) <= (t - p + 1), "invalid 'n'");
  m = (n >= 0 ? t - n : p - n - 1);  /* end of prefix */
  reverse(L, p, m);  /* reverse the prefix with length 'n' */
  reverse(L, m + 1, t);  /* reverse the suffix */
  reverse(L, p, t);  /* reverse the entire segment */
  lua_unlock(L);
}

/// @brief 将索引 fromidx 处的元素复制到有效索引 toidx 中，替换该位置处的值。其他位置的值不受影响。
/// @param L 
/// @param fromidx 
/// @param toidx 
/// @return 
LUA_API void lua_copy (lua_State *L, int fromidx, int toidx) {
  TValue *fr, *to;
  lua_lock(L);
  fr = index2value(L, fromidx);
  to = index2value(L, toidx);
  api_check(L, isvalid(L, to), "invalid index");
  setobj(L, to, fr);
  if (isupvalue(toidx))  /* function upvalue? */
    luaC_barrier(L, clCvalue(s2v(L->ci->func)), fr);
  /* LUA_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
  lua_unlock(L);
}

/// @brief 把栈中指定索引的数据拷贝到栈顶
/// 例子 栈的初始状态为10 20 30 40 50 *（从栈底到栈顶，“*”标识为栈顶）有
///  lua_pushvalue(L, 3)    --> 10 20 30 40 50 30*
///  lua_pushvalue(L,3)是取得原来栈中的第三个元素，压到栈顶
/// @param L 
/// @param idx 
/// @return 
LUA_API void lua_pushvalue (lua_State *L, int idx) {
  lua_lock(L);
  setobj2s(L, L->top, index2value(L, idx));
  api_incr_top(L);
  lua_unlock(L);
}

/*
** access functions (stack -> C)
*/

/// @brief 获取指定索引位置的数据类型
/// @param L 
/// @param idx 
/// @return 
LUA_API int lua_type (lua_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (isvalid(L, o) ? ttype(o) : LUA_TNONE);
}

/// @brief 获取类型的名字
/// @param L 
/// @param t 
/// @return 
LUA_API const char *lua_typename (lua_State *L, int t) {
  UNUSED(L);
  api_check(L, LUA_TNONE <= t && t < LUA_NUMTYPES, "invalid type");
  return ttypename(t);
}

/// @brief 指定索引处的数据是不是c函数
/// @param L 
/// @param idx 
/// @return 
LUA_API int lua_iscfunction (lua_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}

/// @brief 指定索引处的数据是不是整数
/// @param L 
/// @param idx 
/// @return 
LUA_API int lua_isinteger (lua_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return ttisinteger(o);
}

/// @brief 指定索引处的数据是不是number
/// @param L 
/// @param idx 
/// @return 
LUA_API int lua_isnumber (lua_State *L, int idx) {
  lua_Number n;
  const TValue *o = index2value(L, idx);
  return tonumber(o, &n);
}

/// @brief 指定索引处的数据是不是string
/// @param L 
/// @param idx 
/// @return 
LUA_API int lua_isstring (lua_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttisstring(o) || cvt2str(o));
}

/// @brief 指定索引处的数据是不是userdata
/// @param L 
/// @param idx 
/// @return 
LUA_API int lua_isuserdata (lua_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttisfulluserdata(o) || ttislightuserdata(o));
}

/// @brief 如果两个索引 index1 和 index2 处的值简单地相等（不调用元方法）则返回 1 。否则返回 0 。如果任何一个索引无效也返回 0 。
/// @param L 
/// @param index1 
/// @param index2 
/// @return 
LUA_API int lua_rawequal (lua_State *L, int index1, int index2) {
  const TValue *o1 = index2value(L, index1);
  const TValue *o2 = index2value(L, index2);
  return (isvalid(L, o1) && isvalid(L, o2)) ? luaV_rawequalobj(o1, o2) : 0;
}

/// @brief 对堆栈顶部的两个值(如果是否定值,则为一个)进行算术或位运算,顶部的值为第二个操作数,弹出这些值,并推送运算结果。该函数遵循相应的Lua操作符的语义(也就是说,它可以调用元方法)。
// op 的值必须是以下常量之一：
// LUA_OPADD ：执行加法（ + ）
// LUA_OPSUB ：执行减法（ - ）
// LUA_OPMUL ：执行乘法（ * ）
// LUA_OPDIV ：执行浮点除法（ / ）
// LUA_OPIDIV ：执行楼层划分（ // ）
// LUA_OPMOD ：执行模（ % ）
// LUA_OPPOW ：执行求幂（ ^ ）
// LUA_OPUNM ：执行数学求反（一元 - ）
// LUA_OPBNOT ：执行按位非（ ~ ）
// LUA_OPBAND ：执行按位AND（ & ）
// LUA_OPBOR ：执行按位或（ | ）
// LUA_OPBXOR ：执行按位异或（ ~ ）
// LUA_OPSHL ：执行左移（ << ）
// LUA_OPSHR ：执行右移（ >> ）
/// @param L 
/// @param op 
/// @return 
LUA_API void lua_arith (lua_State *L, int op) {
  lua_lock(L);
  if (op != LUA_OPUNM && op != LUA_OPBNOT)
    api_checknelems(L, 2);  /* all other operations expect two operands */
  else {  /* for unary operations, add fake 2nd operand */
    api_checknelems(L, 1);
    setobjs2s(L, L->top, L->top - 1);
    api_incr_top(L);
  }
  /* first operand at top - 2, second at top - 1; result go to top - 2 */
  luaO_arith(L, op, s2v(L->top - 2), s2v(L->top - 1), L->top - 2);
  L->top--;  /* remove second operand */
  lua_unlock(L);
}

/// @brief 比较两个Lua值。如果遵循相应的Lua运算符的语义（即它可能称为元方法），则当索引 index1 的值与索引 index2 的值满足 op 时，返回1 。否则返回0。如果任何索引无效，也返回0。
// op 的值必须是以下常量之一：
// LUA_OPEQ ：比较是否相等（ == ）
// LUA_OPLT ：比较小于（ < ）
// LUA_OPLE ：比较小于或等于（ <= ）
/// @param L 
/// @param index1 
/// @param index2 
/// @param op 
/// @return 
LUA_API int lua_compare (lua_State *L, int index1, int index2, int op) {
  const TValue *o1;
  const TValue *o2;
  int i = 0;
  lua_lock(L);  /* may call tag method */
  o1 = index2value(L, index1);
  o2 = index2value(L, index2);
  if (isvalid(L, o1) && isvalid(L, o2)) {
    switch (op) {
      case LUA_OPEQ: i = luaV_equalobj(L, o1, o2); break;
      case LUA_OPLT: i = luaV_lessthan(L, o1, o2); break;
      case LUA_OPLE: i = luaV_lessequal(L, o1, o2); break;
      default: api_check(L, 0, "invalid option");
    }
  }
  lua_unlock(L);
  return i;
}

/// @brief 将以零结尾的字符串 s 转换为数字，将该数字压入堆栈，并返回字符串的总大小，即长度加一。
/// 根据 Lua 的词汇约定（参见§3.1），转换可以产生整数或浮点数。字符串可能有前导和尾随空格以及一个符号。
/// 如果字符串不是有效数字，则返回 0 并且不推送任何内容。（请注意，结果可以用作布尔值，如果转换成功，则为 true。）
/// @param L 
/// @param s 
/// @return 
LUA_API size_t lua_stringtonumber (lua_State *L, const char *s) {
  size_t sz = luaO_str2num(s, s2v(L->top));
  if (sz != 0)
    api_incr_top(L);
  return sz;
}


LUA_API lua_Number lua_tonumberx (lua_State *L, int idx, int *pisnum) {
  lua_Number n = 0;
  const TValue *o = index2value(L, idx);
  int isnum = tonumber(o, &n);
  if (pisnum)
    *pisnum = isnum;
  return n;
}

/// @brief 将给定索引处的 Lua 值转换为有符号整数类型 lua_Integer 。
/// Lua 值必须是整数，或者是可转换为整数的数字或字符串；否则， lua_tointegerx 返回 0。
/// 如果 isnum 不为 NULL ，则为其引用对象分配一个布尔值，该布尔值指示操作是否成功。
/// @param L 
/// @param idx 
/// @param pisnum 
/// @return 
LUA_API lua_Integer lua_tointegerx (lua_State *L, int idx, int *pisnum) {
  lua_Integer res = 0;
  const TValue *o = index2value(L, idx);
  int isnum = tointeger(o, &res);
  if (pisnum)
    *pisnum = isnum;
  return res;
}

/// @brief 将给定索引处的 Lua 值转换为 C 布尔值（0 或 1）。
/// 像 Lua 中的所有测试一样， lua_toboolean 对任何不同于false和nil的 Lua 值返回 true ；
/// 否则返回false。（如果您只想接受实际的布尔值，请使用 lua_isboolean 来测试值的类型。）
/// @param L 
/// @param idx 
/// @return 
LUA_API int lua_toboolean (lua_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return !l_isfalse(o);
}

/// @brief 将给定索引处的 Lua 值转换为 C 字符串。如果 len 不为 NULL ，则将 *len 设置为字符串长度。
/// Lua 值必须是字符串或数字；否则，该函数返回 NULL 。
/// 如果该值是一个数字，那么 lua_tolstring 也会将堆栈中的实际值更改为一个字符串。
/// 当 lua_tolstring 在表遍历期间应用于键时，此更改会混淆 lua_next 。
/// lua_tolstring 返回一个指向 Lua 状态内的字符串的指针。
/// 该字符串在其最后一个字符之后总是有一个零（' \0 '）（如在 C 中），但它的主体中可以包含其他零。
/// @param L 
/// @param idx 
/// @param len 
/// @return 
LUA_API const char *lua_tolstring (lua_State *L, int idx, size_t *len) {
  TValue *o;
  lua_lock(L);
  o = index2value(L, idx);
  if (!ttisstring(o)) {
    if (!cvt2str(o)) {  /* not convertible? */
      if (len != NULL) *len = 0;
      lua_unlock(L);
      return NULL;
    }
    luaO_tostring(L, o);
    luaC_checkGC(L);
    o = index2value(L, idx);  /* previous call may reallocate the stack */
  }
  if (len != NULL)
    *len = vslen(o);
  lua_unlock(L);
  return svalue(o);
}

/// @brief 返回给定索引处值的原始“长度”：对于字符串，这是字符串长度；
/// 对于字符串，这是字符串长度。对于表，这是没有元方法的长度运算符（' # '）的结果；
/// 对于用户数据，这是为用户数据分配的内存块的大小。
/// 对于其他值，此调用返回0。
/// @param L 
/// @param idx 
/// @return 
LUA_API lua_Unsigned lua_rawlen (lua_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  switch (ttypetag(o)) {
    case LUA_VSHRSTR: return tsvalue(o)->shrlen;
    case LUA_VLNGSTR: return tsvalue(o)->u.lnglen;
    case LUA_VUSERDATA: return uvalue(o)->len;
    case LUA_VTABLE: return luaH_getn(hvalue(o));
    default: return 0;
  }
}

/// @brief 将给定索引处的值转换为C函数。该值必须是C函数；否则，返回 NULL
/// @param L 
/// @param idx 
/// @return 
LUA_API lua_CFunction lua_tocfunction (lua_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->f;
  else return NULL;  /* not a C function */
}

/// @brief 如果给定索引处的值是完整的用户数据，则返回其内存块地址。
/// 如果该值是一个轻量级用户数据，则返回其值（一个指针）。否则，返回 NULL 。
/// @param o 
/// @return 
l_sinline void *touserdata (const TValue *o) {
  switch (ttype(o)) {
    case LUA_TUSERDATA: return getudatamem(uvalue(o));
    case LUA_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}

/// @brief 如果给定索引处的值是完整的用户数据，则返回其内存块地址。
/// 如果该值是一个轻量级用户数据，则返回其值（一个指针）。否则，返回 NULL 。
/// @param L 
/// @param idx 
/// @return 
LUA_API void *lua_touserdata (lua_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return touserdata(o);
}

/// @brief 将给定索引处的值转换为Lua线程（表示为 lua_State* ）。该值必须是线程；否则，该函数返回 NULL 。
/// @param L 
/// @param idx 
/// @return 
LUA_API lua_State *lua_tothread (lua_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (!ttisthread(o)) ? NULL : thvalue(o);
}


/*
** Returns a pointer to the internal representation of an object.
** Note that ANSI C does not allow the conversion of a pointer to
** function to a 'void*', so the conversion here goes through
** a 'size_t'. (As the returned pointer is only informative, this
** conversion should not be a problem.)
*/

/// @brief 将给定索引处的值转换为通用C指针（ void* ）。
/// 该值可以是一个用户数据，一个表，一个线程，一个字符串或一个函数
/// 否则， lua_topointer 返回 NULL 。不同的对象将给出不同的指针。无法将指针转换回其原始值。
/// 通常这个函数只用于散列和调试信息。
/// @param L 
/// @param idx 
/// @return 
LUA_API const void *lua_topointer (lua_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  switch (ttypetag(o)) {
    case LUA_VLCF: return cast_voidp(cast_sizet(fvalue(o)));
    case LUA_VUSERDATA: case LUA_VLIGHTUSERDATA:
      return touserdata(o);
    default: {
      if (iscollectable(o))
        return gcvalue(o);
      else
        return NULL;
    }
  }
}

/*
** push functions (C -> stack)
*/

/// @brief 将一个nil值推到堆栈上。
/// @param L 
/// @return 
LUA_API void lua_pushnil (lua_State *L) {
  lua_lock(L);
  setnilvalue(s2v(L->top));
  api_incr_top(L);
  lua_unlock(L);
}

/// @brief 将值 n 的浮点数压入堆栈
/// @param L 
/// @param n 
/// @return 
LUA_API void lua_pushnumber (lua_State *L, lua_Number n) {
  lua_lock(L);
  setfltvalue(s2v(L->top), n);
  api_incr_top(L);
  lua_unlock(L);
}

/// @brief 将值为 n 的整数压入堆栈。
/// @param L 
/// @param n 
/// @return 
LUA_API void lua_pushinteger (lua_State *L, lua_Integer n) {
  lua_lock(L);
  setivalue(s2v(L->top), n);
  api_incr_top(L);
  lua_unlock(L);
}


/*
** Pushes on the stack a string with given length. Avoid using 's' when
** 'len' == 0 (as 's' can be NULL in that case), due to later use of
** 'memcmp' and 'memcpy'.
*/

/// @brief 将大小为 len 的 s 指向的字符串压入堆栈。
/// Lua将创建或重用给定字符串的内部副本，因此可以在函数返回后立即释放或重用 s 处的内存。
/// 该字符串可以包含任何二进制数据，包括嵌入的零
/// 返回指向字符串内部副本的指针
/// @param L 
/// @param s 
/// @param len 
/// @return 
LUA_API const char *lua_pushlstring (lua_State *L, const char *s, size_t len) {
  TString *ts;
  lua_lock(L);
  ts = (len == 0) ? luaS_new(L, "") : luaS_newlstr(L, s, len);
  setsvalue2s(L, L->top, ts);
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return getstr(ts);
}

/// @brief 将 s 所指向的零终止字符串推入堆栈。Lua将创建或重用给定字符串的内部副本，因此可以在函数返回后立即释放或重用 s 处的内存。
/// 如果 s 为 NULL ，则推送nil并返回 NULL 。
/// @param L 
/// @param s 
/// @return 
LUA_API const char *lua_pushstring (lua_State *L, const char *s) {
  lua_lock(L);
  if (s == NULL)
    setnilvalue(s2v(L->top));
  else {
    TString *ts;
    ts = luaS_new(L, s);
    setsvalue2s(L, L->top, ts);
    s = getstr(ts);  /* internal copy's address */
  }
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return s;
}

/// @brief 等价于 lua_pushfstring ，除了它接收一个 va_list 而不是可变数量的参数。
/// @param L 
/// @param fmt 
/// @param argp 
/// @return 
LUA_API const char *lua_pushvfstring (lua_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  lua_lock(L);
  ret = luaO_pushvfstring(L, fmt, argp);
  luaC_checkGC(L);
  lua_unlock(L);
  return ret;
}

/// @brief 将格式化的字符串压入堆栈并返回指向该字符串的指针。
/// 它类似于 ISO C 函数 sprintf ，但有两个重要区别。
/// 首先，您不必为结果分配空间；结果是一个 Lua 字符串，Lua 负责内存分配（和释放，通过垃圾收集）。
/// 其次，转换说明符非常受限制。没有标志、宽度或精度。
/// 转换说明符只能是' %% '（插入字符' % '）、' %s '（插入一个以零结尾的字符串，没有大小限制）、' %f '（插入一个 lua_Number ）、' %I ' lua_Integer ）、' %p '（插入一个指针）、' %d '（插入一个 int ）、' %c '（插入一个 int 作为单字节字符）和' %U '（插入一个 long int 作为UTF-8 字节序列）。
/// 这个函数可能会因为内存溢出或无效的转换指定器而产生错误。
/// @param L 
/// @param fmt 
/// @param  
/// @return 
LUA_API const char *lua_pushfstring (lua_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;
  lua_lock(L);
  va_start(argp, fmt);
  ret = luaO_pushvfstring(L, fmt, argp);
  va_end(argp);
  luaC_checkGC(L);
  lua_unlock(L);
  return ret;
}

/// @brief 将新的 C 闭包推入堆栈。该函数接收一个指向 C 函数的指针，并将一个 Lua 类型的 function 值压入堆栈，该函数在调用时会调用相应的 C 函数。参数 n 告诉这个函数有多少上值
/// @param L 
/// @param fn 
/// @param n 
/// @return 
LUA_API void lua_pushcclosure (lua_State *L, lua_CFunction fn, int n) {
  lua_lock(L);
  if (n == 0) {
    setfvalue(s2v(L->top), fn);
    api_incr_top(L);
  }
  else {
    CClosure *cl;
    api_checknelems(L, n);
    api_check(L, n <= MAXUPVAL, "upvalue index too large");
    cl = luaF_newCclosure(L, n);
    cl->f = fn;
    L->top -= n;
    while (n--) {
      setobj2n(L, &cl->upvalue[n], s2v(L->top + n));
      /* does not need barrier because closure is white */
      lua_assert(iswhite(cl));
    }
    setclCvalue(L, s2v(L->top), cl);
    api_incr_top(L);
    luaC_checkGC(L);
  }
  lua_unlock(L);
}

/// @brief 将具有值 b 的布尔值压入堆栈。
/// @param L 
/// @param b 
/// @return 
LUA_API void lua_pushboolean (lua_State *L, int b) {
  lua_lock(L);
  if (b)
    setbtvalue(s2v(L->top));
  else
    setbfvalue(s2v(L->top));
  api_incr_top(L);
  lua_unlock(L);
}

/// @brief 将一个轻量级的用户数据推到堆栈上。
/// 用户数据表示Lua中的C值。
/// 用户数据代表一个指针，一个 void* 。
/// 它是一个值（如数字）：你不创建它，它没有单独的元表，也没有收集它（因为它从未创建过）。轻量用户数据等于具有相同C地址的“任何”轻量用户数据。
/// @param L 
/// @param p 
/// @return 
LUA_API void lua_pushlightuserdata (lua_State *L, void *p) {
  lua_lock(L);
  setpvalue(s2v(L->top), p);
  api_incr_top(L);
  lua_unlock(L);
}

/// @brief 将 L 表示的线程推入堆栈。如果此线程是其状态的主线程，则返回1。
/// @param L 
/// @return 
LUA_API int lua_pushthread (lua_State *L) {
  lua_lock(L);
  setthvalue(L, s2v(L->top), L);
  api_incr_top(L);
  lua_unlock(L);
  return (G(L)->mainthread == L);
}

/*
** get functions (Lua -> stack)
*/

/// @brief  获取table(t)中对应key(k)的value并入栈
/// 所以函数外，拿栈顶的内容就可以获取到字符串
/// @param L 
/// @param t 
/// @param k 
/// @return 
l_sinline int auxgetstr (lua_State *L, const TValue *t, const char *k) {
  const TValue *slot;
  TString *str = luaS_new(L, k);
  if (luaV_fastget(L, t, str, slot, luaH_getstr)) {
    setobj2s(L, L->top, slot);
    api_incr_top(L);
  }
  else {
    setsvalue2s(L, L->top, str);
    api_incr_top(L);
    luaV_finishget(L, t, s2v(L->top - 1), L->top - 1, slot);
  }
  lua_unlock(L);
  return ttype(s2v(L->top - 1));
}


/*
** Get the global table in the registry. Since all predefined
** indices in the registry were inserted right when the registry
** was created and never removed, they must always be in the array
** part of the registry.
*/

/// @brief 获取注册表中的全局表。由于注册表中的所有预定义索引都是在创建注册表时插入的，并且从未删除，因此它们必须始终位于注册表的数组中
#define getGtable(L)  \
	(&hvalue(&G(L)->l_registry)->array[LUA_RIDX_GLOBALS - 1])


/// @brief 将t[name] 元素push到栈顶, 其中t为全局表.
/// @param L 
/// @param name 
/// @return 
LUA_API int lua_getglobal (lua_State *L, const char *name) {
  const TValue *G;
  lua_lock(L);
  G = getGtable(L);
  return auxgetstr(L, G, name);
}

/// @brief 将t[k]元素push到栈顶. 其中t是index处的table,k为栈顶元素. 
// 这个函数可能触发index元方法. 
// 调用完成后弹出栈顶元素(key).
// lua_getglobal(L, "mytable") <== push mytable
// lua_pushnumber(L, 1)        <== push key 1
// lua_gettable(L, -2)         <== pop key 1, push mytable[1]
/// @param L 
/// @param idx 
/// @return 
LUA_API int lua_gettable (lua_State *L, int idx) {
  const TValue *slot;
  TValue *t;
  lua_lock(L);
  t = index2value(L, idx);
  if (luaV_fastget(L, t, s2v(L->top - 1), slot, luaH_get)) {
    setobj2s(L, L->top - 1, slot);
  }
  else
    luaV_finishget(L, t, s2v(L->top - 1), L->top - 1, slot);
  lua_unlock(L);
  return ttype(s2v(L->top - 1));
}

/// @brief 将t[k]元素push到栈顶. 其中t是index处的table.这个函数可能触发index元方法.
// lua_getglobal(L, "mytable") <== push mytable
// lua_getfield(L, -1, "x")    <== push mytable["x"]，作用同下面两行调用
// --lua_pushstring(L, "x")    <== push key "x"
// --lua_gettable(L,-2)        <== pop key "x", push mytable["x"]
/// @param L 
/// @param idx 
/// @param k 
/// @return 
LUA_API int lua_getfield (lua_State *L, int idx, const char *k) {
  lua_lock(L);
  return auxgetstr(L, index2value(L, idx), k);
}

/// @brief 获取idx对应的table中，key为整数n的value。与在 Lua 中一样，此函数可能会触发“索引”事件的元方法
// 获取的顺序：
//  *     1）判断数组部分，如果可以，从数组中拿
//  *     2）否则，以hash的方式从table中拿
//  * 如果都拿不到，操作元表获取
/// @param L 
/// @param idx 
/// @param n 
/// @return 
LUA_API int lua_geti (lua_State *L, int idx, lua_Integer n) {
  TValue *t;
  const TValue *slot;
  lua_lock(L);
  t = index2value(L, idx);
  if (luaV_fastgeti(L, t, n, slot)) {
    setobj2s(L, L->top, slot);
  }
  else {
    TValue aux;
    setivalue(&aux, n);
    luaV_finishget(L, t, &aux, L->top, slot);
  }
  api_incr_top(L);
  lua_unlock(L);
  return ttype(s2v(L->top - 1));
}

/// @brief 简单的将val入栈，如果val是nil，入栈一个nil
/// @param L 
/// @param val 
/// @return 
l_sinline int finishrawget (lua_State *L, const TValue *val) {
  if (isempty(val))  /* avoid copying empty items to the stack */
    setnilvalue(s2v(L->top));
  else
    setobj2s(L, L->top, val);
  api_incr_top(L);
  lua_unlock(L);
  return ttype(s2v(L->top - 1));
}

/// @brief  获取指定索引的值，只是这个值期待是table
/// @param L 
/// @param idx 
/// @return 
static Table *gettable (lua_State *L, int idx) {
  TValue *t = index2value(L, idx);
  api_check(L, ttistable(t), "table expected");
  return hvalue(t);
}

/// @brief 用法同lua_gettable,但更快(因为当key不存在时不用访问元方法 __index)
/// @param L 
/// @param idx 
/// @return 
LUA_API int lua_rawget (lua_State *L, int idx) {
  Table *t;
  const TValue *val;
  lua_lock(L);
  api_checknelems(L, 1);
  t = gettable(L, idx);
  val = luaH_get(t, s2v(L->top - 1));
  L->top--;  /* remove key */
  return finishrawget(L, val);
}

/// @brief 将t[n]元素push到栈顶.其中t是index处的table. 
// 这个函数不会触发index元方法. 
// lua_getglobal(L, "mytable") <== push mytable
// lua_rawgeti(L, -1, 1)       <== push mytable[1]，作用同下面两行调用
// --lua_pushnumber(L, 1)      <== push key 1
// --lua_rawget(L,-2)          <== pop key 1, push mytable[1]
/// @param L 
/// @param idx 
/// @param n 
/// @return 
LUA_API int lua_rawgeti (lua_State *L, int idx, lua_Integer n) {
  Table *t;
  lua_lock(L);
  t = gettable(L, idx);
  return finishrawget(L, luaH_getint(t, n));
}

/// @brief 将值 t[k] 入堆栈，其中 t 是给定索引处的表， k 是表示为轻量用户数据的指针 p 。访问是原始的；也就是说，它不使用 __index 元值。
/// lua_rawgetp() 函数与 lua_rawget() 函数类似，也是从在 Lua 栈里的表中获取给定键的值。但前者的键被特定为了指针。
/// 它的参数分别为：Lua 栈、要取出值的表在 Lua 栈中的索引、指针。它的返回值将被压入 Lua 栈，其值为该指针键对应的值
/// @param L 
/// @param idx 
/// @param p 
/// @return 
LUA_API int lua_rawgetp (lua_State *L, int idx, const void *p) {
  Table *t;
  TValue k;
  lua_lock(L);
  t = gettable(L, idx);
  setpvalue(&k, cast_voidp(p));
  return finishrawget(L, luaH_get(t, &k));
}

/// @brief 创建一个新的table并将之放在栈顶.narr是该table数组部分的长度,nrec是该table hash部分的长度. 
    // 当我们确切的知道要放多少元素到table的时候,使用这个函数,lua可以预分配一些内存,提升性能. 
    // 如果不确定要存放多少元素可以使用 lua_newtable 函数来创建table. 
/// @param L 
/// @param narray 
/// @param nrec 
/// @return 
LUA_API void lua_createtable (lua_State *L, int narray, int nrec) {
  Table *t;
  lua_lock(L);
  t = luaH_new(L);
  sethvalue2s(L, L->top, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    luaH_resize(L, t, narray, nrec);
  luaC_checkGC(L);
  lua_unlock(L);
}

/// @brief 将index处元素的元表push到栈顶. 如果该元素没有元表, 函数返回0 , 不改变栈.
/// @param L 
/// @param objindex 
/// @return 
LUA_API int lua_getmetatable (lua_State *L, int objindex) {
  const TValue *obj;
  Table *mt;
  int res = 0;
  lua_lock(L);
  obj = index2value(L, objindex);
  switch (ttype(obj)) {
    case LUA_TTABLE:
      mt = hvalue(obj)->metatable;
      break;
    case LUA_TUSERDATA:
      mt = uvalue(obj)->metatable;
      break;
    default:
      mt = G(L)->mt[ttype(obj)];
      break;
  }
  if (mt != NULL) {
    sethvalue2s(L, L->top, mt);
    api_incr_top(L);
    res = 1;
  }
  lua_unlock(L);
  return res;
}

/// @brief 在给定索引处将与完整用户数据关联的第 n 个用户值压入堆栈，并返回压入值的类型
/// 如果 userdata 没有该值，则压入nil并返回 LUA_TNONE 
/// @param L 
/// @param idx 
/// @param n 
/// @return 
LUA_API int lua_getiuservalue (lua_State *L, int idx, int n) {
  TValue *o;
  int t;
  lua_lock(L);
  o = index2value(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (n <= 0 || n > uvalue(o)->nuvalue) {
    setnilvalue(s2v(L->top));
    t = LUA_TNONE;
  }
  else {
    setobj2s(L, L->top, &uvalue(o)->uv[n - 1].uv);
    t = ttype(s2v(L->top));
  }
  api_incr_top(L);
  lua_unlock(L);
  return t;
}


/*
** set functions (stack -> Lua)
*/

/*
** t[k] = value at the top of the stack (where 'k' is a string)
*/

/// @brief 把栈顶的值设置到table[k]
/// @param L 
/// @param t 
/// @param k 
static void auxsetstr (lua_State *L, const TValue *t, const char *k) {
  const TValue *slot;
  TString *str = luaS_new(L, k);
  api_checknelems(L, 1);
  if (luaV_fastget(L, t, str, slot, luaH_getstr)) {
    luaV_finishfastset(L, t, slot, s2v(L->top - 1));
    L->top--;  /* pop value */
  }
  else {
    setsvalue2s(L, L->top, str);  /* push 'str' (to make it a TValue) */
    api_incr_top(L);
    luaV_finishset(L, t, s2v(L->top - 1), s2v(L->top - 2), slot);
    L->top -= 2;  /* pop value and key */
  }
  lua_unlock(L);  /* lock done by caller */
}

/// @brief 为table中的key赋值. t[name] = v . 其中t为全局表. v为栈顶元素. 
// 调用完成后弹出栈顶元素(v). 
/// @param L 
/// @param name 
/// @return 
LUA_API void lua_setglobal (lua_State *L, const char *name) {
  const TValue *G;
  lua_lock(L);  /* unlock done in 'auxsetstr' */
  G = getGtable(L);
  auxsetstr(L, G, name);
}

/// @brief 为table中的key赋值. t[k] = v . 其中t是index处的table , v为栈顶元素. k为-2处的元素. 
// 这个函数可能触发newindex元方法. 
// 调用完成后弹出栈顶两个元素(key , value) 
// lua_getglobal(L, "mytable") <== push mytable
// lua_pushnumber(L, 1)        <== push key 1
// lua_pushstring(L, "abc")    <== push value "abc"
// lua_settable(L, -3)         <== mytable[1] = "abc", pop key & value
/// @param L 
/// @param idx 
/// @return 
LUA_API void lua_settable (lua_State *L, int idx) {
  TValue *t;
  const TValue *slot;
  lua_lock(L);
  api_checknelems(L, 2);
  t = index2value(L, idx);
  if (luaV_fastget(L, t, s2v(L->top - 2), slot, luaH_get)) {
    luaV_finishfastset(L, t, slot, s2v(L->top - 1));
  }
  else
    luaV_finishset(L, t, s2v(L->top - 2), s2v(L->top - 1), slot);
  L->top -= 2;  /* pop index and value */
  lua_unlock(L);
}


/// @brief  为table中的key赋值. t[k] = v . 其中t是index处的table , v为栈顶元素. 
//  k: k
//  v: top[-1]
// 这个函数可能触发newindex元方法. 
// 调用完成后弹出栈顶元素(value).  
// lua_getglobal(L, "mytable") <== push mytable
// lua_pushstring(L, "abc")    <== push value "abc"
// lua_setfield(L, -2, "x")    <== mytable["x"] = "abc", pop value "abc"
/// @param L 
/// @param idx 
/// @param k 
/// @return 
LUA_API void lua_setfield (lua_State *L, int idx, const char *k) {
  lua_lock(L);  /* unlock done in 'auxsetstr' */
  auxsetstr(L, index2value(L, idx), k);
}

/// @brief  为table中的key赋值. t[n] = v . 其中t是index处的table , v为栈顶元素. 
//  k: n
//  v: top[-1]
// 这个函数可能触发newindex元方法. 
// 调用完成后弹出栈顶元素(value).  
// lua_getglobal(L, "mytable") <== push mytable
// lua_pushstring(L, "abc")    <== push value "abc"
// lua_setfield(L, -2, 3)    <== mytable[3] = "abc", pop value "abc"
/// @param L 
/// @param idx 
/// @param n 
/// @return 
LUA_API void lua_seti (lua_State *L, int idx, lua_Integer n) {
  TValue *t;
  const TValue *slot;
  lua_lock(L);      mnbvc
  api_checknelems(L, 1);
  t = index2value(L, idx);
  if (luaV_fastgeti(L, t, n, slot)) {
    luaV_finishfastset(L, t, slot, s2v(L->top - 1));
  }
  else {
    TValue aux;
    setivalue(&aux, n);
    luaV_finishset(L, t, &aux, s2v(L->top - 1), slot);
  }
  L->top--;  /* pop value */
  lua_unlock(L);
}

/// @brief 使用栈顶的值设置idx对应的table的key的值，n的作用仅仅是让这个方法帮忙把n个元素出栈(不操作元表)
/// @param L 
/// @param idx 
/// @param key 
/// @param n 
static void aux_rawset (lua_State *L, int idx, TValue *key, int n) {
  Table *t;
  lua_lock(L);
  api_checknelems(L, n);
  t = gettable(L, idx);
  luaH_set(L, t, key, s2v(L->top - 1));
  invalidateTMcache(t);
  luaC_barrierback(L, obj2gco(t), s2v(L->top - 1));
  L->top -= n;
  lua_unlock(L);
}

/// @brief 与lua_settable函数类似, 但是不会触发newindex元方法.
/// @param L 
/// @param idx 
/// @return 
LUA_API void lua_rawset (lua_State *L, int idx) {
  aux_rawset(L, idx, s2v(L->top - 2), 2);
}

/// @brief 为table中的key赋值. t[p] = v .其中t是index处的table , p是一个lightuserdata , v为栈顶元素. 
// 这个函数不会触发newindex元方法. 
// 调用完成后弹出栈顶元素.
/// @param L 
/// @param idx 
/// @param p 
/// @return 
LUA_API void lua_rawsetp (lua_State *L, int idx, const void *p) {
  TValue k;
  setpvalue(&k, cast_voidp(p));
  aux_rawset(L, idx, &k, 1);
}


/// @brief 为table中的key赋值. t[n] = v .其中t是index处的table , v为栈顶元素. 
//  k: n
//  v: top[-1]
// 这个函数不会触发newindex元方法. 
// 调用完成后弹出栈顶元素(value).  
// lua_getglobal(L, "mytable") <== push mytable
// lua_pushstring(L, "abc")    <== push value "abc"
// lua_setfield(L, -2, 3)    <== mytable[3] = "abc", pop value "abc"
/// @param L 
/// @param idx 
/// @param n 
/// @return 
LUA_API void lua_rawseti (lua_State *L, int idx, lua_Integer n) {
  Table *t;
  lua_lock(L);
  api_checknelems(L, 1);
  t = gettable(L, idx);
  luaH_setint(L, t, n, s2v(L->top - 1));
  luaC_barrierback(L, obj2gco(t), s2v(L->top - 1));
  L->top--;
  lua_unlock(L);
}

/// @brief 预先push一个元表table进入栈的L->top-1的位置，然后通过索引找到要关联元表的table，
/// 最后设置 table.metatable = 元表table，根据代码可以看出：只有table和userData 可以有原表
/// @param L 
/// @param objindex 
/// @return 
LUA_API int lua_setmetatable (lua_State *L, int objindex) {
  TValue *obj;
  Table *mt;
  lua_lock(L);
  api_checknelems(L, 1);
  obj = index2value(L, objindex);
  if (ttisnil(s2v(L->top - 1)))
    mt = NULL;
  else {
    api_check(L, ttistable(s2v(L->top - 1)), "table expected");
    mt = hvalue(s2v(L->top - 1));
  }
  switch (ttype(obj)) {
    case LUA_TTABLE: {
      hvalue(obj)->metatable = mt;
      if (mt) {
        luaC_objbarrier(L, gcvalue(obj), mt);
        luaC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    case LUA_TUSERDATA: {
      uvalue(obj)->metatable = mt;
      if (mt) {
        luaC_objbarrier(L, uvalue(obj), mt);
        luaC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    default: {
      G(L)->mt[ttype(obj)] = mt;
      break;
    }
  }
  L->top--;
  lua_unlock(L);
  return 1;
}


/// @brief 把整数n作为key，栈顶作为uservalue设置idx对应table的key-value 
/// 如果userdata没有该值，则返回0。
/// @param L 
/// @param idx 
/// @param n 
/// @return 
LUA_API int lua_setiuservalue (lua_State *L, int idx, int n) {
  TValue *o;
  int res;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2value(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (!(cast_uint(n) - 1u < cast_uint(uvalue(o)->nuvalue)))
    res = 0;  /* 'n' not in [1, uvalue(o)->nuvalue] */
  else {
    setobj(L, &uvalue(o)->uv[n - 1].uv, s2v(L->top - 1));
    luaC_barrierback(L, gcvalue(o), s2v(L->top - 1));
    res = 1;
  }
  L->top--;
  lua_unlock(L);
  return res;
}


/*
** 'load' and 'call' functions (run Lua code)
*/

/// @brief 检测 nargs:压入栈的参数个数 nresults:返回值的个数 合法性
#define checkresults(L,na,nr) \
     api_check(L, (nr) == LUA_MULTRET || (L->ci->top - L->top >= (nr) - (na)), \
	"results from function overflow current stack size")

/// @brief 在保护模式下调用一个函数(或一个可调用对象)
/// @param L 线程
/// @param nargs 目标函数的参数个数
/// @param nresults 目标函数返回值个数
/// @param ctx continuation-function 上下文环境
/// @param k continuation-function 函数指针
/// @return 
// continuation-function 是延续函数等价于下面的用法
// static int finishpcall(lua_State *L,int status,intptr_t ctx){
//      （void）ctx; /* 未使用的参数 */
//       status =(status != LUA_OK && status != LUA_YIELD);
//       lua_pushboolean（L，（status == 0））;/* 状态*/
//       lua_insert（L，1）;/* 状态是第一个结果*/ 
//       return lua_gettop（L）;/* 返回状态和所有结果 */
// }
// static int luaB_pcall(lua_State *L) { 
//       int status;
//       LuaL_checkany(L,1);
//       status = lua_pcallk(L,lua_gettop(L)-1,LUA_MULTRET,0, 0,finishpcall);
//       return finishpcall(L, status,0);
// }
LUA_API void lua_callk (lua_State *L, int nargs, int nresults,
                        lua_KContext ctx, lua_KFunction k) {
  StkId func;
  lua_lock(L);
  api_check(L, k == NULL || !isLua(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->top - (nargs+1);
  if (k != NULL && yieldable(L)) {  /* need to prepare continuation? */
    L->ci->u.c.k = k;  /* save continuation */
    L->ci->u.c.ctx = ctx;  /* save context */
    luaD_call(L, func, nresults);  /* do the call */
  }
  else  /* no continuation or no yieldable */
    luaD_callnoyield(L, func, nresults);  /* just do the call */
  adjustresults(L, nresults);
  lua_unlock(L);
}



/*
** Execute a protected call.
*/
struct CallS {  /* data to 'f_call' */
  StkId func;//* 要调用的函数位置 */
  int nresults;//函数返回值个数
};


static void f_call (lua_State *L, void *ud) {
  struct CallS *c = cast(struct CallS *, ud);
  luaD_callnoyield(L, c->func, c->nresults);
}


/// @brief 和lua_callk类似只是多了个errfunc
/// @param L 
/// @param nargs 
/// @param nresults 
/// @param errfunc 
/// @param ctx 
/// @param k 
/// @return 
LUA_API int lua_pcallk (lua_State *L, int nargs, int nresults, int errfunc,
                        lua_KContext ctx, lua_KFunction k) {
  struct CallS c;
  int status;
  ptrdiff_t func;
  lua_lock(L);
  api_check(L, k == NULL || !isLua(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = index2stack(L, errfunc);
    api_check(L, ttisfunction(s2v(o)), "error handler must be a function");
    func = savestack(L, o);
  }
  c.func = L->top - (nargs+1);  /* function to be called */
  if (k == NULL || !yieldable(L)) {  /* no continuation or no yieldable? */
    c.nresults = nresults;  /* do a 'conventional' protected call */
    status = luaD_pcall(L, f_call, &c, savestack(L, c.func), func);
  }
  else {  /* prepare continuation (call is already protected by 'resume') */
    CallInfo *ci = L->ci;
    ci->u.c.k = k;  /* save continuation */
    ci->u.c.ctx = ctx;  /* save context */
    /* save information for error recovery */
    ci->u2.funcidx = cast_int(savestack(L, c.func));
    ci->u.c.old_errfunc = L->errfunc;
    L->errfunc = func;
    setoah(ci->callstatus, L->allowhook);  /* save value of 'allowhook' */
    ci->callstatus |= CIST_YPCALL;  /* function can do error recovery */
    luaD_call(L, c.func, nresults);  /* do the call */
    ci->callstatus &= ~CIST_YPCALL;
    L->errfunc = ci->u.c.old_errfunc;
    status = LUA_OK;  /* if it is here, there were no errors */
  }
  adjustresults(L, nresults);
  lua_unlock(L);
  return status;
}


/// 加载一段 Lua 代码块，但不运行它。并设置加载字节码的upvalue[0]为全局寄存器， 如果没有错误， lua_load 把一个编译好的代码块作为一个 Lua 函数压到栈顶。 否则，压入错误消息
/// @param L 
/// @param reader  用来读取数据，比如 luaL_loadfilex 内部调用 lua_load 函数，reader 就是getF函数，其通过fread函数读取文件
/// @param data  是指向可选数据结构的指针，可以传递给reader函数
/// @param chunkname chunkname是一个字符串，标识了正在加载的块了名字
/// @param mode mode是一个字符串，指定如何编译数据块。可能取值为:"b"(二进制):该块是预编译的二进制块，加载速度比源块快。"t" (text): chunk是一个文本块，在执行前被编译成字节码。"bt" (both):数据块可以是二进制数据块也可以是文本数据块，函数首先尝试将其作为二进制数据块加载，如果加载失败，则尝试将其作为文本数据块加载。
/// @return 
LUA_API int lua_load (lua_State *L, lua_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  ZIO z;
  int status;
  lua_lock(L);
  if (!chunkname) chunkname = "?";
  luaZ_init(L, &z, reader, data);
  status = luaD_protectedparser(L, &z, chunkname, mode);
  if (status == LUA_OK) {  /* no errors? */
    LClosure *f = clLvalue(s2v(L->top - 1));  /* get newly created function */
    if (f->nupvalues >= 1) {  /* does it have an upvalue? */
      /* get global table from registry */
      const TValue *gt = getGtable(L);
      /* set global table as 1st upvalue of 'f' (may be LUA_ENV) */
      setobj(L, f->upvals[0]->v, gt);
      luaC_barrier(L, f->upvals[0], gt);
    }
  }
  lua_unlock(L);
  return status;
}

/// @brief 把函数导出成二进制代码块 。函数接收栈顶的 Lua 函数做参数，然后生成它的二进制代码块。
// 若被导出的东西被再次加载，加载的结果就相当于原来的函数。
// 当它在产生代码块的时候，lua_dump通过调用函数 writer（参见 lua_Writer ）来写入数据，后面的 data 参数会被传入 writer 。
// 如果 strip 为真，二进制代码块将不包含该函数的调试信息。
// 最后一次由 writer 的返回值将作为这个函数的返回值返回；0 表示没有错误。
// 该函数不会把 Lua 函数弹出堆栈。
// string扩展库中使用的例子
// static int writer (lua_State *L, const void* b, size_t size, void* B) {
//   (void)L;
//   luaL_addlstring((luaL_Buffer*) B, (const char *)b, size);
//   return 0;
// }
 
 
// static int str_dump (lua_State *L) {
//   luaL_Buffer b;
//   luaL_checktype(L, 1, LUA_TFUNCTION);
//   lua_settop(L, 1);
//   luaL_buffinit(L,&b);
//   if (lua_dump(L, writer, &b) != 0)
//     return luaL_error(L, "unable to dump given function");
//   luaL_pushresult(&b);
//   return 1;
// }

/// @param L 
/// @param writer 
/// @param data 
/// @param strip 
/// @return 
LUA_API int lua_dump (lua_State *L, lua_Writer writer, void *data, int strip) {
  int status;
  TValue *o;
  lua_lock(L);
  api_checknelems(L, 1);
  o = s2v(L->top - 1);
  if (isLfunction(o))
    status = luaU_dump(L, getproto(o), writer, data, strip);
  else
    status = 1;
  lua_unlock(L);
  return status;
}

/// @brief 返回线程 L 的状态。
// 对于普通线程，状态可以是 LUA_OK ,如果线程以错误完成 lua_resume 的执行，则可以是错误代码，如果线程被挂起，则可以是 LUA_YIELD 。
// 您只能在状态 LUA_OK 的线程中调用函数。您可以恢复状态 LUA_OK （启动新线程）或 LUA_YIELD （恢复线程）的线程。
/// @param L 
/// @return 
LUA_API int lua_status (lua_State *L) {
  return L->status;
}


/*
** Garbage-collection function
*/

/// @brief 控制垃圾收集器。
// 该功能根据参数 what 的值执行多项任务。对于需要额外参数的选项，它们会在选项之后列出。
// LUA_GCCOLLECT ：执行完整的垃圾收集周期。
// LUA_GCSTOP ：停止垃圾收集器。
// LUA_GCRESTART ：重新启动垃圾收集器。
// LUA_GCCOUNT ：返回Lua正在使用的当前内存量（以KB为单位）。
// LUA_GCCOUNTB ：返回将Lua当前使用的内存字节数除以1024的余数。
// LUA_GCSTEP (int stepsize) ：执行垃圾收集的增量步骤，对应于 stepsize Kbytes的分配。
// LUA_GCISRUNNING ：返回一个布尔值，该布尔值指示收集器是否正在运行（即未停止）。
// LUA_GCINC (int pause, int stepmul, stepsize)：使用给定参数将收集器更改为增量模式。返回之前的模式（ LUA_GCGEN 或 LUA_GCINC ）。
// LUA_GCGEN (int minormul, int majormul)：使用给定参数将收集器更改为分代模式。返回之前的模式（ LUA_GCGEN 或 LUA_GCINC ）。
/// @param L 
/// @param what 
/// @param  
/// @return 
LUA_API int lua_gc (lua_State *L, int what, ...) {
  va_list argp;
  int res = 0;
  global_State *g = G(L);
  if (g->gcstp & GCSTPGC)  /* internal stop? */
    return -1;  /* all options are invalid when stopped */
  lua_lock(L);
  va_start(argp, what);
  switch (what) {
    case LUA_GCSTOP: {
      g->gcstp = GCSTPUSR;  /* stopped by the user */
      break;
    }
    case LUA_GCRESTART: {
      luaE_setdebt(g, 0);
      g->gcstp = 0;  /* (GCSTPGC must be already zero here) */
      break;
    }
    case LUA_GCCOLLECT: {
      luaC_fullgc(L, 0);
      break;
    }
    case LUA_GCCOUNT: {
      /* GC values are expressed in Kbytes: #bytes/2^10 */
      res = cast_int(gettotalbytes(g) >> 10);
      break;
    }
    case LUA_GCCOUNTB: {
      res = cast_int(gettotalbytes(g) & 0x3ff);
      break;
    }
    case LUA_GCSTEP: {
      int data = va_arg(argp, int);
      l_mem debt = 1;  /* =1 to signal that it did an actual step */
      lu_byte oldstp = g->gcstp;
      g->gcstp = 0;  /* allow GC to run (GCSTPGC must be zero here) */
      if (data == 0) {
        luaE_setdebt(g, 0);  /* do a basic step */
        luaC_step(L);
      }
      else {  /* add 'data' to total debt */
        debt = cast(l_mem, data) * 1024 + g->GCdebt;
        luaE_setdebt(g, debt);
        luaC_checkGC(L);
      }
      g->gcstp = oldstp;  /* restore previous state */
      if (debt > 0 && g->gcstate == GCSpause)  /* end of cycle? */
        res = 1;  /* signal it */
      break;
    }
    case LUA_GCSETPAUSE: {
      int data = va_arg(argp, int);
      res = getgcparam(g->gcpause);
      setgcparam(g->gcpause, data);
      break;
    }
    case LUA_GCSETSTEPMUL: {
      int data = va_arg(argp, int);
      res = getgcparam(g->gcstepmul);
      setgcparam(g->gcstepmul, data);
      break;
    }
    case LUA_GCISRUNNING: {
      res = gcrunning(g);
      break;
    }
    case LUA_GCGEN: {
      int minormul = va_arg(argp, int);
      int majormul = va_arg(argp, int);
      res = isdecGCmodegen(g) ? LUA_GCGEN : LUA_GCINC;
      if (minormul != 0)
        g->genminormul = minormul;
      if (majormul != 0)
        setgcparam(g->genmajormul, majormul);
      luaC_changemode(L, KGC_GEN);
      break;
    }
    case LUA_GCINC: {
      int pause = va_arg(argp, int);
      int stepmul = va_arg(argp, int);
      int stepsize = va_arg(argp, int);
      res = isdecGCmodegen(g) ? LUA_GCGEN : LUA_GCINC;
      if (pause != 0)
        setgcparam(g->gcpause, pause);
      if (stepmul != 0)
        setgcparam(g->gcstepmul, stepmul);
      if (stepsize != 0)
        g->gcstepsize = stepsize;
      luaC_changemode(L, KGC_INC);
      break;
    }
    default: res = -1;  /* invalid option */
  }
  va_end(argp);
  lua_unlock(L);
  return res;
}



/*
** miscellaneous functions
*/

/// @brief 使用堆栈顶部的值作为错误对象引发 Lua 错误。这个函数做了一个长跳，因此永远不会返回
/// @param L 
/// @return 
LUA_API int lua_error (lua_State *L) {
  TValue *errobj;
  lua_lock(L);
  errobj = s2v(L->top - 1);
  api_checknelems(L, 1);
  /* error object is the memory error message? */
  if (ttisshrstring(errobj) && eqshrstr(tsvalue(errobj), G(L)->memerrmsg))
    luaM_error(L);  /* raise a memory error */
  else
    luaG_errormsg(L);  /* raise a regular error */
  /* code unreachable; will unlock when control actually leaves the kernel */
  return 0;  /* to avoid warnings */
}

/// @brief 该函数用来遍历一个table. 
// 从栈顶弹出一个key , 并且push一个 key-value对(栈顶key的下一个键值对) ,到栈顶. 
// 如果table中没有更多的元素, 函数返回0. 
// 遍历开始时栈顶为一个nil , 函数取出第一个键值对. 
  
// 通常遍历方法为: 
// lua_pushnil(L);  // first key 
// while (lua_next(L, t) != 0) { 
//     // uses 'key' (at index -2) and 'value' (at index -1) 
//     printf("%s - %s\n", 
//     lua_typename(L, lua_type(L, -2)), 
//     lua_typename(L, lua_type(L, -1))); 
//     // removes 'value'; keeps 'key' for next iteration  
//     lua_pop(L, 1); 
// } 
// 注意: 在遍历table的时候 ,除非明确的知道key为字符串,不要对栈上的key使用 lua_tolstring 函数 , 
// 因为这样有可能改变key的类型 , 影响下一次 lua_next调用. 

/// @param L 
/// @param idx 
/// @return 
LUA_API int lua_next (lua_State *L, int idx) {
  Table *t;
  int more;
  lua_lock(L);
  api_checknelems(L, 1);
  t = gettable(L, idx);
  more = luaH_next(L, t, L->top - 1);
  if (more) {
    api_incr_top(L);
  }
  else  /* no more elements */
    L->top -= 1;  /* remove key */
  lua_unlock(L);
  return more;
}

/// @brief 将堆栈中的给定索引标记为要关闭的“变量”
/// @param L 
/// @param idx 
/// @return 
LUA_API void lua_toclose (lua_State *L, int idx) {
  int nresults;
  StkId o;
  lua_lock(L);
  o = index2stack(L, idx);
  nresults = L->ci->nresults;
  api_check(L, L->tbclist < o, "given index below or equal a marked one");
  luaF_newtbcupval(L, o);  /* create new to-be-closed upvalue */
  if (!hastocloseCfunc(nresults))  /* function not marked yet? */
    L->ci->nresults = codeNresults(nresults);  /* mark it */
  lua_assert(hastocloseCfunc(L->ci->nresults));
  lua_unlock(L);
}

/// @brief 连接堆栈顶部的 n 个值,弹出它们,并将结果留在顶部。如果 n 为 1，则结果是堆栈上的单个值（即该函数什么都不做）；如果 n 为 0，则结​​果为空字符串
/// @param L 
/// @param n 
/// @return 
LUA_API void lua_concat (lua_State *L, int n) {
  lua_lock(L);
  api_checknelems(L, n);
  if (n > 0)
    luaV_concat(L, n);
  else {  /* nothing to concatenate */
    setsvalue2s(L, L->top, luaS_newlstr(L, "", 0));  /* push empty string */
    api_incr_top(L);
  }
  luaC_checkGC(L);
  lua_unlock(L);
}

/// @brief 获取index处元素#操作符的结果 , 放置在栈顶.
/// @param L 
/// @param idx 
/// @return 
LUA_API void lua_len (lua_State *L, int idx) {
  TValue *t;
  lua_lock(L);
  t = index2value(L, idx);
  luaV_objlen(L, L->top, t);
  api_incr_top(L);
  lua_unlock(L);
}


/// @brief 返回给定状态的内存分配函数。如果 ud 不为 NULL ，则Lua将在设置内存分配器函数时给出的不透明指针存储在 *ud 中。
/// @param L 
/// @param ud 
/// @return 
LUA_API lua_Alloc lua_getallocf (lua_State *L, void **ud) {
  lua_Alloc f;
  lua_lock(L);
  if (ud) *ud = G(L)->ud;
  f = G(L)->frealloc;
  lua_unlock(L);
  return f;
}

/// @brief 使用用户数据 ud 将给定状态的分配器功能更改为 f
/// @param L 
/// @param f 
/// @param ud 
/// @return 
LUA_API void lua_setallocf (lua_State *L, lua_Alloc f, void *ud) {
  lua_lock(L);
  G(L)->ud = ud;
  G(L)->frealloc = f;
  lua_unlock(L);
}

/// @brief 设置 Lua 使用的警告函数来发出警告（参见 lua_WarnFunction ）。 ud 参数设置传递给警告函数的值 ud 
/// @param L 
/// @param f 
/// @param ud 
void lua_setwarnf (lua_State *L, lua_WarnFunction f, void *ud) {
  lua_lock(L);
  G(L)->ud_warn = ud;
  G(L)->warnf = f;
  lua_unlock(L);
}

/// @brief 使用给定的消息发出警告。调用 tocont true的消息应在对该函数的另一个调用中继续。
/// @param L 
/// @param msg 
/// @param tocont 
void lua_warning (lua_State *L, const char *msg, int tocont) {
  lua_lock(L);
  luaE_warning(L, msg, tocont);
  lua_unlock(L);
}


/// @brief 该函数创建一个新的完整用户数据并将其压入堆栈，其中包含与 nuvalue 关联的 Lua 值，称为 user values 
/// ，以及一个关联的 size 字节的原始内存块。（用户值可以通过函数 lua_setiuservalue 和 lua_getiuservalue 设置和读取)
/// @param L 
/// @param size 
/// @param nuvalue 
/// @return 
LUA_API void *lua_newuserdatauv (lua_State *L, size_t size, int nuvalue) {
  Udata *u;
  lua_lock(L);
  api_check(L, 0 <= nuvalue && nuvalue < USHRT_MAX, "invalid value");
  u = luaS_newudata(L, size, nuvalue);
  setuvalue(L, s2v(L->top), u);
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return getudatamem(u);
}


/// @brief 
/// @param fi 指定索引的值
/// @param n 第几个upvalue
/// @param val 第 n 个 upvalue 指针的地址
/// @param owner GCObject对象
/// @return upvalue的名字
static const char *aux_upvalue(TValue *fi, int n, TValue **val,
                               GCObject **owner)
{
  switch (ttypetag(fi))
  {
  case LUA_VCCL:
  { /* C closure */
    CClosure *f = clCvalue(fi);
    if (!(cast_uint(n) - 1u < cast_uint(f->nupvalues)))
      return NULL; /* 'n' not in [1, f->nupvalues] */
    *val = &f->upvalue[n - 1];
    if (owner)
      *owner = obj2gco(f);
    return "";
  }
  case LUA_VLCL:
  { /* Lua closure */
    LClosure *f = clLvalue(fi);
    TString *name;
    Proto *p = f->p;
    if (!(cast_uint(n) - 1u < cast_uint(p->sizeupvalues)))
      return NULL; /* 'n' not in [1, p->sizeupvalues] */
    *val = f->upvals[n - 1]->v;
    if (owner)
      *owner = obj2gco(f->upvals[n - 1]);
    name = p->upvalues[n - 1].name;
    return (name == NULL) ? "(no name)" : getstr(name);
  }
  default:
    return NULL; /* not a closure */
  }
}

/// @brief 获取一个 closure 的 upvalue 信息。
// 对于 Lua 函数，upvalue 是函数需要使用的外部局部变量，因此这些变量被包含在 closure 中。lua_getupvalue 获取第n 个 upvalue ，把这个 upvalue 的值压入堆栈，并且返回它的名字。 
// funcindex 指向堆栈上 closure 的位置。（ 因为 upvalue 在整个函数中都有效，所以它们没有特别的次序。因此，它们以字母次序来编号。）
// 当索引号比 upvalue 数量大的时候，返回 NULL （而且不会压入任何东西）对于 C 函数，这个函数用空串"" 表示所有 upvalue 的名字。
/// @param L 
/// @param funcindex 
/// @param n 
/// @return 
LUA_API const char *lua_getupvalue(lua_State *L, int funcindex, int n)
{
  const char *name;
  TValue *val = NULL; /* to avoid warnings */
  lua_lock(L);
  name = aux_upvalue(index2value(L, funcindex), n, &val, NULL);
  if (name)
  {
    setobj2s(L, L->top, val);
    api_incr_top(L);
  }
  lua_unlock(L);
  return name;
}

/// @brief 设置 closure 的 upvalue 的值。它把栈顶的值弹出并赋于 upvalue 并返回 upvalue 的名字。参数 funcindex 与 n 和 lua_getupvalue 中的一样（参见 lua_getupvalue）。
// 当索引大于 upvalue 的个数时，返回 NULL （什么也不弹出）。
/// @param L 
/// @param funcindex 
/// @param n 
/// @return 
LUA_API const char *lua_setupvalue(lua_State *L, int funcindex, int n)
{
  const char *name;
  TValue *val = NULL;     /* to avoid warnings */
  GCObject *owner = NULL; /* to avoid warnings */
  TValue *fi;
  lua_lock(L);
  fi = index2value(L, funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner);
  if (name)
  {
    L->top--;
    setobj(L, val, s2v(L->top));
    luaC_barrier(L, owner, val);
  }
  lua_unlock(L);
  return name;
}

/// @brief 返回 upvalue 的地址
/// @param L 
/// @param fidx 闭包位置
/// @param n 第 n 个upvalue
/// @param pf 不为NULL时返回闭包地址
/// @return 返回 upvalue 的地址
static UpVal **getupvalref(lua_State *L, int fidx, int n, LClosure **pf)
{
  static const UpVal *const nullup = NULL;
  LClosure *f;
  TValue *fi = index2value(L, fidx);
  api_check(L, ttisLclosure(fi), "Lua function expected");
  f = clLvalue(fi);
  if (pf)
    *pf = f;
  if (1 <= n && n <= f->p->sizeupvalues)
    return &f->upvals[n - 1]; /* get its upvalue pointer */
  else
    return (UpVal **)&nullup;
}

/// @brief  实际上就是返回 upvalue 的地址, &f->upvalue[n - 1]
/// @param L 
/// @param fidx 闭包位置
/// @param n 第 n 个upvalue
/// @return 
LUA_API void *lua_upvalueid(lua_State *L, int fidx, int n)
{
  TValue *fi = index2value(L, fidx);
  switch (ttypetag(fi))
  {
  case LUA_VLCL:
  { /* lua closure */
    return *getupvalref(L, fidx, n, NULL);
  }
  case LUA_VCCL:
  { /* C closure */
    CClosure *f = clCvalue(fi);
    if (1 <= n && n <= f->nupvalues)
      return &f->upvalue[n - 1];
    /* else */
  } /* FALLTHROUGH */
  case LUA_VLCF:
    return NULL; /* light C functions have no upvalues */
  default:
  {
    api_check(L, 0, "function expected");
    return NULL;
  }
  }
}

/// @brief  让 Lua 闭包 f1 的第 n1 个上值 引用 Lua 闭包 f2 的第 n2 个上值
/// @param L 
/// @param fidx1 f1闭包位置
/// @param n1 n1 个上值
/// @param fidx2 f2闭包位置
/// @param n2 n2 个上值
/// @return 
LUA_API void lua_upvaluejoin(lua_State *L, int fidx1, int n1,
                             int fidx2, int n2)
{
  LClosure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, NULL);
  api_check(L, *up1 != NULL && *up2 != NULL, "invalid upvalue index");
  *up1 = *up2;
  luaC_objbarrier(L, f1, *up1);
}

