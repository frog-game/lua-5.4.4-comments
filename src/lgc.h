/*
 * @文件作用: 垃圾回收
 * @功能分类: 虚拟机运转的核心功能
 * @注释者: frog-game
 * @LastEditTime: 2023-01-21 19:31:38
 */
/*
** $Id: lgc.h $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h


#include "lobject.h"
#include "lstate.h"

/*
** Collectable objects may have one of three colors: white, which means
** the object is not marked; gray, which means the object is marked, but
** its references may be not marked; and black, which means that the
** object and all its references are marked.  The main invariant of the
** garbage collector, while marking objects, is that a black object can
** never point to a white one. Moreover, any gray object must be in a
** "gray list" (gray, grayagain, weak, allweak, ephemeron) so that it
** can be visited again before finishing the collection cycle. (Open
** upvalues are an exception to this rule.)  These lists have no meaning
** when the invariant is not being enforced (e.g., sweep phase).
*/


/*
** Possible states of the Garbage Collector
*/

#define GCSpropagate	0 // 传播阶段 分多次执行，直到 gray 链表处理完，进入 GCSatomic
#define GCSenteratomic	1 // gc 转移到 GCSatomic的辅助状态 , 所有的GC状态必须在GCSpropagate状态或者GCSatomic状态两者之间
#define GCSatomic	2 //一次性的处理所有需要回顾一遍的地方, 保证一致性, 然后进入清理阶段
#define GCSswpallgc	3//清扫global_State.allgc 可以分多次执行, 清理完后进入 GCSswpfinobj
#define GCSswpfinobj	4 //对global_State.finobj链表进行清扫 可以分多次执行, 清理完 后进入 GCSswptobefnz
#define GCSswptobefnz	5//对global_State.tobefnz链表进行清扫  可以分多次执行, 清理完 后进入 GCSswpend
#define GCSswpend	6//sweep main thread 然后进入 GCScallfin
#define GCScallfin	7// 执行一些 finalizer (__gc) 然后进入 GCSpause, 完成循环
#define GCSpause	8 //处于两次完整 GC 流程中间的休息状态

/// 是不是扫描阶段
#define issweepphase(g)  \
	(GCSswpallgc <= (g)->gcstate && (g)->gcstate <= GCSswpend)


/*
** macro to tell when main invariant (white objects cannot point to black
** ones) must be kept. During a collection, the sweep
** phase may break the invariant, as objects turned white may point to
** still-black objects. The invariant is restored when sweep ends and
** all objects are white again.
*/

/// 是不是标记阶段
#define keepinvariant(g)	((g)->gcstate <= GCSatomic)


/*
** some useful bit tricks
*/

//------------------------------------- 对二进制位 操作 begin ---------------------------------//
#define resetbits(x,m)		((x) &= cast_byte(~(m))) //和resetbit 进行搭配用来将特定的位清零
#define setbits(x,m)		((x) |= (m))//和l_setbit 进行搭配 实现设置一个变量的特定的位置1
#define testbits(x,m)		((x) & (m))//和testbit 进行搭配用来检测特定位是不是1
#define bitmask(b)		(1<<(b))//1向左进行偏移b位
#define bit2mask(b1,b2)		(bitmask(b1) | bitmask(b2))
#define l_setbit(x,b)		setbits(x, bitmask(b))
#define resetbit(x,b)		resetbits(x, bitmask(b))
#define testbit(x,b)		testbits(x, bitmask(b))
//------------------------------------- 对二进制位 操作 end ---------------------------------//

/*
** Layout for bit use in 'marked' field. First three bits are
** used for object "age" in generational mode. Last bit is used
** by tests.
*/

//------------------------------------- 位标记索引 begin ---------------------------------//
#define WHITE0BIT	3  /* object is white (type 0) *///白色0  	bit2mask(3)   1000
#define WHITE1BIT	4  /* object is white (type 1) *///白色1 	bit2mask(4)  10000
#define BLACKBIT	5  /* object is black *///黑色				bit2mask(5) 100000
#define FINALIZEDBIT	6  //用于标记userdata					110
//  当 userdata 确认不被引用，则设置上这个标记 不同于颜色标记。
// 主要还是userdata 能设置gc方法，释放内存要延迟到gc方法以后,这个标记可以保证元方法不被反复调用


#define TESTBIT		7//测试位 就是 111



