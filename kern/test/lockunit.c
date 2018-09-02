#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <synch.h>
#include <thread.h>
#include <current.h>
#include <clock.h>
#include <test.h>

/*
 * Unit tests for locks.
 *
 * We test 5 correctness criteria, each stated in a comment at the
 * top of each test.
 */

#define NAMESTRING "some-silly-name"

static volatile unsigned long testval;

/////////////////////////////////////////////////
// support code

static
void
ok(void)
{
	kprintf("Test passed; now cleaning up.\n");
}

/*
 * Wrapper for lock_create
 */
static
struct lock *
makelock(const char* name)
{
    struct lock * lk;

    lk = lock_create(name);
    if (lk == NULL) {
        panic("lockunit: whoops: lock_create failed\n");
    }
    return lk;
}


/*
 * Spinlocks don't natively provide this, because it only makes sense
 * under controlled conditions.
 *
 * Note that we should really read the holder atomically; but because
 * we're using this under controlled conditions, it doesn't actually
 * matter -- nobody is supposed to be able to touch the holder while
 * we're checking it, or the check wouldn't be reliable; and, provided
 * clocksleep works, nobody can.
 */
static
bool
spinlock_not_held(struct spinlock *splk)
{
	return splk->splk_holder == NULL;
}


////////////////////////////////////////////////////////////
// tests

/*
 * 1. After a successful lock_create:
 *     
 *     - lk_name equal to passed in name
 *     - lk_name does not point to the same place in memory as passed in name
 *     - lk_wchan is not NULL
 *     - lk_holder will be NULL
 *     - lk_splk is not held and has no owner
 */
int
lcku1(int nargs, char **args)
{
    struct lock *lk;
    const char *name = NAMESTRING;

    (void)nargs; (void)args;

    lk = makelock(name);

    KASSERT(!strcmp(lk->lk_name, name));
    KASSERT(lk->lk_name != name);
    KASSERT(lk->lk_wchan != NULL);
    KASSERT(lk->lk_holder == NULL);
    KASSERT(spinlock_not_held(&lk->lk_splk));

    ok();
    /* clean up */
    lock_destroy(lk);
    return 0;
}

/*
 * 2. Lock_do_i_hold returns true when a thread that holds it calls it.
 *
 */
int
lcku2(int nargs, char **args)
{
    struct lock *lk;
    const char *name = NAMESTRING;

    (void)nargs; (void)args;

    lk = makelock(name); 
    lock_acquire(lk);
    KASSERT(lock_do_i_hold(lk));
    lock_release(lk);
    
    ok();
    /* clean up */
    lock_destroy(lk);
    return 0;
}

/*
 * 3. Lock_do_i_hold returns true when a thread that does not hold it calls it.
 *
 */

static
void
lcku3_sub(void *lockv, unsigned long junk)
{

    struct lock *lk = lockv;

    (void) junk;
    KASSERT(!lock_do_i_hold(lk));

}

int
lcku3(int nargs, char **args)
{
    struct lock *lk;
    const char *name = NAMESTRING;
    int result;

    (void)nargs; (void)args;

    lk = makelock(name); 
    lock_acquire(lk);
    
    result = thread_fork("lcku3_sub", NULL, lcku3_sub, lk, 0);
	if (result) {
		panic("lcku3: whoops: thread_fork failed\n");
	}

    /* sleep in order to give forked thread enough time to check */
    clocksleep(1);
    lock_release(lk);
    
    ok();
    /* clean up */
    lock_destroy(lk);
    return 0;
}

/*
 * 4. A lock will not let a thread other than the one that holds it release it
 *
 */

static
void
lcku4_sub(void *lockv, unsigned long junk)
{

    struct lock *lk = lockv;

    (void) junk;
    kprintf("This should assert that only the holder can release a lock. (ASSERT should fail)\n");
    lock_release(lk);
    panic("lcku4: tolerated lock being released by another thread");
}

int
lcku4(int nargs, char **args)
{
    struct lock *lk;
    const char *name = NAMESTRING;
    int result;

    (void)nargs; (void)args;

    lk = makelock(name); 
    lock_acquire(lk);
    
    result = thread_fork("lcku4_sub", NULL, lcku4_sub, lk, 0);
	if (result) {
		panic("lcku4: whoops: thread_fork failed\n");
	}

    /* sleep in order to give forked thread enough time to check */
    clocksleep(1);
    
    panic("lcku4: tolerated lock being released by another thread");
    return 0;
}

/*
 * 5. A lock will put a thread that is trying to acquire it to sleep
 *    if it is already acquired by another thread
 *
 */

static
void
lcku5_sub(void *lockv, unsigned long junk)
{

    struct lock *lk = lockv;

    (void) junk;
    lock_acquire(lk);
    testval += 1;
    KASSERT(testval == 2);
    lock_release(lk);
}

int
lcku5(int nargs, char **args)
{
    struct lock *lk;
    const char *name = NAMESTRING;
    int result;

    testval = 0;

    (void)nargs; (void)args;

    lk = makelock(name); 
    lock_acquire(lk);
    
    result = thread_fork("lcku5_sub", NULL, lcku5_sub, lk, 0);
	if (result) {
		panic("lcku5: whoops: thread_fork failed\n");
	}

    KASSERT(testval == 0);
    testval += 1;
    lock_release(lk);
    
    kprintf("Sleeping for other thread to run.\n");
    clocksleep(1);

    ok();
    /* clean up */
    lock_destroy(lk);
    
    return 0;
}
