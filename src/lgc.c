/*
 * @文件作用: 垃圾回收
 * @功能分类: 虚拟机运转的核心功能
 * @注释者: frog-game
 * @LastEditTime: 2023-01-21 19:31:38
 */

/*
** $Id: lgc.c $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE

#include "lprefix.h"

#include <stdio.h>
#include <string.h>


#include "lua.h"

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


/*
** Maximum number of elements to sweep in each single step.
** (Large enough to dissipate fixed overheads but small enough
** to allow small steps for the collector.)
*/
#define GCSWEEPMAX	100 //每一步要扫描的最大元素数

/*
** Maximum number of finalizers to call in each single step.
*/
#define GCFINMAX	10 //每一步中调用的_gc数量


/*
** Cost of calling one finalizer.
*/
#define GCFINALIZECOST	50 //控制gc进度


/*
** The equivalent, in bytes, of one unit of "work" (visiting a slot,
** sweeping an object, etc.)
*/
#define WORK2MEM	sizeof(TValue) //通用数据类型大小


/*
** macro to adjust 'pause': 'pause' is actually used like
** 'pause / PAUSEADJ' (value chosen by tests)
*/
#define PAUSEADJ		100 //用来控制gc暂停


/* mask with all color bits */
// 所有的颜色为都标记上
#define maskcolors	(bitmask(BLACKBIT) | WHITEBITS) //其实就是二进制 111000 翻译过来就是白色黑色灰色都标识上了

/* mask with all GC bits */
// 所有的gc位为都标记上
#define maskgcbits      (maskcolors | AGEBITS) //其实就是二进制 111111 翻译过来就是白色黑色灰色年龄位都标识上了


/* macro to erase all color bits then set only the current white bit */
#define makewhite(g,x)	\
  (x->marked = cast_byte((x->marked & ~maskcolors) | luaC_white(g)))//擦除所有的颜色位,保留当前的白色位

/* make an object gray (neither white nor black) */
#define set2gray(x)	resetbits(x->marked, maskcolors)//设置成灰色 也就是3,4,5位都是0


/* make an object black (coming from any color) */
#define set2black(x)  \
  (x->marked = cast_byte((x->marked & ~WHITEBITS) | bitmask(BLACKBIT))) //设置成黑色 3,4位为0 第5位为1 


#define valiswhite(x)   (iscollectable(x) && iswhite(gcvalue(x))) //被回收的gc对象是不是白色

#define keyiswhite(n)   (keyiscollectable(n) && iswhite(gckey(n))) //被回收的键字段Key是不是白色


/*
** Protected access to objects in values
*/
#define gcvalueN(o)     (iscollectable(o) ? gcvalue(o) : NULL) //获取value 包含返回NULL


#define markvalue(g,o) { checkliveness(g->mainthread,o); \
  if (valiswhite(o)) reallymarkobject(g,gcvalue(o)); } //对value进行标记

#define markkey(g, n)	{ if keyiswhite(n) reallymarkobject(g,gckey(n)); }//对key进行标记

#define markobject(g,t)	{ if (iswhite(t)) reallymarkobject(g, obj2gco(t)); }//对object进行标记

/*
** mark an object that can be NULL (either because it is really optional,
** or it was stripped as debug info, or inside an uncompleted structure)
*/
// NULL 代表null的userdata结构
// 为什么会有NULL,如下解释
// lua_newtable(L);
// lua_pushlightuserdata(L, NULL);
// lua_setfield(L, -2, "null");
// 这样在实际调用时， setpvalue(L->top, p); 相当于 void *p = NULL， 最后是被封装到table变量里返回的。

#define markobjectN(g,t)	{ if (t) markobject(g,t); } //N:表示可能的t==NULL,对object进行标记

static void reallymarkobject (global_State *g, GCObject *o);//前置声明
static lu_mem atomic (lua_State *L);//前置声明
static void entersweep (lua_State *L);//前置声明


/*
** {======================================================
** Generic functions
** =======================================================
*/


/*
** one after last element in a hash array
*/
#define gnodelast(h)	gnode(h, cast_sizet(sizenode(h))) //hash数组中最后一个元素

/// @brief 通过GCObject类型获取gclist地址
/// @param o 
/// @return 
static GCObject **getgclist (GCObject *o) {
  switch (o->tt) {
    case LUA_VTABLE: return &gco2t(o)->gclist;
    case LUA_VLCL: return &gco2lcl(o)->gclist;
    case LUA_VCCL: return &gco2ccl(o)->gclist;
    case LUA_VTHREAD: return &gco2th(o)->gclist;
    case LUA_VPROTO: return &gco2p(o)->gclist;
    case LUA_VUSERDATA: {
      Udata *u = gco2u(o);
      lua_assert(u->nuvalue > 0);
      return &u->gclist;
    }
    default: lua_assert(0); return 0;
  }
}


/*
** Link a collectable object 'o' with a known type into the list 'p'.
** (Must be a macro to access the 'gclist' field in different types.)
*/

//将具有已知类型的可收集对象o链接到链表p中
//等价于(o)->gclist = p  p里面的内容是o
//个人感觉gclist命名改成gcnext命名更顺畅
#define linkgclist(o,p)	linkgclist_(obj2gco(o), &(o)->gclist, &(p))

/// @brief 将对象o连接到p链表上
/// @param o 
/// @param pnext 
/// @param list 
// 这个因为有二维指针，比较复杂，后面博客我上传示意图一看就明白了
static void linkgclist_ (GCObject *o, GCObject **pnext, GCObject **list) {
  lua_assert(!isgray(o));  /* cannot be in a gray list */// 不能是灰色链表
  *pnext = *list;
  *list = o;
  set2gray(o);  /* now it is *///把object改成灰色
}


/*
** Link a generic collectable object 'o' into the list 'p'.
*/
//将LUA_VTABLE,LUA_VLCL,LUA_VCCL,LUA_VTHREAD,LUA_VPROTO,LUA_VUSERDATA的类型对应的gclist链接到p中
#define linkobjgclist(o,p) linkgclist_(obj2gco(o), getgclist(o), &(p))



/*
** Clear keys for empty entries in tables. If entry is empty, mark its
** entry as dead. This allows the collection of the key, but keeps its
** entry in the table: its removal could break a chain and could break
** a table traversal.  Other places never manipulate dead keys, because
** its associated empty value is enough to signal that the entry is
** logically empty.
*/

/// @brief 将未使用的key remove掉
/// @param n 
static void clearkey (Node *n) {
  lua_assert(isempty(gval(n)));//n必须是nil类型
  if (keyiscollectable(n))//n必须是可回收类型
    setdeadkey(n);  /* unused key; remove it *///设置key死亡状态
}


/*
** tells whether a key or value can be cleared from a weak
** table. Non-collectable objects are never removed from weak
** tables. Strings behave as 'values', so are never removed too. for
** other objects: if really collected, cannot keep them; for objects
** being finalized, keep them in keys, but not in values
*/

/// @brief o 为 weak table 的 key 或 value, 判断其是否需要 clear 
// 只有拥有显示构造的对象类型会被自动从weak表中移除，值类型boolean、number是不会自动从weak中移除的。
// 而string类型虽然也由gc来负责清理，但是string没有显示的构造过程，因此也不会自动从weak表中移除，对于string的内存管理有单独的策略
/// @param g 
/// @param o 
/// @return 
static int iscleared (global_State *g, const GCObject *o) {
  if (o == NULL) return 0;  /* non-collectable value *///不能回收的值 
  else if (novariant(o->tt) == LUA_TSTRING) {
    markobject(g, o);  /* strings are 'values', so are never weak *///string 不会被移除
    return 0;
  }
  else return iswhite(o);//为白色表示没有其他对象引用它, 表示其可能需要 clear
}


/*
** Barrier that moves collector forward, that is, marks the white object
** 'v' being pointed by the black object 'o'.  In the generational
** mode, 'v' must also become old, if 'o' is old; however, it cannot
** be changed directly to OLD, because it may still point to non-old
** objects. So, it is marked as OLD0. In the next cycle it will become
** OLD1, and in the next it will finally become OLD (regular old). By
** then, any object it points to will also be old.  If called in the
** incremental sweep phase, it clears the black object to white (sweep
** it) to avoid other barrier calls for this same object. (That cannot
** be done is generational mode, as its sweep does not distinguish
** whites from deads.)
*/

/// @brief  向前设置barrier，把新建立联系的对象立刻标记 用来保证增量式GC在GC流程中暂停时，对象引用状态的改变不会引起GC流程产生错误的结果
/// @param L 
/// @param o 黑色 object
/// @param v 白色 object
void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v) {
  global_State *g = G(L);
  lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));//保持o是黑色,v是白色
  if (keepinvariant(g)) {  /* must keep invariant? *///如果是标记阶段
    reallymarkobject(g, v);  /* restore invariant *///对Object进行颜色标记
    if (isold(o)) {//如果黑色对象是旧对象
      lua_assert(!isold(v));  /* white object could not be old *///必须保证白色对象不是旧对象
      setage(v, G_OLD0);  /* restore generational invariant *///将白色对象设置成old0
    }
  }
  else {  /* sweep phase *///扫描阶段
    lua_assert(issweepphase(g));//验证一下是不是扫描阶段
    if (g->gckind == KGC_INC)  /* incremental mode? *///如果是增量gc
      makewhite(g, o);  /* mark 'o' as white to avoid other barriers *///将o标记位白色
  }
}


/*
** barrier that moves collector backward, that is, mark the black object
** pointing to a white object as gray again.
*/

/// @brief 向后设置barrier 将黑色对象再次标记为灰色。用来保证增量式GC在GC流程中暂停时，对象引用状态的改变不会引起GC流程产生错误的结果
/// @param L 
/// @param o 
void luaC_barrierback_ (lua_State *L, GCObject *o) {
  global_State *g = G(L);
  lua_assert(isblack(o) && !isdead(g, o));//o必须是黑色并且没有死亡
  lua_assert((g->gckind == KGC_GEN) == (isold(o) && getage(o) != G_TOUCHED1));
  if (getage(o) == G_TOUCHED2)  /* already in gray list? *///TOUCHED2年龄阶段
    set2gray(o);  /* make it gray to become touched1 *///设置成灰色
  else  /* link it in 'grayagain' and paint it gray *///将其链接到g->grayagain链表当中
    linkobjgclist(o, g->grayagain);
  if (isold(o))  /* generational mode? *///如果是旧的
    setage(o, G_TOUCHED1);  /* touched in current cycle *///设置成G_TOUCHED1年龄阶段
}