#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT)//用来切换白色、判断对象是否dead以及标记对象为白色 十进制:24 二进制:11000
//------------------------------------- 位标记索引 end ---------------------------------//


//------------------------------------- 位检测设置 begin ---------------------------------//
#define iswhite(x)      testbits((x)->marked, WHITEBITS)//是不是白色  其实就是看第4位和第5位是不是1
#define isblack(x)      testbit((x)->marked, BLACKBIT)//是不是黑色
#define isgray(x)  /* neither white nor black */  \ 
	(!testbits((x)->marked, WHITEBITS | bitmask(BLACKBIT)))//是不是灰色 意思就是3,4,5位都的是0

#define tofinalize(x)	testbit((x)->marked, FINALIZEDBIT)//是不是标记了userdata 

#define otherwhite(g)	((g)->currentwhite ^ WHITEBITS)//非当前GC将要回收的白色类型  比如如果(g)->currentwhite是1000 1000 ^ 11000 = 10000，如果(g)->currentwhite的值是10000的话， 10000 ^ 11000 = 1000 结果正好相反。从这里的逻辑我们可以看出，white的值只有两种，要么是1000，要么是10000
#define isdeadm(ow,m)	((m) & (ow))
#define isdead(g,v)	isdeadm(otherwhite(g), (v)->marked) //如果是其他白,那么就说明真正死亡了,肯定会被清除掉, 当前白表示新创建的对象，是不一样的东西

#define changewhite(x)	((x)->marked ^= WHITEBITS) //改变白色位 比如如果((x)->marked是111000  111000 ^ 11000 = 100000 比如如果((x)->marked是101000  101000 ^ 11000 = 110000  比如如果((x)->marked是110000  110000 ^ 11000 = 101000  比如如果((x)->marked是100000  100000 ^ 11000 = 111000   注意看第4位和第5位是不是切换位标识了
#define nw2black(x)  \
	check_exp(!iswhite(x), l_setbit((x)->marked, BLACKBIT))//非白色到黑色

#define luaC_white(g)	cast_byte((g)->currentwhite & WHITEBITS) //得到当前的白色状态 比如如果(g)->currentwhite是1000 1000 & 11000 = 01000， 1000 & 11000还是1000 是能获取当前白色值的状态 如果(g)->currentwhite是10000   10000 & 11000 = 10000， 10000 &11000还是10000
//------------------------------------- 位检测设置 end ---------------------------------//

/* object age in generational mode */
//------------------------------------- 分代模式下对象年龄标识 begin ---------------------------------//
#define G_NEW		0	/* created in current cycle *///(0代对象）本次cycle创建的新对象（没有引用任何old对象） 
#define G_SURVIVAL	1	/* created in previous cycle *///(1代对象）上一轮cycle创建的对象 -- 只活过一轮，下一次如果是白色的话，仍然会被回收
#define G_OLD0		2	/* marked old by frw. barrier in this cycle *///(1代对象） 当前gc循环被barrier forward的节点，如果被插入的节点为isold()为true的节点,表示本次cycle创建的新对象，但是引用了old对象

//老年代对象
#define G_OLD1		3	/* first full cycle as old *///（2代对象）作为老对象第一次存活了整个gc过程 
#define G_OLD		4	/* really old object (not to be visited) *///（3代对象）作为老对象活过了两次完整的gc，表示真正的old对象，不会被回收 
#define G_TOUCHED1	5	/* old object touched this cycle *///（3代对象）标记位G_OLD的对象在这次gc barrier_back的状态 新touch的对象，需要进入到grayagain中
#define G_TOUCHED2	6	/* old object touched in previous cycle *///（3代对象）标记为G_OLD的对象在上一次gc barrier_back的状态前进到touched2  从G_TOUCHED1转成G_TOUCHED2，并设置为黑色，仍然存在于grayagain中

#define AGEBITS		7  /* all age bits (111) *///age使用的位mask，age只使用了marked的0,1,2字段

#define getage(o)	((o)->marked & AGEBITS)//获取年龄
#define setage(o,a)  ((o)->marked = cast_byte(((o)->marked & (~AGEBITS)) | a))//设置年龄
#define isold(o)	(getage(o) > G_SURVIVAL)//是不是旧对象

#define changeage(o,f,t)  \ //改变年龄 从f变到t 前提是o的年龄要等于f 举个例子changeage(curr, G_TOUCHED1, G_TOUCHED2); 如果getage(o) == (f) 那么marked的最地3位是101 101^=（101^110） 101^=011 o->marked=110
	check_exp(getage(o) == (f), (o)->marked ^= ((f)^(t)))//这个公式是lua作者利用对同一个值进行两次异或等于本身原理
