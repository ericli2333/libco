#include "co.h"
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
// #define DEBUG
#ifdef DEBUG
#define debug printf("function:%s,line: %d\n", __FUNCTION__, __LINE__);
#else
#define debug
#endif

#define STACK_SIZE 1024 * 64

typedef enum
{
    Created = 1,
    Suspend,
    Running, // 在co_wait等待其它协程中
    Killed
} Status;

typedef void (*func_ptr)(void *);

typedef struct co
{
    char *name;
    Status status;
    func_ptr f;
    void *args;
    struct co *waiter;
    jmp_buf context;
    uint8_t stack[STACK_SIZE] __attribute__((aligned(16)));
} co;

#define MAX_CO 3000
struct co *current, *co_main;
struct co *all_co[MAX_CO];
void list_init()
{
    for (size_t i = 0; i < MAX_CO; i++)
    {
        all_co[i] = NULL;
    }
}
void list_delete(struct co *c)
{
    for (size_t i = 0; i < MAX_CO; i++)
    {
        if (all_co[i] == c)
        {
            free(c);
            all_co[i] = NULL;
            return;
        }
    }
}

void list_insert(struct co *c)
{
    for (size_t i = 0; i < MAX_CO; i++)
    {
        if (all_co[i] == NULL)
        {
            all_co[i] = c;
            return;
        }
    }
}

struct co *list_get()
{
    int available = 0;
    for (int i = 0; i < MAX_CO; i++)
    {
        if (all_co[i] == NULL)
            continue;
        if (all_co[i]->status == Created || all_co[i]->status == Running)
        {
            available += 1;
        }
    }
    assert(available > 0);
    available = rand() % available;
    for (int i = 0; i < MAX_CO; i++)
    {
        if (all_co[i] == NULL)
            continue;
        if (all_co[i]->status == Created || all_co[i]->status == Running)
        {
            if (!available)
            {
                return all_co[i];
            }
            available -= 1;
        }
    }
    assert(0);
}

__attribute__((constructor)) void co_init()
{
    debug
    list_init();
    co_main = malloc(sizeof(struct co));
    co_main->name = "main";
    co_main->status = Running;
    // co_main->args = NULL;
    memset(co_main->stack, 0, sizeof(co_main->stack));
    list_insert(co_main);
    current = co_main;
}

static inline void
stack_switch_call(void *sp, void *entry, uintptr_t arg)
{
    asm volatile(
#if __x86_64__
        "movq %0, %%rsp; movq %2, %%rdi; call *%1"
        :
        : "b"((uintptr_t)sp),
          "d"(entry),
          "a"(arg)
        : "memory"
#else
        "movl %0, %%esp; movl %2, 0(%0); call *%1"
        :
        : "b"((uintptr_t)sp - 16),
          "d"(entry),
          "a"(arg)
        : "memory"
#endif
    );
};

void co_print(co *c)
{
    printf("name : %s, status : %d\n", c->name, c->status);
}

void wrapper(void *arg)
{
    struct co *c = (struct co *)arg;
    c->f(c->args);
    c->status = Killed;
    if (c->waiter != NULL)
    {
        c->waiter->status = Running;
        c->waiter = NULL;
    }
    co_yield ();
}

struct co *co_start(const char *name, void (*func)(void *), void *arg)
{
    co *newCo = (co *)malloc(sizeof(co));
    newCo->name = (char *)name;
    newCo->f = func;
    newCo->args = arg;
    newCo->status = Created;
    newCo->waiter = NULL;
    memset(newCo->stack, 0, sizeof(newCo->stack));
    list_insert(newCo);
    return newCo;
}

void co_wait(struct co *toWait)
{
    if (toWait->status != Killed)
    {
        current->status = Suspend;
        toWait->waiter = current;
        co_yield ();
    }
    list_delete(toWait);
}

void co_yield ()
{
    int val = setjmp(current->context);
    if (val == 0)
    {
        co *next = list_get();
        current = next;
        if (next->status == Created)
        {
            next->status = Running;
            stack_switch_call(next->stack + sizeof(next->stack), wrapper, (uintptr_t)next);
            debug
        }
        else if (next->status == Running)
        {
            longjmp(next->context, 1);
            assert(0);
        }
        else
        {
            assert(0);
        }
    }
    else
    {
        return;
    }
}