/// @brief 设置对象永远不被回收（因为标灰了）
/// @param L 
/// @param o 
void luaC_fix (lua_State *L, GCObject *o) {
  global_State *g = G(L);
  lua_assert(g->allgc == o);  /* object must be 1st in 'allgc' list! */
  set2gray(o);  /* they will be gray forever */
  setage(o, G_OLD);  /* and old forever */
  g->allgc = o->next;  /* remove object from 'allgc' list */
  o->next = g->fixedgc;  /* link it to 'fixedgc' list */
  g->fixedgc = o;
}


/*
** create a new collectable object (with given type and size) and link
** it to 'allgc' list.
*/

/// @brief 创建一个GCObject实例
/// @param L 
/// @param tt 
/// @param sz 
/// @return 
GCObject *luaC_newobj (lua_State *L, int tt, size_t sz) {
  global_State *g = G(L);
  GCObject *o = cast(GCObject *, luaM_newobject(L, novariant(tt), sz));
  o->marked = luaC_white(g);
  o->tt = tt;
  o->next = g->allgc;
  g->allgc = o;
  return o;
}

/* }====================================================== */



/*
** {======================================================
** Mark functions
** =======================================================
*/


/*
** Mark an object.  Userdata with no user values, strings, and closed
** upvalues are visited and turned black here.  Open upvalues are
** already indirectly linked through their respective threads in the
** 'twups' list, so they don't go to the gray list; nevertheless, they
** are kept gray to avoid barriers, as their values will be revisited
** by the thread or by 'remarkupvals'.  Other objects are added to the
** gray list to be visited (and turned black) later.  Both userdata and
** upvalues can call this function recursively, but this recursion goes
** for at most two levels: An upvalue cannot refer to another upvalue
** (only closures can), and a userdata's metatable must be a table.
*/

/// @brief 对对象进行标记
//将 userdata, string, closed upvalue 涂黑, 其它类型对象涂灰等待进一步处理
/// @param g 
/// @param o 
static void reallymarkobject (global_State *g, GCObject *o) {
  switch (o->tt) {
    case LUA_VSHRSTR://短串
    case LUA_VLNGSTR: {//长串
      set2black(o);  /* nothing to visit *///直接涂黑
      break;
    }
    case LUA_VUPVAL: {//上值
      UpVal *uv = gco2upv(o);//GCObject转换成上值
      if (upisopen(uv))//上值是不是open的
        set2gray(uv);  /* open upvalues are kept gray *///涂灰
      else
        set2black(uv);  /* closed upvalues are visited here *///涂黑
      markvalue(g, uv->v);  /* mark its content *///对内容进行标记
      break;
    }
    case LUA_VUSERDATA: {//userdata
      Udata *u = gco2u(o);//GCObject转换成userdata
      if (u->nuvalue == 0) {  /* no user values? *///没有用户自定义值
        markobjectN(g, u->metatable);  /* mark its metatable *///标记元表
        set2black(u);  /* nothing else to mark *///设置成黑色·
        break;
      }
      /* else... */
    }  /* FALLTHROUGH */
    case LUA_VLCL: case LUA_VCCL: case LUA_VTABLE:
    case LUA_VTHREAD: case LUA_VPROTO: {
      linkobjgclist(o, g->gray);  /* to be visited later *///丢灰色链表
      break;
    }
    default: lua_assert(0); break;//o->tt类型不对
  }
}


/*
** mark metamethods for basic types
*/

//标记基础类型的元表
static void markmt (global_State *g) {
  int i;
  for (i=0; i < LUA_NUMTAGS; i++)
    markobjectN(g, g->mt[i]);
}


/*
** mark all objects in list of being-finalized
*/

/// @brief 遍历g->tobefnz链表中所有元素并标记（上一循环剩下的object）
/// @param g 
/// @return 
static lu_mem markbeingfnz (global_State *g) {
  GCObject *o;
  lu_mem count = 0;
  for (o = g->tobefnz; o != NULL; o = o->next) {
    count++;
    markobject(g, o);
  }
  return count;
}


/*
** For each non-marked thread, simulates a barrier between each open
** upvalue and its value. (If the thread is collected, the value will be
** assigned to the upvalue, but then it can be too late for the barrier
** to act. The "barrier" does not need to check colors: A non-marked
** thread must be young; upvalues cannot be older than their threads; so
** any visited upvalue must be young too.) Also removes the thread from
** the list, as it was already visited. Removes also threads with no
** upvalues, as they have nothing to be checked. (If the thread gets an
** upvalue later, it will be linked in the list again.)
*/

/// @brief 标记open状态的UpValue
/// @param g 
/// @return 完成工作量
static int remarkupvals (global_State *g) {
  lua_State *thread;
  lua_State **p = &g->twups; //得到twups首地址
  int work = 0;  /* estimate of how much work was done here */
  while ((thread = *p) != NULL) {
    work++;//工作量++
    if (!iswhite(thread) && thread->openupval != NULL)//如果是白色并且open状态的上值链表不是null
      p = &thread->twups;  /* keep marked thread with upvalues in the list *///获取下一个twups的首地址
    else {  /* thread is not marked or without upvalues */
      UpVal *uv;
      lua_assert(!isold(thread) || thread->openupval == NULL);
      *p = thread->twups;  /* remove thread from the list *///
      thread->twups = thread;  /* mark that it is out of list *///加上面哪一步就等价于将thread移除了
      for (uv = thread->openupval; uv != NULL; uv = uv->u.open.next) {//循环openupval链表
        lua_assert(getage(uv) <= getage(thread));//上值年龄要小于线程年龄
        work++;//工作量++
        if (!iswhite(uv)) {  /* upvalue already visited? *///如果不是白色
          lua_assert(upisopen(uv) && isgray(uv));
          markvalue(g, uv->v);  /* mark its value *///对uv->v进行标记
        }
      }
    }
  }
  return work;
}

/// @brief 清除灰色链表
/// @param g 
static void cleargraylists (global_State *g) {
  g->gray = g->grayagain = NULL;
  g->weak = g->allweak = g->ephemeron = NULL;
}


/*
** mark root set and reset all gray lists, to start a new collection
*/

/// @brief 标记根集并重置所有灰名单，以开始新集合
/// @param g 
static void restartcollection (global_State *g) {
  cleargraylists(g);
  markobject(g, g->mainthread);
  markvalue(g, &g->l_registry);
  markmt(g);
  markbeingfnz(g);  /* mark any finalizing object left from previous cycle */
}

/* }====================================================== */


/*
** {======================================================
** Traverse functions
** =======================================================
*/


/*
** Check whether object 'o' should be kept in the 'grayagain' list for
** post-processing by 'correctgraylist'. (It could put all old objects
** in the list and leave all the work to 'correctgraylist', but it is
** more efficient to avoid adding elements that will be removed.) Only
** TOUCHED1 objects need to be in the list. TOUCHED2 doesn't need to go
** back to a gray list, but then it must become OLD. (That is what
** 'correctgraylist' does when it finds a TOUCHED2 object.)
*/

/// @brief 如果是touched1状态把黑色对象link进灰色链表,否则如果是touched2状态改变他的年龄到G_OLD
/// @param g 
/// @param o 黑色对象
static void genlink (global_State *g, GCObject *o) {
  lua_assert(isblack(o));
  if (getage(o) == G_TOUCHED1) {  /* touched in this cycle? */// touched1状态
    linkobjgclist(o, g->grayagain);  /* link it back in 'grayagain' *///把object放到灰色链表
  }  /* everything else do not need to be linked back */
  else if (getage(o) == G_TOUCHED2)// touched2状态
    changeage(o, G_TOUCHED2, G_OLD);  /* advance age *///改变年龄从G_TOUCHED2到G_OLD
}


/*
** Traverse a table with weak values and link it to proper list. During
** propagate phase, keep it in 'grayagain' list, to be revisited in the
** atomic phase. In the atomic phase, if table has any white value,
** put it in 'weak' list, to be cleared.
*/

/// @brief 遍历strong key, weak value情况
//  若 gc 处在 GCSpropagate 阶段, 并且g->gray不为空 将 weak table 加入到 g->grayagain 链表中, 在 atomic phase 再次访问. 
//  否则按下面的规则添加到对应 list:
//  1. 若table数组部分中有元素, 并且是atomic phase阶段 加入到 g->weak list.
//     若table数组部分中没有元素 并且不是atomic phase阶段 加入到 g->grayagain list.
//  2. table hash部分
//    1) val is nil: 移除它
//    2) val is not nil: 标记 key, 若 value is 白色 (且不为不可回收对象), 
//    	   是atomic phase阶段 加入到 g->weak list
//         不是atomic phase阶段 加入到 g->grayagain list.
    
/// @param g 
/// @param h 
static void traverseweakvalue (global_State *g, Table *h) {
  Node *n, *limit = gnodelast(h);//得到最后一个元素
  /* if there is array part, assume it may have white values (it is not
     worth traversing it now just to check) */
  int hasclears = (h->alimit > 0);//是不是有元素
  for (n = gnode(h, 0); n < limit; n++) {  /* traverse hash part *///遍历hash元素
    if (isempty(gval(n)))  /* entry is nil? *///如果是nil值
      clearkey(n);  /* clear its key *///移除它
    else {
      lua_assert(!keyisnil(n));//key类型不能为空
      markkey(g, n);//标记
      if (!hasclears && iscleared(g, gcvalueN(gval(n))))  /* a white value? *///如果数组部分没有值,但是hash表中有白色的值
        hasclears = 1;  /* table will have to be cleared *///说明要清除
    }
  }
  if (g->gcstate == GCSatomic && hasclears)//如果是GCSatomic阶段并且有元素需要清除
    linkgclist(h, g->weak);  /* has to be cleared later *///放入弱链表
  else
    linkgclist(h, g->grayagain);  /* must retraverse it in atomic phase *///放入灰色链表 等到原子阶段遍历他
}


