#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <synch.h>
#include <thread.h>
#include <current.h>
#include <clock.h>
#include <test.h>

/*
 * Unit tests for condition variables.
 *
 * We test 7 correctness criteria, each stated in a comment at the
 * top of each test.
 */

#define NAMESTRING "some-silly-name"

static volatile unsigned long testval;
static volatile unsigned long threadcnt;


/////////////////////////////////////////////////
// support code

struct lock_and_cv
{
    struct lock *lk;
    struct cv *cv;
};

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
    struct lock *lk;

    lk = lock_create(name);
    if (lk == NULL) {
        panic("cvunit: whoops: lock_create failed\n");
    }
    return lk;
}

/*
 * Wrapper for cv_create
 */
static
struct cv *
makecv(const char* name)
{
    struct cv *cv;
    cv = cv_create(name);
    if (cv == NULL) {
        panic("cvunit: whoops: cv_create failed\n");
    }
    return cv;
}

/*
 * Wrapper for making a lock and a condition variable
 */
static
struct lock_and_cv *
makelcv(const char* name)
{   
    struct lock_and_cv *lcv;
    lcv = kmalloc(sizeof(*lcv));
    if (lcv == NULL) {
        panic("cvunit: whoops: failed to create lock and cv.\n");
    }
    lcv->lk = makelock(name);
    lcv->cv = makecv(name);
    return lcv;
}

/*
 * Cleans up a lock_and_cv struct
 */