//------------------------------------- 分代模式下对象年龄标识 end ---------------------------------//

/* Default Values for GC parameters */
#define LUAI_GENMAJORMUL         100 //全局gc参数
#define LUAI_GENMINORMUL         20 //局部gc参数

/* wait memory to double before starting new cycle */
#define LUAI_GCPAUSE    200 //等待内存翻倍数

/*
** some gc parameters are stored divided by 4 to allow a maximum value
** up to 1023 in a 'lu_byte'.
*/

//按4的倍数划分参数
#define getgcparam(p)	((p) * 4)
#define setgcparam(p,v)	((p) = (v) / 4)

#define LUAI_GCMUL      100 //gc增长速度

/* how much to allocate before next GC step (log2) */
#define LUAI_GCSTEPSIZE 13      /* 8 KB *///gc的粒度值


/*
** Check whether the declared GC mode is generational. While in
** generational mode, the collector can go temporarily to incremental
** mode to improve performance. This is signaled by 'g->lastatomic != 0'.
*/
#define isdecGCmodegen(g)	(g->gckind == KGC_GEN || g->lastatomic != 0) //是不是分代gc


/*
** Control when GC is running:
*/
#define GCSTPUSR	1  /* bit true when GC stopped by user */// 用户停止gc
#define GCSTPGC		2  /* bit true when GC stopped by itself *///gc自己停止
#define GCSTPCLS	4  /* bit true when closing Lua state *///关闭lLua state时候
#define gcrunning(g)	((g)->gcstp == 0)//gc是否正在运行


/*
** Does one step of collection when debt becomes positive. 'pre'/'pos'
** allows some adjustments to be done only when needed. macro
** 'condchangemem' is used only for heavy tests (forcing a full
** GC cycle on every opportunity)
*/

/// 条件满足自动触发gc
#define luaC_condGC(L,pre,pos) \
	{ if (G(L)->GCdebt > 0) { pre; luaC_step(L); pos;}; \
	  condchangemem(L,pre,pos); }

/* more often than not, 'pre'/'pos' are empty */
/// 随着内存的使用增加,通过对g-> GCdebt、g-> totalbytes等参数计算来触发GC
#define luaC_checkGC(L)		luaC_condGC(L,(void)0,(void)0)

/// 针对 TValue 标记过程向前走一步 如果新建对象是白色，而它被一个黑色对象引用了，那么将这个新建对象颜色从白色变为灰色 
#define luaC_barrier(L,p,v) (  \
	(iscollectable(v) && isblack(p) && iswhite(gcvalue(v))) ?  \
	luaC_barrier_(L,obj2gco(p),gcvalue(v)) : cast_void(0))

/// 标记过程向后走一步 此时将引用的它的黑色对象的颜色从黑色变为灰色，使得其重新被扫描一次
#define luaC_barrierback(L,p,v) (  \
	(iscollectable(v) && isblack(p) && iswhite(gcvalue(v))) ? \
	luaC_barrierback_(L,p) : cast_void(0))

/// 针对 GCObject 标记过程向前走一步 如果新建对象是白色，而它被一个黑色对象引用了，那么将这个新建对象颜色从白色变为灰色 
#define luaC_objbarrier(L,p,o) (  \
	(isblack(p) && iswhite(o)) ? \
	luaC_barrier_(L,obj2gco(p),obj2gco(o)) : cast_void(0))

LUAI_FUNC void luaC_fix (lua_State *L, GCObject *o);
LUAI_FUNC void luaC_freeallobjects (lua_State *L);
LUAI_FUNC void luaC_step (lua_State *L);
LUAI_FUNC void luaC_runtilstate (lua_State *L, int statesmask);
LUAI_FUNC void luaC_fullgc (lua_State *L, int isemergency);
LUAI_FUNC GCObject *luaC_newobj (lua_State *L, int tt, size_t sz);
LUAI_FUNC void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v);
LUAI_FUNC void luaC_barrierback_ (lua_State *L, GCObject *o);
LUAI_FUNC void luaC_checkfinalizer (lua_State *L, GCObject *o, Table *mt);
LUAI_FUNC void luaC_changemode (lua_State *L, int newmode);


#endif