/*
** Traverse an ephemeron table and link it to proper list. Returns true
** iff any object was marked during this traversal (which implies that
** convergence has to continue). During propagation phase, keep table
** in 'grayagain' list, to be visited again in the atomic phase. In
** the atomic phase, if table has any white->white entry, it has to
** be revisited during ephemeron convergence (as that key may turn
** black). Otherwise, if it has any white key, table has to be cleared
** (in the atomic phase). In generational mode, some tables
** must be kept in some gray list for post-processing; this is done
** by 'genlink'.
*/

/// @brief 遍历weak key, strong value情况
// 1. 遍历数组如果有白色值就进行标记,并marked设置为true
// 2. 遍历hash部分,按倒序或者正序遍历进行标记,标记好以后进行如下操作
//   若 gc 处在 GCSpropagate: 把h放入g->grayagain中
//   否则按如下规则处理
//    val is nil:  移除它
//    white key-white value:  把h放入g->ephemeron
//    white key-marked value: 把h放入g->allweak
//    marked key-white value: 标记 value
//    其他:如果是touched1状态把黑色对象link进灰色链表,否则如果是touched2状态改变他的年龄到G_OLD
/// @param g 
/// @param h 
/// @param inv true:倒序遍历
/// @return 标记了任何 object 返回true, 否则 false.
static int traverseephemeron (global_State *g, Table *h, int inv) {
  int marked = 0;  /* true if an object is marked in this traversal *///如果在此遍历中标记了对象,那么就为true
  int hasclears = 0;  /* true if table has white keys *///如果有白色的值,那么就为true
  int hasww = 0;  /* true if table has entry "white-key -> white-value" *///如果table中有 白色的key->白色的value 那么就为true
  unsigned int i;
  unsigned int asize = luaH_realasize(h);//得到数组的真实长度
  unsigned int nsize = sizenode(h);//得到hash表的真实长度
  /* traverse array part */
  for (i = 0; i < asize; i++) {//遍历数组
    if (valiswhite(&h->array[i])) {//回收对象是白色
      marked = 1;//进行标记
      reallymarkobject(g, gcvalue(&h->array[i]));//标记
    }
  }
  /* traverse hash part; if 'inv', traverse descending
     (see 'convergeephemerons') */
  for (i = 0; i < nsize; i++) {//遍历hash
    Node *n = inv ? gnode(h, nsize - 1 - i) : gnode(h, i);//通过是倒序还是正序决定取最后一个还是第一个node
    if (isempty(gval(n)))  /* entry is nil? *///是nil类型
      clearkey(n);  /* clear its key *///清除它
    else if (iscleared(g, gckeyN(n))) {  /* key is not marked (yet)? *///能否移除
      hasclears = 1;  /* table must be cleared *///hasclears设置位true
      if (valiswhite(gval(n)))  /* value not marked yet? *///回收对象是白色
        hasww = 1;  /* white-white entry *///hasww设置位true
    }
    else if (valiswhite(gval(n))) {  /* value not marked yet? *///回收对象是白色
      marked = 1; //marked设置为1
      reallymarkobject(g, gcvalue(gval(n)));  /* mark it now *///标记value
    }
  }
  /* link table into proper list */
  ///将表链接到正确的列表
  if (g->gcstate == GCSpropagate)//传播阶段
    linkgclist(h, g->grayagain);  /* must retraverse it in atomic phase *///扔grayagain链表
  else if (hasww)  /* table has white->white entries? *///hasww位true
    linkgclist(h, g->ephemeron);  /* have to propagate again *///ephemeron链用途：如果键在后面的 atomic 阶段发现是有效的，则需 mark 其值
  else if (hasclears)  /* table has white keys? *///hasclears为true
    linkgclist(h, g->allweak);  /* may have to clean white keys *///键不可达，值可达，后期需要清理掉不可达的键
  else
    genlink(g, obj2gco(h));  /* check whether collector still needs to see it *///如果是touched1状态把黑色对象link进灰色链表,否则如果是touched2状态改变他的年龄到G_OLD
  return marked;
}


/// @brief 遍历strong key, strong value情况
//    1. 标记 数组部分
//       对value进行标记
//    2.hash 部分
//       value is nil: 移除它
//       value is not nil: 标记 key, 标记 value
/// @param g 
/// @param h 
static void traversestrongtable (global_State *g, Table *h) {
  Node *n, *limit = gnodelast(h);//得到最后一个元素
  unsigned int i;
  unsigned int asize = luaH_realasize(h);//得到数组的真实长度
  for (i = 0; i < asize; i++)  /* traverse array part *///遍历数组
    markvalue(g, &h->array[i]);//进行标记
  for (n = gnode(h, 0); n < limit; n++) {  /* traverse hash part *///遍历hash
    if (isempty(gval(n)))  /* entry is empty? *///如果是nil
      clearkey(n);  /* clear its key *///删除它
    else {
      lua_assert(!keyisnil(n));
      markkey(g, n);//标记key
      markvalue(g, gval(n));//标记Value
    }
  }
  genlink(g, obj2gco(h));
}

/// @brief 由下面的情况决定加入到哪个list
//  a. strong key, weak value: 
//    若 gc 处在 GCSpropagate 阶段, 并且g->gray不为空 将 weak table 加入到 g->grayagain 链表中, 在 atomic phase 再次访问. 
//    否则按下面的规则添加到对应 list:
//    1. 若table数组部分中有元素, 并且是atomic phase阶段 加入到 g->weak list.
//       若table数组部分中没有元素 并且不是atomic phase阶段 加入到 g->grayagain list.
//    2. table hash部分
//      1) val is nil: 移除它
//      2) val is not nil: 标记 key, 若 value is 白色 (且不为不可回收对象), 
//    	   是atomic phase阶段 加入到 g->weak list
//         不是atomic phase阶段 加入到 g->grayagain list. 

//  b. weak key, strong value: 
//    1. 遍历数组如果有白色值就进行标记,并marked设置为true
//     2. 遍历hash部分,按倒序或者正序遍历进行标记,标记好以后进行如下操作
//      若 gc 处在 GCSpropagate: 把h放入g->grayagain中
//      否则按如下规则处理
//        val is nil:  移除它
//        white key-white value:  把h放入g->ephemeron
//        white key-marked value: 把h放入g->allweak
//        marked key-white value: 标记 value
//        其他:如果是touched1状态把黑色对象link进灰色链表,否则如果是touched2状态改变他的年龄到G_OLD
//  c. weak key, weak value:
//    把h放入g->allweak
 
//  d. strong key, strong value: 
//    1. 标记 数组部分
//       对value进行标记
//    2. hash 部分
//       value is nil: 移除它
//       value is not nil: 标记 key, 标记 value
/// @param g 
/// @param h 
/// @return 返回工作单元数量
static lu_mem traversetable (global_State *g, Table *h) {
  const char *weakkey, *weakvalue;
  const TValue *mode = gfasttm(g, h->metatable, TM_MODE);//从元方法中获取弱表信息
  markobjectN(g, h->metatable);//对object标记
  if (mode && ttisstring(mode) &&  /* is there a weak mode? *///是weak mode
      (cast_void(weakkey = strchr(svalue(mode), 'k')),//得到key
       cast_void(weakvalue = strchr(svalue(mode), 'v')),//得到value
       (weakkey || weakvalue))) {  /* is really weak? *///如果有weakkey或者有weakvalue 或者两者存在
    if (!weakkey)  /* strong keys? *///strong key, weak value
      traverseweakvalue(g, h);//// 遍历strong key, strong value情况
    else if (!weakvalue)  /* strong values? *///weak key, strong value
      traverseephemeron(g, h, 0);// 遍历weak key, strong value情况
    else  /* all weak *///weak key, weak value
      linkgclist(h, g->allweak);  /* nothing to traverse now */
  }
  else  /* not weak *///strong key, strong value
    traversestrongtable(g, h);// 遍历strong key, strong value情况
  return 1 + h->alimit + 2 * allocsizenode(h);//返回工作单元数量
}

/// @brief 标记userdata: metatable, upvalues,
/// @param g 
/// @param u 
/// @return 返回工作单元数量
static int traverseudata (global_State *g, Udata *u) {
  int i;
  markobjectN(g, u->metatable);  /* mark its metatable *///标记元表
  for (i = 0; i < u->nuvalue; i++)
    markvalue(g, &u->uv[i].uv);//自定义上值进行标记
  genlink(g, obj2gco(u));//如果是touched1状态把黑色对象link进灰色链表,否则如果是touched2状态改变他的年龄到G_OLD
  return 1 + u->nuvalue;//返回工作单元数量
}


/*
** Traverse a prototype. (While a prototype is being build, its
** arrays can be larger than needed; the extra slots are filled with
** NULL, so the use of 'markobjectN')
*/

/// @brief 标记函数原型 source, constants (k), upvalues name, inested protos,locvar varname
/// @param g 
/// @param f 
/// @return 返回工作单元数量
static int traverseproto (global_State *g, Proto *f) {
  int i;
  markobjectN(g, f->source);//标记文件名
  for (i = 0; i < f->sizek; i++)  /* mark literals *///标记常量表
    markvalue(g, &f->k[i]);
  for (i = 0; i < f->sizeupvalues; i++)  /* mark upvalue names *///标记上值名字
    markobjectN(g, f->upvalues[i].name);
  for (i = 0; i < f->sizep; i++)  /* mark nested protos *///标记子函数表
    markobjectN(g, f->p[i]);
  for (i = 0; i < f->sizelocvars; i++)  /* mark local-variable names *///标记局部变量
    markobjectN(g, f->locvars[i].varname);
  return 1 + f->sizek + f->sizeupvalues + f->sizep + f->sizelocvars;//返回工作单元数量
}

/// @brief 标记 C闭包中所有的 upvalues 
/// @param g 
/// @param cl 
/// @return //返回工作单元数量
static int traverseCclosure (global_State *g, CClosure *cl) {
  int i;
  for (i = 0; i < cl->nupvalues; i++)  /* mark its upvalues */
    markvalue(g, &cl->upvalue[i]);//标记上值
  return 1 + cl->nupvalues;//返回工作单元数量
}

/*
** Traverse a Lua closure, marking its prototype and its upvalues.
** (Both can be NULL while closure is being created.)
*/