static
void
cleanlcv(struct lock_and_cv *lcv)
{ 
    lock_destroy(lcv->lk);
    cv_destroy(lcv->cv);
    kfree(lcv);  
    return;
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
 * 1. After a successful cv_create:
 *     
 *     - cv_name equal to passed in name
 *     - cv_name does not point to the same place in memory as passed in name
 *     - cv_wchan is not NULL
 *     - cv_lock is not held and has no owner
 */
int
cvu1(int nargs, char **args)
{
    struct cv *cv;
    const char *name = NAMESTRING;

    (void)nargs; (void)args;

    cv = makecv(name);

    KASSERT(!strcmp(cv->cv_name, name));
    KASSERT(cv->cv_name != name);
    KASSERT(cv->cv_wchan != NULL);
    KASSERT(spinlock_not_held(&cv->cv_lock));

    ok();
    /* clean up */
    cv_destroy(cv);

    return 0;
}

/*
 * 2. a thread will go to sleep if it calls cv_wait
 */


/*
 *  thread function for unit tests 2-4
 */
static
void
cvu2to4_sub(void *lcvv, unsigned long junk)
{

    struct lock_and_cv *lcv = lcvv;

    (void) junk;
    lock_acquire(lcv->lk);
    threadcnt++;
    cv_wait(lcv->cv, lcv->lk);
    testval++;
    lock_release(lcv->lk);

}
int
cvu2(int nargs, char **args)
{
    struct lock_and_cv *lcv;
    const char *name = NAMESTRING;
    int result;

    testval = 0;
    threadcnt = 0;

    (void)nargs; (void)args;

    lcv = makelcv(name);
    
    result = thread_fork("cvu2to4_sub", NULL, cvu2to4_sub, lcv, 0);
	if (result) {
		panic("cvu2: whoops: thread_fork failed\n");
	}

    kprintf("waiting to make sure other thread is sleeping.\n");
    clocksleep(1);
    KASSERT(testval == 0);

    /* wake up the sleeping thread to avoid a kernel crash */
    lock_acquire(lcv->lk);
    cv_signal(lcv->cv, lcv->lk);
    lock_release(lcv->lk);

    ok();
    /* clean up */
    cleanlcv(lcv);    

    return 0;

}

/*
 * 3. cv_signal will wake up exactly one thread
 */

int
cvu3(int nargs, char **args)
{
    struct lock_and_cv *lcv;
    const char *name = NAMESTRING;
    int result;

    testval = 0;
    threadcnt = 0;

    (void)nargs; (void)args;

    lcv = makelcv(name);
    
    result = thread_fork("cvu2to4a_sub", NULL, cvu2to4_sub, lcv, 0);
	if (result) {
		panic("cvu3: whoops: thread_fork failed\n");
	}

    result = thread_fork("cvu2to4b_sub", NULL, cvu2to4_sub, lcv, 0);
	if (result) {
		panic("cvu3: whoops: thread_fork failed\n");
	}

    KASSERT(testval == 0);
    
    while(threadcnt < 2) {
        thread_yield();
    }

    lock_acquire(lcv->lk);
    cv_signal(lcv->cv, lcv->lk);
    lock_release(lcv->lk);
    
    kprintf("Waiting for thread to wake up and do it's thing...\n");
    clocksleep(1);

    KASSERT(testval == 1);

    
    lock_acquire(lcv->lk);
    cv_signal(lcv->cv, lcv->lk);
    lock_release(lcv->lk);

    kprintf("Waiting for thread to wake up and do it's thing...\n");
    clocksleep(1);

    KASSERT(testval == 2);

    ok();
    /* clean up */
    cleanlcv(lcv);    

    return 0;

}

/*
 * 4. cv_broadcast will wake up all threads
 */

int
cvu4(int nargs, char **args)
{
    struct lock_and_cv *lcv;
    const char *name = NAMESTRING;
    int result;

    testval = 0;
    threadcnt = 0;

    (void)nargs; (void)args;

    lcv = makelcv(name);
    
    result = thread_fork("cvu2to4a_sub", NULL, cvu2to4_sub, lcv, 0);
	if (result) {
		panic("cvu4: whoops: thread_fork failed\n");
	}

    result = thread_fork("cvu2to4b_sub", NULL, cvu2to4_sub, lcv, 0);
	if (result) {
		panic("cvu4: whoops: thread_fork failed\n");
	}

    KASSERT(testval == 0);
    
    while(threadcnt < 2) {
        thread_yield();
    }

    lock_acquire(lcv->lk);
    cv_broadcast(lcv->cv, lcv->lk);
    lock_release(lcv->lk);

    kprintf("Waiting for threads to wake up and do their things...\n");
    clocksleep(1);

    KASSERT(testval == 2);

    ok();
    /* clean up */
    cleanlcv(lcv);    

    return 0;

}

/*
 * 5. a CV will not let a thread that does not
 *    hold the associated lock call cv_signal
 */

static
void
cvu5_sub(void *lcvv, unsigned long junk)
{

    struct lock_and_cv *lcv = lcvv;

    (void) junk;
    kprintf("This should assert that only the lock holder can call cv_signal. (ASSERT should fail)\n");
    cv_signal(lcv->cv, lcv->lk);
    panic("cvu5: tolerated cv_signal being called without owning the lock.\n");

}

int
cvu5(int nargs, char **args)
{
    struct lock_and_cv *lcv;
    const char *name = NAMESTRING;
    int result;

    testval = 0;

    (void)nargs; (void)args;

    lcv = makelcv(name);

    lock_acquire(lcv->lk);

    result = thread_fork("cvu5_sub", NULL, cvu5_sub, lcv, 0);
	if (result) {
		panic("cvu5: whoops: thread_fork failed\n");
	}

    clocksleep(1);

    panic("cvu5: tolerated cv_signal being called without owning the lock.\n");
    return 0;
}

/*
 * 6. a CV will not let a thread that does not
 *    hold the associated lock call cv_broadcast
 */

static
void
cvu6_sub(void *lcvv, unsigned long junk)
{

    struct lock_and_cv *lcv = lcvv;

    (void) junk;
    kprintf("This should assert that only the lock holder can call cv_broadcast. (ASSERT should fail)\n");
    cv_broadcast(lcv->cv, lcv->lk);
    panic("cvu6: tolerated cv_broadcast being called without owning the lock.\n");

}

int
cvu6(int nargs, char **args)
{
    struct lock_and_cv *lcv;
    const char *name = NAMESTRING;
    int result;

    testval = 0;

    (void)nargs; (void)args;

    lcv = makelcv(name);

    lock_acquire(lcv->lk);

    result = thread_fork("cvu6_sub", NULL, cvu6_sub, lcv, 0);
	if (result) {
		panic("cvu6: whoops: thread_fork failed\n");
	}

    clocksleep(1);

    panic("cvu6: tolerated cv_broadcast being called without owning the lock.\n");
    return 0;
}

/*
 * 7. a CV will not let a thread that does not
 *    hold the associated lock call cv_wait
 */

static
void
cvu7_sub(void *lcvv, unsigned long junk)
{

    struct lock_and_cv *lcv = lcvv;

    (void) junk;
    kprintf("This should assert that only the lock holder can call cv_wait. (ASSERT should fail)\n");
    cv_wait(lcv->cv, lcv->lk);
    panic("cvu7: tolerated cv_wait being called without owning the lock.\n");

}

int
cvu7(int nargs, char **args)
{
    struct lock_and_cv *lcv;
    const char *name = NAMESTRING;
    int result;

    testval = 0;

    (void)nargs; (void)args;

    lcv = makelcv(name);

    lock_acquire(lcv->lk);

    result = thread_fork("cvu7_sub", NULL, cvu7_sub, lcv, 0);
	if (result) {
		panic("cvu7: whoops: thread_fork failed\n");
	}

    clocksleep(1);

    panic("cvu7: tolerated cv_wait being called without owning the lock.\n");
    return 0;
}