/// @brief 标记 Lua closure 引用的对象: proto, upvalues,
/// @param g 
/// @param cl 
/// @return //返回工作单元数量
static int traverseLclosure (global_State *g, LClosure *cl) {
  int i;
  markobjectN(g, cl->p);  /* mark its prototype *///标记函数原型
  for (i = 0; i < cl->nupvalues; i++) {  /* visit its upvalues */
    UpVal *uv = cl->upvals[i];
    markobjectN(g, uv);  /* mark upvalue *///标记上值
  }
  return 1 + cl->nupvalues;//返回工作单元数量
}


/*
** Traverse a thread, marking the elements in the stack up to its top
** and cleaning the rest of the stack in the final traversal. That
** ensures that the entire stack have valid (non-dead) objects.
** Threads have no barriers. In gen. mode, old threads must be visited
** at every cycle, because they might point to young objects.  In inc.
** mode, the thread can still be modified before the end of the cycle,
** and therefore it must be visited again in the atomic phase. To ensure
** these visits, threads must return to a gray list if they are not new
** (which can only happen in generational mode) or if the traverse is in
** the propagate phase (which can only happen in incremental mode).
*/

/// @brief 标记线程
/// @param g 
/// @param th 
/// @return //返回工作单元数量
static int traversethread (global_State *g, lua_State *th) {
  UpVal *uv;
  StkId o = th->stack;
  if (isold(th) || g->gcstate == GCSpropagate)//如果线程是old,gcstate是GCSpropagate状态
    linkgclist(th, g->grayagain);  /* insert into 'grayagain' list *///丢到g->grayagain列表
  if (o == NULL)
    return 1;  /* stack not completely built yet *///栈没有创建
  lua_assert(g->gcstate == GCSatomic ||
             th->openupval == NULL || isintwups(th));
  for (; o < th->top; o++)  /* mark live elements in the stack *///标记栈上所有有效元素
    markvalue(g, s2v(o));
  for (uv = th->openupval; uv != NULL; uv = uv->u.open.next)///标记openupval
    markobject(g, uv);  /* open upvalues cannot be collected */
  if (g->gcstate == GCSatomic) {  /* final traversal? */
    for (; o < th->stack_last + EXTRA_STACK; o++)//将栈上 free slot 置为 nil
      setnilvalue(s2v(o));  /* clear dead stack slice */
    /* 'remarkupvals' may have removed thread from 'twups' list */
    if (!isintwups(th) && th->openupval != NULL) {// 若 thread 上有 openupval, 则将其重新加入到 g->twups list 中
      th->twups = g->twups;  /* link it back to the list */
      g->twups = th;
    }
  }
  else if (!g->gcemergency)//没有紧急回收
    luaD_shrinkstack(th); /* do not change stack in emergency cycle *///栈进行合理收缩
  return 1 + stacksize(th);//返回工作单元数量
}


/*
** traverse one gray object, turning it to black.
*/

/// @brief 只 traverse 一个 gray object, 将其标记为 black, 并从 gray list 移除
/// @param g 
/// @return //返回工作单元数量
static lu_mem propagatemark (global_State *g) {
  GCObject *o = g->gray;
  nw2black(o);
  g->gray = *getgclist(o);  /* remove from 'gray' list *///返回下一个灰色对象,把当前的移除掉
  switch (o->tt) {
    case LUA_VTABLE: return traversetable(g, gco2t(o));
    case LUA_VUSERDATA: return traverseudata(g, gco2u(o));
    case LUA_VLCL: return traverseLclosure(g, gco2lcl(o));
    case LUA_VCCL: return traverseCclosure(g, gco2ccl(o));
    case LUA_VPROTO: return traverseproto(g, gco2p(o));
    case LUA_VTHREAD: return traversethread(g, gco2th(o));
    default: lua_assert(0); return 0;
  }
}

/// @brief 将gray链表的所有对象进行标记
// 遍历灰色链表的过程中，可能会有新增对象会被扫描过的Table对象引用，这个Table对象将会放在grayagain里，所以需要这个
/// @param g 
/// @return //返回工作单元数量
static lu_mem propagateall (global_State *g) {
  lu_mem tot = 0;
  while (g->gray)//对灰色列表进行标记
    tot += propagatemark(g);
  return tot;//返回工作单元数量
}


/*
** Traverse all ephemeron tables propagating marks from keys to values.
** Repeat until it converges, that is, nothing new is marked. 'dir'
** inverts the direction of the traversals, trying to speed up
** convergence on chains in the same table.
**
*/

/// @brief 不断遍历 weak table 的 ephemerons 链表, 直到一次遍历没有标记任何值为止.
//  此函数结束后键是否可达已最终确定，mark 掉其可达键所关联的值
/// @param g 
static void convergeephemerons (global_State *g) {
  int changed;
  int dir = 0;
  do {
    GCObject *w;
    GCObject *next = g->ephemeron;  /* get ephemeron list *///获取ephemeron链表
    g->ephemeron = NULL;  /* tables may return to this list when traversed */
    changed = 0;
    while ((w = next) != NULL) {  /* for each ephemeron table *///遍历ephemeron 里面存的table
      Table *h = gco2t(w);//强转为table
      next = h->gclist;  /* list is rebuilt during loop *///获取下一个table
      nw2black(h);  /* out of the list (for now) *///把table变黑
      if (traverseephemeron(g, h, dir)) {  /* marked some value? *///如果有值被标记了
        propagateall(g);  /* propagate changes *///将gray链表的所有对象进行标记
        changed = 1;  /* will have to revisit all ephemeron tables *///重新遍历ephemeron tables
      }
    }
    dir = !dir;  /* invert direction next time *///下次反转方向
  } while (changed);  /* repeat until no more changes *///直到没有变化了
}

/* }====================================================== */


/*
** {======================================================
** Sweep Functions
** =======================================================
*/


/*
** clear entries with unmarked keys from all weaktables in list 'l'
*/

/// @brief 清除列表l中所有弱表中带有未标记的Key
/// @param g 
/// @param l 
static void clearbykeys (global_State *g, GCObject *l) {
  for (; l; l = gco2t(l)->gclist) {//遍历l链表
    Table *h = gco2t(l); //得到table
    Node *limit = gnodelast(h);//获取hash数组最后一个元素
    Node *n;
    for (n = gnode(h, 0); n < limit; n++) {//遍历hash
      if (iscleared(g, gckeyN(n)))  /* unmarked key? *///如果key能移除
        setempty(gval(n));  /* remove entry *///移除它
      if (isempty(gval(n)))  /* is entry empty? *///如果n值是nil
        clearkey(n);  /* clear its key *///移除key
    }
  }
}


/*
** clear entries with unmarked values from all weaktables in list 'l' up
** to element 'f'
*/

/// @brief 清除列表 l 中所有弱表中带有未标记value 到元素 f
/// @param g 
/// @param l 
/// @param f 
static void clearbyvalues (global_State *g, GCObject *l, GCObject *f) {
  for (; l != f; l = gco2t(l)->gclist) {//遍历l到f的链表
    Table * h = gco2t(l);//转换成table
    Node *n, *limit = gnodelast(h);//获取hash数组最后一个元素
    unsigned int i;
    unsigned int asize = luaH_realasize(h);//得到数组的真实长度
    for (i = 0; i < asize; i++) {//遍历数组
      TValue *o = &h->array[i];
      if (iscleared(g, gcvalueN(o)))  /* value was collected? *///如果能被回收
        setempty(o);  /* remove entry *///移除
    }
    for (n = gnode(h, 0); n < limit; n++) {//遍历数组
      if (iscleared(g, gcvalueN(gval(n))))  /* unmarked value? *///值能被回收
        setempty(gval(n));  /* remove entry *///移除
      if (isempty(gval(n)))  /* is entry empty? *///值是nil
        clearkey(n);  /* clear its key *///清除key
    }
  }
}

/// @brief 释放上值
/// @param L 
/// @param uv 
static void freeupval (lua_State *L, UpVal *uv) {
  if (upisopen(uv))//上值是open状态
    luaF_unlinkupval(uv);//把当前UpVal从链表移除
  luaM_free(L, uv);//释放内存
}

/// @brief 释放对象
/// @param L 
/// @param o 
static void freeobj (lua_State *L, GCObject *o) {
  switch (o->tt) {
    case LUA_VPROTO://函数类型
      luaF_freeproto(L, gco2p(o)); //释放函数类型
      break;
    case LUA_VUPVAL:
      freeupval(L, gco2upv(o));//释放上值
      break;
    case LUA_VLCL: {//lua闭包
      LClosure *cl = gco2lcl(o);
      luaM_freemem(L, cl, sizeLclosure(cl->nupvalues));//释放lua闭包
      break;
    }
    case LUA_VCCL: {//c闭包
      CClosure *cl = gco2ccl(o);
      luaM_freemem(L, cl, sizeCclosure(cl->nupvalues));//释放c闭包
      break;
    }
    case LUA_VTABLE://表
      luaH_free(L, gco2t(o));//释放表
      break;
    case LUA_VTHREAD://线程
      luaE_freethread(L, gco2th(o));//释放线程
      break;
    case LUA_VUSERDATA: {//userdata
      Udata *u = gco2u(o);
      luaM_freemem(L, o, sizeudata(u->nuvalue, u->len));//释放userdata
      break;
    }
    case LUA_VSHRSTR: {//短字符串
      TString *ts = gco2ts(o);
      luaS_remove(L, ts);  /* remove it from hash table *///从hash进行移除
      luaM_freemem(L, ts, sizelstring(ts->shrlen));//释放段字符串
      break;
    }
    case LUA_VLNGSTR: {//长字符串
      TString *ts = gco2ts(o);
      luaM_freemem(L, ts, sizelstring(ts->u.lnglen));//释放长字符串
      break;
    }
    default: lua_assert(0);
  }
}


/*
** sweep at most 'countin' elements from a list of GCObjects erasing dead
** objects, where a dead object is one marked with the old (non current)
** white; change all non-dead objects back to white, preparing for next
** collection cycle. Return where to continue the traversal or NULL if
** list is finished. ('*countout' gets the number of elements traversed.)
*/

/// @brief 释放dead对象, 将非 dead 对象标记为 currentwhite 
/// @param L 
/// @param p 
/// @param countin 每步扫描最大数量
/// @param countout 遍历的元素数
/// @return 
static GCObject **sweeplist (lua_State *L, GCObject **p, int countin,
                             int *countout) {
  global_State *g = G(L);//获取全局状态机
  int ow = otherwhite(g);//非当前白
  int i;
  int white = luaC_white(g);  /* current white *///得到当前白
  for (i = 0; *p != NULL && i < countin; i++) {//循环遍历
    GCObject *curr = *p;//得到当前的GCObject
    int marked = curr->marked;
    if (isdeadm(ow, marked)) {  /* is 'curr' dead? *///如果是死亡状态
      *p = curr->next;  /* remove 'curr' from list *///移除当前列表
      freeobj(L, curr);  /* erase 'curr' *///进行释放
    }
    else {  /* change mark to 'white' */
      curr->marked = cast_byte((marked & ~maskgcbits) | white);//如果不是dead状态，就给标记处当前白
      p = &curr->next;  /* go to next element *///移动到下一个元素
    }
  }
  if (countout)
    *countout = i;  /* number of elements traversed *///遍历了多少个
  return (*p == NULL) ? NULL : p;//如果list遍历已经完成那就返回Null
}


/*
** sweep a list until a live object (or end of list)
*/

/// @brief  调用 sweeplist, sweep 过程中把非 dead 对象标记为 currentwhite 
/// @param L 
/// @param p 
/// @return 
static GCObject **sweeptolive (lua_State *L, GCObject **p) {
  GCObject **old = p;
  do {
    p = sweeplist(L, p, 1, NULL);
  } while (p == old);
  return p;
}

/* }====================================================== */


/*
** {======================================================
** Finalization
** =======================================================
*/

/*
** If possible, shrink string table.
*/

/// @brief 如果可能，收缩字符串表
/// @param L 
/// @param g 
static void checkSizes (lua_State *L, global_State *g) {
  if (!g->gcemergency) {//不是紧急回收
    if (g->strt.nuse < g->strt.size / 4) {  /* string table too big? *///字符串太大 里面的元素大于hash表的4分之一
      l_mem olddebt = g->GCdebt;//得到需要回收的量
      luaS_resize(L, g->strt.size / 2);
      g->GCestimate += g->GCdebt - olddebt;  /* correct estimate *///计算存活下来的数量
    }
  }
}


/*
** Get the next udata to be finalized from the 'tobefnz' list, and
** link it back into the 'allgc' list.
*/

/// @brief 将tobefnz链表中的对象放到allgc链表中
/// @param g 
/// @return 
static GCObject *udata2finalize (global_State *g) {
  GCObject *o = g->tobefnz;  /* get first element *///得到终结列表的第一个对象
  lua_assert(tofinalize(o));
  g->tobefnz = o->next;  /* remove it from 'tobefnz' list *///把对象移除g->tobefnz链表
  o->next = g->allgc;  /* return it to 'allgc' list *///放到g->allgc链表中
  g->allgc = o;//到了这一步那么o就放到g->allgc链表当中了
  resetbit(o->marked, FINALIZEDBIT);  /* object is "normal" again *///标记userdata不在被引用
  if (issweepphase(g))//扫描阶段
    makewhite(g, o);  /* "sweep" object *///擦除所有的颜色位,保留当前的白色位
  else if (getage(o) == G_OLD1)//年龄是G_OLD1
    g->firstold1 = o;  /* it is the first OLD1 object in the list *///设置成列表中的第一个 OLD1 对象
  return o;
}

/// @brief 对于可挂起的过程，会保存上下文环境和 cotinuation-function，然后使用 luaD_call 调用目标函数；对于不可挂起的过程，则使用 luaD_callnoyield 去调用目标函数
/// @param L 
/// @param ud 
static void dothecall (lua_State *L, void *ud) {
  UNUSED(ud);
  luaD_callnoyield(L, L->top - 2, 0);
}

/// @brief 每次调用一个需要回收的 userdata 的 gc 元方法
/// @param L 
static void GCTM (lua_State *L) {
  global_State *g = G(L);//获取全局状态机
  const TValue *tm;
  TValue v;
  lua_assert(!g->gcemergency);//不能是紧急回收状态
  setgcovalue(L, &v, udata2finalize(g));//将tobefnz链表中的对象放到allgc链表中
  tm = luaT_gettmbyobj(L, &v, TM_GC);//获取gc元方法
  if (!notm(tm)) {  /* is there a finalizer? *///不是nil
    int status;
    lu_byte oldah = L->allowhook;//暂存hook状态
    int oldgcstp  = g->gcstp;//暂存gcstep状态
    g->gcstp |= GCSTPGC;  /* avoid GC steps *///把GC停下来,好避开 GC steps 
    L->allowhook = 0;  /* stop debug hooks during GC metamethod *///在 GC 元方法期间停止debug hook
    setobj2s(L, L->top++, tm);  /* push finalizer... *///push gc 元方法
    setobj2s(L, L->top++, &v);  /* ... and its argument *///push 参数
    L->ci->callstatus |= CIST_FIN;  /* will run a finalizer *///调用状态增加CIST_FIN状态
    status = luaD_pcall(L, dothecall, NULL, savestack(L, L->top - 2), 0);//在调用不出错的情况下，和lua_call行为一模一样,其处理方法主要取决于第四个参数 errfunc, 如果errfunc为0，则lua_pcall直接把错误信息通过lua_pushstring压栈，然后返回；然后errfunc不为0，则自动调用（L, errfunc）(errmsg)，errmsg表示原始出错信息
    L->ci->callstatus &= ~CIST_FIN;  /* not running a finalizer anymore *///调用状态减去CIST_FIN状态
    L->allowhook = oldah;  /* restore hooks *///还原hook状态
    g->gcstp = oldgcstp;  /* restore state *///还原gcstp状态
    if (l_unlikely(status != LUA_OK)) {  /* error while running __gc? *///如果发生了错误
      luaE_warnerror(L, "__gc");//那么就告警
      L->top--;  /* pops error object *///弹出错误对象
    }
  }
}


/*
** Call a few finalizers
*/

/// @brief 调用最多n个准备终结的对象
/// @param L 
/// @param n 
/// @return 
static int runafewfinalizers (lua_State *L, int n) {
  global_State *g = G(L);
  int i;
  for (i = 0; i < n && g->tobefnz; i++)
    GCTM(L);  /* call one finalizer *///调用userdata的元方法
  return i;
}


/*
** call all pending finalizers
*/

/// @brief 调用所有的table或者userdata的"__gc"元方法
/// @param L 
static void callallpendingfinalizers (lua_State *L) {
  global_State *g = G(L);
  while (g->tobefnz)
    GCTM(L);
}


/*
** find last 'next' field in list 'p' list (to add elements in its end)
*/

/// @brief 在list p中找到最后一个字段的下一个位置，好在末尾进行元素添加
/// @param p 
/// @return 
static GCObject **findlast (GCObject **p) {
  while (*p != NULL)
    p = &(*p)->next;
  return p;
}


/*
** Move all unreachable objects (or 'all' objects) that need
** finalization from list 'finobj' to list 'tobefnz' (to be finalized).
** (Note that objects after 'finobjold1' cannot be white, so they
** don't need to be traversed. In incremental mode, 'finobjold1' is NULL,
** so the whole list is traversed.)
*/

/// @brief 将带__gc函数的需要回收的(白色)对象放到global_State.tobefnz表中,留待以后清理
/// @param g 
/// @param all 
static void separatetobefnz (global_State *g, int all) {
  GCObject *curr;
  GCObject **p = &g->finobj;//找到带有_gcH函数的链表
  GCObject **lastnext = findlast(&g->tobefnz);//找到最后一个字段的下一个位置
  while ((curr = *p) != g->finobjold1) {  /* traverse all finalizable objects *///遍历所有带__gc函数的对象
    lua_assert(tofinalize(curr));
    if (!(iswhite(curr) || all))  /* not being collected? *///如果不是白色或者全部状态
      p = &curr->next;  /* don't bother with it *///选择下一个
    else {
      if (curr == g->finobjsur)  /* removing 'finobjsur'? *///移除掉当前带有_gc函数的生存对象
        g->finobjsur = curr->next;  /* correct it *///指针指向下一个,等价于从g->finobjsur移除curr
      *p = curr->next;  /* remove 'curr' from 'finobj' list *///指针指向下一个,等价于从g->finobj移除curr
      curr->next = *lastnext;  /* link at the end of 'tobefnz' list *///这句话加下面两家等价于把对象放到global_State.tobefnz屁股后面
      *lastnext = curr;
      lastnext = &curr->next;
    }
  }
}


/*
** If pointer 'p' points to 'o', move it to the next element.
*/

/// @brief 如果指针p指向o，则将其移动到下一个元素。
/// @param p 
/// @param o 
static void checkpointer (GCObject **p, GCObject *o) {
  if (o == *p)
    *p = o->next;
}


/*
** Correct pointers to objects inside 'allgc' list when
** object 'o' is being removed from the list.
*/

/// @brief  纠正指针
/// @param g 
/// @param o 
static void correctpointers (global_State *g, GCObject *o) {
  checkpointer(&g->survival, o);
  checkpointer(&g->old1, o);
  checkpointer(&g->reallyold, o);
  checkpointer(&g->firstold1, o);
}


/*
** if object 'o' has a finalizer, remove it from 'allgc' list (must
** search the list to find it) and link it in 'finobj' list.
*/

/// @brief 当对一个对像设置gc函数时, 例如setmetatable(tbl, {__gc == function() end}), 
// LuaVM会调用luaC_checkfinalizer函数将对像tbl从global_State.allgc链表上移除,
// 并将其添加到global_State.finobj上(ps.从这里可以得出一个结论,gc元表设置越早效率越高,因为这一操作需要遍历才实现).
/// @param L 
/// @param o 
/// @param mt 
void luaC_checkfinalizer (lua_State *L, GCObject *o, Table *mt) {
  global_State *g = G(L);//获取全局状态机
  if (tofinalize(o) ||                 /* obj. is already marked... *///已经进行了标记
      gfasttm(g, mt, TM_GC) == NULL ||    /* or has no finalizer... *///当不含gc元方法或者是关闭步骤的时候
      (g->gcstp & GCSTPCLS))                   /* or closing state? */
    return;  /* nothing to be done *///啥也不做
  else {  /* move 'o' to 'finobj' list *///把o从g->finobj进行移除
    GCObject **p;
    if (issweepphase(g)) {//是不是扫描阶段
      makewhite(g, o);  /* "sweep" object 'o' *///擦除所有的颜色位,保留当前的白色位
      if (g->sweepgc == &o->next)  /* should not remove 'sweepgc' object *///不应删除扫描对象
        g->sweepgc = sweeptolive(L, g->sweepgc);  /* change 'sweepgc' *///标记为当前白
    }
    else
      correctpointers(g, o);
    /* search for pointer pointing to 'o' */
    for (p = &g->allgc; *p != o; p = &(*p)->next) { /* empty */ }//这步是从g->allgc里面找到o
    *p = o->next;  /* remove 'o' from 'allgc' list *///把o从allgc移除
    o->next = g->finobj;  /* link it in 'finobj' list *///扔到finobj链表
    g->finobj = o;
    l_setbit(o->marked, FINALIZEDBIT);  /* mark it as such *///标记
  }
}

/* }====================================================== */


/*
** {======================================================
** Generational Collector //分代gc
** =======================================================
*/

static void setpause (global_State *g);//c语言前置声明


/*
** Sweep a list of objects to enter generational mode.  Deletes dead
** objects and turns the non dead to old. All non-dead threads---which
** are now old---must be in a gray list. Everything else is not in a
** gray list. Open upvalues are also kept gray.
*/

/// @brief  清除过期的对象，同时把所有对象存活的对象标记为G_OLD
/// @param L 
/// @param p 
static void sweep2old (lua_State *L, GCObject **p) {
  GCObject *curr;
  global_State *g = G(L);//获取全局状态机
  while ((curr = *p) != NULL) {//进行遍历
    if (iswhite(curr)) {  /* is 'curr' dead? *///如果是白色的
      lua_assert(isdead(g, curr));//其他白,说明肯定要被清除掉额
      *p = curr->next;  /* remove 'curr' from list *///从p链表删除curr对象
      freeobj(L, curr);  /* erase 'curr' *///释放curr
    }
    else {  /* all surviving objects become old *///把所有幸存的物体都变旧
      setage(curr, G_OLD);//设置年龄为G_OLD
      if (curr->tt == LUA_VTHREAD) {  /* threads must be watched *///如果是线程
        lua_State *th = gco2th(curr);
        linkgclist(th, g->grayagain);  /* insert into 'grayagain' list *///丢到grayagain链表
      }
      else if (curr->tt == LUA_VUPVAL && upisopen(gco2upv(curr)))//如果是上值类型，并且是open状态的
        set2gray(curr);  /* open upvalues are always gray *///上值必须设置成灰色
      else  /* everything else is black */
        nw2black(curr);//其他的都是黑色
      p = &curr->next;  /* go to next element *///下一个
    }
  }
}


/*
** Sweep for generational mode. Delete dead objects. (Because the
** collection is not incremental, there are no "new white" objects
** during the sweep. So, any white object must be dead.) For
** non-dead objects, advance their ages and clear the color of
** new objects. (Old objects keep their colors.)
** The ages of G_TOUCHED1 and G_TOUCHED2 objects cannot be advanced
** here, because these old-generation objects are usually not swept
** here.  They will all be advanced in 'correctgraylist'. That function
** will also remove objects turned white here from any gray list.
*/

/// @brief 从p开始到limit，如果该节点没有被mark过则清除，否则将age设置到下一状态
/// @param L 
/// @param g 
/// @param p 
/// @param limit 
/// @param pfirstold1 
/// @return 
static GCObject **sweepgen (lua_State *L, global_State *g, GCObject **p,
                            GCObject *limit, GCObject **pfirstold1) {
  static const lu_byte nextage[] = {
    G_SURVIVAL,  /* from G_NEW */
    G_OLD1,      /* from G_SURVIVAL */
    G_OLD1,      /* from G_OLD0 */
    G_OLD,       /* from G_OLD1 */
    G_OLD,       /* from G_OLD (do not change) */
    G_TOUCHED1,  /* from G_TOUCHED1 (do not change) */
    G_TOUCHED2   /* from G_TOUCHED2 (do not change) */
  };
  int white = luaC_white(g);//获取当前白
  GCObject *curr;
  while ((curr = *p) != limit) {//遍历
    if (iswhite(curr)) {  /* is 'curr' dead? *///如果是白色
      lua_assert(!isold(curr) && isdead(g, curr));//如果不是旧的并且已经死亡状态
      *p = curr->next;  /* remove 'curr' from list *///从p链表中进行移除
      freeobj(L, curr);  /* erase 'curr' *///释放空间
    }
    else {  /* correct mark and age */ //进行回收标记和年龄设置
      if (getage(curr) == G_NEW) {  /* new objects go back to white *///新对象恢复为白色
        int marked = curr->marked & ~maskgcbits;  /* erase GC bits *///释放gc bit位 就是把gc bit位都变成0
        curr->marked = cast_byte(marked | G_SURVIVAL | white);//设置成G_SURVIVAL属性的白色对象
      }
      else {  /* all other objects will be old, and so keep their color *///所有其他对象都将是旧的，因此保持其颜色
        setage(curr, nextage[getage(curr)]);//将age设置到下一状态
        if (getage(curr) == G_OLD1 && *pfirstold1 == NULL)
          *pfirstold1 = curr;  /* first OLD1 object in the list *///列表中的第一个 OLD1 对象
      }
      p = &curr->next;  /* go to next element *///遍历下一个

    }
  }
  return p;
}


/*
** Traverse a list making all its elements white and clearing their
** age. In incremental mode, all objects are 'new' all the time,
** except for fixed strings (which are always old).
*/

/// @brief 把所有节点标记为白色
/// @param g 
/// @param p 
static void whitelist (global_State *g, GCObject *p) {
  int white = luaC_white(g);
  for (; p != NULL; p = p->next)
    p->marked = cast_byte((p->marked & ~maskgcbits) | white);
}


/*
** Correct a list of gray objects. Return pointer to where rest of the
** list should be linked.
** Because this correction is done after sweeping, young objects might
** be turned white and still be in the list. They are only removed.
** 'TOUCHED1' objects are advanced to 'TOUCHED2' and remain on the list;
** Non-white threads also remain on the list; 'TOUCHED2' objects become
** regular old; they and anything else are removed from the list.
*/

/// @brief 对列表进行操作，前面说了graygain链表会在最后进行touched相关的操作
/// @param p 
/// @return 
static GCObject **correctgraylist (GCObject **p) {
  GCObject *curr;
  while ((curr = *p) != NULL) {
    GCObject **next = getgclist(curr);//获取下一个链表
    if (iswhite(curr))//如果是白色直接移除
      goto remove;  /* remove all white objects */
    else if (getage(curr) == G_TOUCHED1) {  /* touched in this cycle? *///年龄是G_TOUCHED1
      lua_assert(isgray(curr));//curr对象必须是灰色
      nw2black(curr);  /* make it black, for next barrier *///让它变黑，用于下一个屏障
      changeage(curr, G_TOUCHED1, G_TOUCHED2);//把curr的年龄从G_TOUCHED1变到G_TOUCHED2
      goto remain;  /* keep it in the list and go to next element *////将其保留在列表中并转到下一个元素
    }
    else if (curr->tt == LUA_VTHREAD) {//如果是线程
      lua_assert(isgray(curr));//curr对象必须是灰色
      goto remain;  /* keep non-white threads on the list *///非白色线程保存在列表,并转到下一个元素
    }
    else {  /* everything else is removed *///其他所有内容都被删除
      lua_assert(isold(curr));  /* young objects should be white here */
      if (getage(curr) == G_TOUCHED2)  /* advance from TOUCHED2... */
        changeage(curr, G_TOUCHED2, G_OLD);  /* ... to OLD *///G_TOUCHED2变到G_OLD
      nw2black(curr);  /* make object black (to be removed) *///把对象变黑，并删除
      goto remove;
    }
    remove: *p = *next; continue;//这个操作curr会移除掉因为这个是一级级指针地址偏移
    remain: p = next; continue;//这个操作curr会会保留下来,因为这个是二级指针地址偏移
  }
  return p;
}


/*
** Correct all gray lists, coalescing them into 'grayagain'.
*/

/// @brief 遍历所有grayagain以及weak系table链表进行操作
/// @param g 
static void correctgraylists (global_State *g) {
  GCObject **list = correctgraylist(&g->grayagain);
  *list = g->weak; g->weak = NULL;
  list = correctgraylist(list);
  *list = g->allweak; g->allweak = NULL;
  list = correctgraylist(list);
  *list = g->ephemeron; g->ephemeron = NULL;
  correctgraylist(list);
}


/*
** Mark black 'OLD1' objects when starting a new young collection.
** Gray objects are already in some gray list, and so will be visited
** in the atomic step.
*/

/// @brief 从from到to，如果节点为G_OLD1并且为黑色，则进行mark，不是黑色表明，该节点已经被mark，所以不用在mark
/// @param g 
/// @param from 
/// @param to 
static void markold (global_State *g, GCObject *from, GCObject *to) {
  GCObject *p;
  for (p = from; p != to; p = p->next) {
    if (getage(p) == G_OLD1) {//如果只是G_OLD1年龄
      lua_assert(!iswhite(p));//不是白色
      changeage(p, G_OLD1, G_OLD);  /* now they are old *///G_OLD1变到G_OLD年龄
      if (isblack(p))//如果是黑色
        reallymarkobject(g, p);//进行标记
    }
  }
}


/*
** Finish a young-generation collection.
*/

/// @brief 进行分代gc的后续操作
/// @param L 
/// @param g 
static void finishgencycle (lua_State *L, global_State *g) {
  correctgraylists(g);//遍历所有grayagain以及weak系table链表进行操作
  checkSizes(L, g);//如果可能，收缩字符串表
  g->gcstate = GCSpropagate;  /* skip restart *///设置成GCSpropagate阶段
  if (!g->gcemergency)//不是紧急
    callallpendingfinalizers(L);//调用所有的table或者userdata的"__gc"元方法
}


/*
** Does a young collection. First, mark 'OLD1' objects. Then does the
** atomic step. Then, sweep all lists and advance pointers. Finally,
** finish the collection.
*/

/// @brief 一次分代gc
/// @param L 
/// @param g 
static void youngcollection (lua_State *L, global_State *g) {
  GCObject **psurvival;  /* to point to first non-dead survival object *///指向第一个未死亡的生存对象
  GCObject *dummy;  /* dummy out parameter to 'sweepgen' */
  lua_assert(g->gcstate == GCSpropagate);///保证当前状态是GCSpropagate
  if (g->firstold1) {  /* are there regular OLD1 objects? *////是否有常规的 OLD1 对象
    markold(g, g->firstold1, g->reallyold);  /* mark them *///进行标记
    g->firstold1 = NULL;  /* no more OLD1 objects (for now) *///g->firstold1不再有 OLD1 对象
  }

  /// 将G_OLD1的所有的黑色节点，重新mark一次，原因在age变化过程中有写
  markold(g, g->finobj, g->finobjrold);
  markold(g, g->tobefnz, NULL);

  ///标记节点
  atomic(L);

  /* sweep nursery and get a pointer to its last live element */
  g->gcstate = GCSswpallgc;

  // 遍历此次创建的节点，free掉没被引用的节点，设置被引用的节点到新的age状态;
  // 返回的psurvival指向的是g->survival的上一个节点的next指针;
  // 这样可以确保g->survival能进行正确的删除操作
  psurvival = sweepgen(L, g, &g->allgc, g->survival, &g->firstold1);
  /* sweep 'survival' */
  sweepgen(L, g, psurvival, g->old1, &g->firstold1);
  g->reallyold = g->old1;
  g->old1 = *psurvival;  /* 'survival' survivals are old now */
  g->survival = g->allgc;  /* all news are survivals */

  /* repeat for 'finobj' lists */
  dummy = NULL;  /* no 'firstold1' optimization for 'finobj' lists */
  psurvival = sweepgen(L, g, &g->finobj, g->finobjsur, &dummy);
  /* sweep 'survival' */
  sweepgen(L, g, psurvival, g->finobjold1, &dummy);
  g->finobjrold = g->finobjold1;
  g->finobjold1 = *psurvival;  /* 'survival' survivals are old now */
  g->finobjsur = g->finobj;  /* all news are survivals */

  sweepgen(L, g, &g->tobefnz, NULL, &dummy);
  finishgencycle(L, g);
}


/*
** Clears all gray lists, sweeps objects, and prepare sublists to enter
** generational mode. The sweeps remove dead objects and turn all
** surviving objects to old. Threads go back to 'grayagain'; everything
** else is turned black (not in any gray list).
*/

/// @brief 原子性切换到分代模式
/// @param L 
/// @param g 
static void atomic2gen (lua_State *L, global_State *g) {
  cleargraylists(g);
  /* sweep all elements making them old */
  g->gcstate = GCSswpallgc;
  sweep2old(L, &g->allgc);
  /* everything alive now is old */
  g->reallyold = g->old1 = g->survival = g->allgc;
  g->firstold1 = NULL;  /* there are no OLD1 objects anywhere */

  /* repeat for 'finobj' lists */
  sweep2old(L, &g->finobj);
  g->finobjrold = g->finobjold1 = g->finobjsur = g->finobj;

  sweep2old(L, &g->tobefnz);

  g->gckind = KGC_GEN;
  g->lastatomic = 0;
  g->GCestimate = gettotalbytes(g);  /* base for memory control */
  finishgencycle(L, g);
}


/*
** Enter generational mode. Must go until the end of an atomic cycle
** to ensure that all objects are correctly marked and weak tables
** are cleared. Then, turn all objects into old and finishes the
** collection.
*/

/// @brief 进入分代模式
/// @param L 
/// @param g 
/// @return 
static lu_mem entergen (lua_State *L, global_State *g) {
  lu_mem numobjs;
  luaC_runtilstate(L, bitmask(GCSpause));  /* prepare to start a new cycle */
  luaC_runtilstate(L, bitmask(GCSpropagate));  /* start new cycle */
  numobjs = atomic(L);  /* propagates all and then do the atomic stuff */
  atomic2gen(L, g);
  return numobjs;
}


/*
** Enter incremental mode. Turn all objects white, make all
** intermediate lists point to NULL (to avoid invalid pointers),
** and go to the pause state.
*/

/// @brief 进入到增量模式
/// @param g 
static void enterinc (global_State *g) {
  whitelist(g, g->allgc);
  g->reallyold = g->old1 = g->survival = NULL;
  whitelist(g, g->finobj);
  whitelist(g, g->tobefnz);
  g->finobjrold = g->finobjold1 = g->finobjsur = NULL;
  g->gcstate = GCSpause;
  g->gckind = KGC_INC;
  g->lastatomic = 0;
}


/*
** Change collector mode to 'newmode'.
*/
void luaC_changemode (lua_State *L, int newmode) {
  global_State *g = G(L);
  if (newmode != g->gckind) {
    if (newmode == KGC_GEN)  /* entering generational mode? */
      entergen(L, g);
    else
      enterinc(g);  /* entering incremental mode */
  }
  g->lastatomic = 0;
}


/*
** Does a full collection in generational mode.
*/

/// @brief 在分代模式下执行完整集合
/// @param L 
/// @param g 
/// @return 
static lu_mem fullgen (lua_State *L, global_State *g) {
  enterinc(g);
  return entergen(L, g);
}


/*
** Set debt for the next minor collection, which will happen when
** memory grows 'genminormul'%.
*/
static void setminordebt (global_State *g) {
  luaE_setdebt(g, -(cast(l_mem, (gettotalbytes(g) / 100)) * g->genminormul));
}


/*
** Does a major collection after last collection was a "bad collection".
**
** When the program is building a big structure, it allocates lots of
** memory but generates very little garbage. In those scenarios,
** the generational mode just wastes time doing small collections, and
** major collections are frequently what we call a "bad collection", a
** collection that frees too few objects. To avoid the cost of switching
** between generational mode and the incremental mode needed for full
** (major) collections, the collector tries to stay in incremental mode
** after a bad collection, and to switch back to generational mode only
** after a "good" collection (one that traverses less than 9/8 objects
** of the previous one).
** The collector must choose whether to stay in incremental mode or to
** switch back to generational mode before sweeping. At this point, it
** does not know the real memory in use, so it cannot use memory to
** decide whether to return to generational mode. Instead, it uses the
** number of objects traversed (returned by 'atomic') as a proxy. The
** field 'g->lastatomic' keeps this count from the last collection.
** ('g->lastatomic != 0' also means that the last collection was bad.)
*/
static void stepgenfull (lua_State *L, global_State *g) {
  lu_mem newatomic;  /* count of traversed objects */
  lu_mem lastatomic = g->lastatomic;  /* count from last collection */
  if (g->gckind == KGC_GEN)  /* still in generational mode? */
    enterinc(g);  /* enter incremental mode */
  luaC_runtilstate(L, bitmask(GCSpropagate));  /* start new cycle */
  newatomic = atomic(L);  /* mark everybody */
  if (newatomic < lastatomic + (lastatomic >> 3)) {  /* good collection? */
    atomic2gen(L, g);  /* return to generational mode */
    setminordebt(g);
  }
  else {  /* another bad collection; stay in incremental mode */
    g->GCestimate = gettotalbytes(g);  /* first estimate */;
    entersweep(L);
    luaC_runtilstate(L, bitmask(GCSpause));  /* finish collection */
    setpause(g);
    g->lastatomic = newatomic;
  }
}


/*
** Does a generational "step".
** Usually, this means doing a minor collection and setting the debt to
** make another collection when memory grows 'genminormul'% larger.
**
** However, there are exceptions.  If memory grows 'genmajormul'%
** larger than it was at the end of the last major collection (kept
** in 'g->GCestimate'), the function does a major collection. At the
** end, it checks whether the major collection was able to free a
** decent amount of memory (at least half the growth in memory since
** previous major collection). If so, the collector keeps its state,
** and the next collection will probably be minor again. Otherwise,
** we have what we call a "bad collection". In that case, set the field
** 'g->lastatomic' to signal that fact, so that the next collection will
** go to 'stepgenfull'.
**
** 'GCdebt <= 0' means an explicit call to GC step with "size" zero;
** in that case, do a minor collection.
*/

/// @brief 分代gc
/// @param L 
/// @param g 
static void genstep (lua_State *L, global_State *g) {
  if (g->lastatomic != 0)  /* last collection was a bad one? */
    stepgenfull(L, g);  /* do a full step */
  else {
    lu_mem majorbase = g->GCestimate;  /* memory after last major collection */
    lu_mem majorinc = (majorbase / 100) * getgcparam(g->genmajormul);
    if (g->GCdebt > 0 && gettotalbytes(g) > majorbase + majorinc) {
      lu_mem numobjs = fullgen(L, g);  /* do a major collection */
      if (gettotalbytes(g) < majorbase + (majorinc / 2)) {
        /* collected at least half of memory growth since last major
           collection; keep doing minor collections */
        setminordebt(g);
      }
      else {  /* bad collection */
        g->lastatomic = numobjs;  /* signal that last collection was bad */
        setpause(g);  /* do a long wait for next (major) collection */
      }
    }
    else {  /* regular case; do a minor collection */
      youngcollection(L, g);
      setminordebt(g);
      g->GCestimate = majorbase;  /* preserve base value */
    }
  }
  lua_assert(isdecGCmodegen(g));
}

/* }====================================================== */


/*
** {======================================================
** GC control
** =======================================================
*/


/*
** Set the "time" to wait before starting a new GC cycle; cycle will
** start when memory use hits the threshold of ('estimate' * pause /
** PAUSEADJ). (Division by 'estimate' should be OK: it cannot be zero,
** because Lua cannot even start with less than PAUSEADJ bytes).
*/

/// @brief   
// 不考虑越界情况,代码这样写
// l_mem threshold = g->gcpause * g->GCestimate / PAUSEADJ;
// luaE_setdebt(g, gettotalbytes(g) - threshold);
// gcpause默认是200,PAUSEADJ默认是100
// 也就是threshold默认是 2*g->GCestimate
// 在luaC_fullgc代码中可以确认当GC循环执行到GCScallfin状态以前,g->GCestimate与gettotalbytes(g)必然相等
// 也就是说设置的g->GCdebt = -g->GCestimate
// 需要分配g->GCestimate大小的内存后,再启动垃圾回收
/// @param g 
static void setpause (global_State *g) {
  l_mem threshold, debt;
  int pause = getgcparam(g->gcpause);
  l_mem estimate = g->GCestimate / PAUSEADJ;  /* adjust 'estimate' */
  lua_assert(estimate > 0);
  threshold = (pause < MAX_LMEM / estimate)  /* overflow? */
            ? estimate * pause  /* no overflow */
            : MAX_LMEM;  /* overflow; truncate to maximum */
  debt = gettotalbytes(g) - threshold;
  if (debt > 0) debt = 0;
  luaE_setdebt(g, debt);
}


/*
** Enter first sweep phase.
** The call to 'sweeptolive' makes the pointer point to an object
** inside the list (instead of to the header), so that the real sweep do
** not need to skip objects created between "now" and the start of the
** real sweep.
*/

/// @brief // 设置gcstate为GCSswpallgc
// 尝试设置sweepgc为allgc中下一项
/// @param L 
static void entersweep (lua_State *L) {
  global_State *g = G(L);
  g->gcstate = GCSswpallgc;
  lua_assert(g->sweepgc == NULL);
  g->sweepgc = sweeptolive(L, &g->allgc);
}


/*
** Delete all objects in list 'p' until (but not including) object
** 'limit'.
*/
static void deletelist (lua_State *L, GCObject *p, GCObject *limit) {
  while (p != limit) {
    GCObject *next = p->next;
    freeobj(L, p);
    p = next;
  }
}


/*
** Call all finalizers of the objects in the given Lua state, and
** then free all objects, except for the main thread.
*/
void luaC_freeallobjects (lua_State *L) {
  global_State *g = G(L);
  g->gcstp = GCSTPCLS;  /* no extra finalizers after here */
  luaC_changemode(L, KGC_INC);
  separatetobefnz(g, 1);  /* separate all objects with finalizers */
  lua_assert(g->finobj == NULL);
  callallpendingfinalizers(L);
  deletelist(L, g->allgc, obj2gco(g->mainthread));
  lua_assert(g->finobj == NULL);  /* no new finalizers */
  deletelist(L, g->fixedgc, NULL);  /* collect fixed objects */
  lua_assert(g->strt.nuse == 0);
}


static lu_mem atomic (lua_State *L) {
  global_State *g = G(L);
  lu_mem work = 0;
  GCObject *origweak, *origall;
  GCObject *grayagain = g->grayagain;  /* save original list */
  g->grayagain = NULL;
  lua_assert(g->ephemeron == NULL && g->weak == NULL);
  lua_assert(!iswhite(g->mainthread));
  g->gcstate = GCSatomic;
  markobject(g, L);  /* mark running thread */
  /* registry and global metatables may be changed by API */
  markvalue(g, &g->l_registry);
  markmt(g);  /* mark global metatables */
  work += propagateall(g);  /* empties 'gray' list */
  /* remark occasional upvalues of (maybe) dead threads */
  work += remarkupvals(g);
  work += propagateall(g);  /* propagate changes */
  g->gray = grayagain;
  work += propagateall(g);  /* traverse 'grayagain' list */
  convergeephemerons(g);
  /* at this point, all strongly accessible objects are marked. */
  /* Clear values from weak tables, before checking finalizers */
  clearbyvalues(g, g->weak, NULL);
  clearbyvalues(g, g->allweak, NULL);
  origweak = g->weak; origall = g->allweak;
  separatetobefnz(g, 0);  /* separate objects to be finalized */
  work += markbeingfnz(g);  /* mark objects that will be finalized */
  work += propagateall(g);  /* remark, to propagate 'resurrection' */
  convergeephemerons(g);
  /* at this point, all resurrected objects are marked. */
  /* remove dead objects from weak tables */
  clearbykeys(g, g->ephemeron);  /* clear keys from all ephemeron tables */
  clearbykeys(g, g->allweak);  /* clear keys from all 'allweak' tables */
  /* clear values from resurrected weak tables */
  clearbyvalues(g, g->weak, origweak);
  clearbyvalues(g, g->allweak, origall);
  luaS_clearcache(g);
  g->currentwhite = cast_byte(otherwhite(g));  /* flip current white */
  lua_assert(g->gray == NULL);
  return work;  /* estimate of slots marked by 'atomic' */
}


static int sweepstep (lua_State *L, global_State *g,
                      int nextstate, GCObject **nextlist) {
  if (g->sweepgc) {
    l_mem olddebt = g->GCdebt;
    int count;
    g->sweepgc = sweeplist(L, g->sweepgc, GCSWEEPMAX, &count);
    g->GCestimate += g->GCdebt - olddebt;  /* update estimate */
    return count;
  }
  else {  /* enter next state */
    g->gcstate = nextstate;
    g->sweepgc = nextlist;
    return 0;  /* no work done */
  }
}

/// @brief 
// work:工作单元表示业务对象量
/// @param L 
/// @return 
static lu_mem singlestep (lua_State *L) {
  global_State *g = G(L);
  lu_mem work;//工作单元
  lua_assert(!g->gcstopem);  /* collector is not reentrant */
  g->gcstopem = 1;  /* no emergency collections while collecting */
  switch (g->gcstate) {
    case GCSpause: {
      restartcollection(g);
      g->gcstate = GCSpropagate;
      work = 1;
      break;
    }
    case GCSpropagate: {
      if (g->gray == NULL) {  /* no more gray objects? */
        g->gcstate = GCSenteratomic;  /* finish propagate phase */
        work = 0;
      }
      else
        work = propagatemark(g);  /* traverse one gray object */
      break;
    }
    case GCSenteratomic: {
      work = atomic(L);  /* work is what was traversed by 'atomic' */
      entersweep(L);
      g->GCestimate = gettotalbytes(g);  /* first estimate */;
      break;
    }
    case GCSswpallgc: {  /* sweep "regular" objects */
      work = sweepstep(L, g, GCSswpfinobj, &g->finobj);
      break;
    }
    case GCSswpfinobj: {  /* sweep objects with finalizers */
      work = sweepstep(L, g, GCSswptobefnz, &g->tobefnz);
      break;
    }
    case GCSswptobefnz: {  /* sweep objects to be finalized */
      work = sweepstep(L, g, GCSswpend, NULL);
      break;
    }
    case GCSswpend: {  /* finish sweeps */
      checkSizes(L, g);
      g->gcstate = GCScallfin;
      work = 0;
      break;
    }
    case GCScallfin: {  /* call remaining finalizers */
      if (g->tobefnz && !g->gcemergency) {
        g->gcstopem = 0;  /* ok collections during finalizers */
        work = runafewfinalizers(L, GCFINMAX) * GCFINALIZECOST;
      }
      else {  /* emergency mode or no more finalizers */
        g->gcstate = GCSpause;  /* finish collection */
        work = 0;
      }
      break;
    }
    default: lua_assert(0); return 0;
  }
  g->gcstopem = 0;
  return work;
}


/*
** advances the garbage collector until it reaches a state allowed
** by 'statemask'
*/
void luaC_runtilstate (lua_State *L, int statesmask) {
  global_State *g = G(L);
  while (!testbit(statesmask, g->gcstate))
    singlestep(L);
}



/*
** Performs a basic incremental step. The debt and step size are
** converted from bytes to "units of work"; then the function loops
** running single steps until adding that many units of work or
** finishing a cycle (pause state). Finally, it sets the debt that
** controls when next step will be performed.
*/

/// @brief 增量gc
/// @param L 
/// @param g 
static void incstep (lua_State *L, global_State *g) {
  int stepmul = (getgcparam(g->gcstepmul) | 1);  /* avoid division by 0 */
  l_mem debt = (g->GCdebt / WORK2MEM) * stepmul;
  l_mem stepsize = (g->gcstepsize <= log2maxs(l_mem))
                 ? ((cast(l_mem, 1) << g->gcstepsize) / WORK2MEM) * stepmul
                 : MAX_LMEM;  /* overflow; keep maximum value */
  do {  /* repeat until pause or enough "credit" (negative debt) */
    lu_mem work = singlestep(L);  /* perform one single step */
    debt -= work;
  } while (debt > -stepsize && g->gcstate != GCSpause);
  if (g->gcstate == GCSpause)
    setpause(g);  /* pause until next cycle */
  else {
    debt = (debt / stepmul) * WORK2MEM;  /* convert 'work units' to bytes */
    luaE_setdebt(g, debt);
  }
}

/*
** performs a basic GC step if collector is running
*/
void luaC_step (lua_State *L) {
  global_State *g = G(L);
  lua_assert(!g->gcemergency);
  if (gcrunning(g)) {  /* running? */
    if(isdecGCmodegen(g))
      genstep(L, g);
    else
      incstep(L, g);
  }
}


/*
** Perform a full collection in incremental mode.
** Before running the collection, check 'keepinvariant'; if it is true,
** there may be some objects marked as black, so the collector has
** to sweep all objects to turn them back to white (as white has not
** changed, nothing will be collected).
*/

/// @brief 以增量模式执行完整收集
/// @param L 
/// @param g 
static void fullinc (lua_State *L, global_State *g) {
  if (keepinvariant(g))  /* black objects? *///如果是标记阶段
    entersweep(L); /* sweep everything to turn them back to white *///扫描所有内容以将它们变回白色
  /* finish any pending sweep phase to start a new cycle *///完成任何挂起的扫描阶段以开始新的周期
  //  执行一次完整的遍历过程
  luaC_runtilstate(L, bitmask(GCSpause));
  luaC_runtilstate(L, bitmask(GCScallfin));  /* run up to finalizers */
  /* estimate must be correct after a full GC cycle */
  lua_assert(g->GCestimate == gettotalbytes(g));//字节数得对的上
  luaC_runtilstate(L, bitmask(GCSpause));  /* finish collection *///收集完成
  setpause(g);
}


/*
** Performs a full GC cycle; if 'isemergency', set a flag to avoid
** some operations which could change the interpreter state in some
** unexpected ways (running finalizers and shrinking some structures).
*/

/// @brief 首先尽可能快的结束执行到一半的GC循环，然后从头开始一轮新的GC循环，以达到fullgc的语义
/// @param L 
/// @param isemergency 
void luaC_fullgc (lua_State *L, int isemergency) {
  global_State *g = G(L);//获取全局状态机
  lua_assert(!g->gcemergency);//不能是紧急回收
  g->gcemergency = isemergency;  /* set flag *///设置成紧急
  if (g->gckind == KGC_INC)//如果是增量
    fullinc(L, g);//以增量模式执行完整收集
  else
    fullgen(L, g);//在分代模式下执行完整集合
  g->gcemergency = 0;
}

/* }====================================================== */


