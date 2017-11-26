/***************************************************************************
 *   Copyright (C) 2016 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#include <lib/libplctag.h>
#include <platform.h>
#include <util/refcount.h>
#include <util/debug.h>
#include <util/hashtable.h>


//~ #ifndef container_of
//~ #define container_of(ptr, type, member) ((type *)((char *)(1 ? (ptr) : &((type *)0)->member) - offsetof(type, member)))
//~ #endif




/*
 * Handle clean up functions.
 */

typedef struct cleanup_t *cleanup_p;

struct cleanup_t {
    cleanup_p next;
    const char *function_name;
    int line_num;
    int extra_arg_count;
    void **extra_args;
    rc_cleanup_func cleanup_func;
    void *dummy[]; /* force alignment */
};


/*
 * This is a rc struct that we use to make sure that we are able to align
 * the remaining part of the allocated block.
 */

struct refcount_t {
    lock_t lock;
    int count;
    const char *function_name;
    int line_num;
    cleanup_p cleaners;

    /* FIXME - needed for alignment, this is a hack! */
    union {
        uint8_t dummy_u8;
        uint16_t dummy_u16;
        uint32_t dummy_u32;
        uint64_t dummy_u64;
        double dummy_double;
        void *dummy_ptr;
        void (*dummy_func)(void);
    } dummy_align[];
};


typedef struct refcount_t *refcount_p;

static void refcount_cleanup(refcount_p rc);
static cleanup_p cleanup_entry_create(const char *func, int line_num, rc_cleanup_func cleaner, int extra_arg_count, va_list extra_args);
static void cleanup_entry_destroy(cleanup_p entry);



/*
 * rc_alloc
 *
 * Create a reference counted control for the requested data size.  Return a strong
 * reference to the data.
 */
void *rc_alloc_impl(const char *func, int line_num, int data_size, int extra_arg_count, rc_cleanup_func cleaner_func, ...)
{
    refcount_p rc = NULL;
    cleanup_p cleanup = NULL;
    va_list extra_args;

    pdebug(DEBUG_INFO,"Starting, called from %s:%d",func, line_num);

    pdebug(DEBUG_SPEW,"Allocating %d-byte refcount struct",(int)sizeof(struct refcount_t));

    rc = mem_alloc(sizeof(struct refcount_t) + data_size);
    if(!rc) {
        pdebug(DEBUG_WARN,"Unable to allocate refcount struct!");
        return NULL;
    }

    rc->count = 1;  /* start with a reference count. */
    rc->lock = LOCK_INIT;

    /* store where we were called from for later. */
    rc->function_name = func;
    rc->line_num = line_num;

    /* allocate the final cleanup struct */
    va_start(extra_args, cleaner_func);
    cleanup = cleanup_entry_create(func, line_num, cleaner_func, extra_arg_count, extra_args);
    va_end(extra_args);

    if(!cleanup) {
        pdebug(DEBUG_ERROR,"Unable to allocate cleanup entry!");
        mem_free(rc);
        return NULL;
    }

    rc->cleaners = cleanup;

    pdebug(DEBUG_INFO, "Done");

    /* return the original address if successful otherwise NULL. */

    /* DEBUG */
    pdebug(DEBUG_DETAIL,"Returning memory pointer %p",(void *)(rc + 1));

    return (void *)(rc + 1);
}






/*
 * create a new cleanup entry.   This will be called when the reference is completely release.
 * Cleanup functions are called in reverse order that they are created.
 */
int rc_register_cleanup_impl(const char *func, int line_num, void *data, rc_cleanup_func cleanup_func, int extra_arg_count, ...)
{
    cleanup_p entry = NULL;
    int status = PLCTAG_STATUS_OK;
    refcount_p rc = NULL;
    va_list extra_args;

    pdebug(DEBUG_INFO,"Starting, called from %s:%d for memory pointer %p",func, line_num, data);

    if(!data) {
        pdebug(DEBUG_WARN,"Invalid pointer passed at %s:%d!",func, line_num);
        return PLCTAG_ERR_NULL_PTR;
    }

    /* get the refcount structure. */
    rc = ((refcount_p)data) - 1;

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

        if(rc->count > 0) {
            va_start(extra_args, extra_arg_count);
            entry = cleanup_entry_create(func, line_num, cleanup_func, extra_arg_count, extra_args);
            va_end(extra_args);

            if(!entry) {
                pdebug(DEBUG_ERROR,"Unable to allocate cleanup entry!");
                status = PLCTAG_ERR_NO_MEM;
            } else {
                entry->next = rc->cleaners;
                rc->cleaners = entry;
            }
        } else {
            pdebug(DEBUG_WARN,"Reference is invalid!");
            status = PLCTAG_ERR_BAD_DATA;
        }

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_INFO,"Done.");

    return status;
}






/*
 * Increments the ref count if the reference is valid.
 *
 * It returns the original poiner if the passed pointer was valid.  It returns
 * NULL if the passed pointer was invalid.
 *
 * This is for usage like:
 * my_struct->some_field_ref = rc_inc(ref);
 */

void *rc_inc_impl(const char *func, int line_num, void *data)
{
    int count = 0;
    refcount_p rc = NULL;
    void *  result = NULL;

    pdebug(DEBUG_DETAIL,"Starting, called from %s:%d for %p",func, line_num, data);

    if(!data) {
        pdebug(DEBUG_WARN,"Invalid pointer passed from %s:%d!", func, line_num);
        return result;
    }

    /* get the refcount structure. */
    rc = ((refcount_p)data) - 1;

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

        if(rc->count > 0) {
            rc->count++;
            count = rc->count;
            result = data;
        } else {
            pdebug(DEBUG_WARN,"Reference is invalid!");
            result = NULL;
        }

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    if(!result) {
        pdebug(DEBUG_WARN,"Invalid ref from call at %s line %d!  Unable to take strong reference.", func, line_num);
    } else {
        pdebug(DEBUG_DETAIL,"Ref count is %d for %p.", count, data);
    }

    /* return the result pointer. */
    return result;
}



/*
 * Decrement the ref count.
 *
 * This is for usage like:
 * my_struct->some_field = rc_dec(rc_obj);
 *
 * Note that the final clean up function _MUST_ free the data pointer
 * passed to it.   It must clean up anything referenced by that data,
 * and the block itself using mem_free() or the appropriate function;
 */

void *rc_dec_impl(const char *func, int line_num, void *data)
{
    int count = 0;
    int invalid = 0;
    refcount_p rc = NULL;

    pdebug(DEBUG_DETAIL,"Starting, called from %s:%d for %p",func, line_num, data);

    if(!data) {
        pdebug(DEBUG_WARN,"Null reference passed from %s:%d!", func, line_num);
        return NULL;
    }

    /* get the refcount structure. */
    rc = ((refcount_p)data) - 1;

    /* loop until we get the lock */
    while (!lock_acquire(&rc->lock)) {
        ; /* do nothing, just spin */
    }

        if(rc->count > 0) {
            rc->count--;
            count = rc->count;
        } else {
            pdebug(DEBUG_WARN,"Reference is invalid!");
            invalid = 1;
        }

    /* release the lock so that other things can get to it. */
    lock_release(&rc->lock);

    pdebug(DEBUG_DETAIL,"Ref count is %d for %p.", count, data);

    /* clean up only if count is zero. */
    if(rc && !invalid && count <= 0) {
        pdebug(DEBUG_DETAIL,"Calling cleanup functions due to call at %s:%d for %p.", func, line_num, data);

        refcount_cleanup(rc);
    }

    return NULL;
}




void refcount_cleanup(refcount_p rc)
{
    pdebug(DEBUG_INFO,"Starting");
    if(!rc) {
        pdebug(DEBUG_WARN,"Refcount is NULL!");
        return;
    }

    while(rc->cleaners) {
        cleanup_p entry = rc->cleaners;
        void *data = (void *)(rc+1);

        rc->cleaners = entry->next;

        /* do the clean up of the object. */
        pdebug(DEBUG_DETAIL,"Calling clean function added in %s at line %d", entry->function_name, entry->line_num);
        entry->cleanup_func(data, entry->extra_arg_count, entry->extra_args);

        cleanup_entry_destroy(entry);
    }

    /* finally done. */
    mem_free(rc);

    pdebug(DEBUG_INFO,"Done.");
}


cleanup_p cleanup_entry_create(const char *func, int line_num, rc_cleanup_func cleanup_func, int extra_arg_count, va_list extra_args)
{
    cleanup_p entry = NULL;

    pdebug(DEBUG_INFO,"Starting");

    entry = mem_alloc(sizeof(struct cleanup_t) + (sizeof(void*) * extra_arg_count));
    if(!entry) {
        pdebug(DEBUG_ERROR,"Unable to allocate new cleanup struct!");
        return NULL;
    }

    entry->cleanup_func = cleanup_func;
    entry->function_name = func;
    entry->line_num = line_num;
    entry->extra_arg_count = extra_arg_count;
    entry->extra_args = (void **)(entry+1); /* just past the struct... */

    /*
     * Fill in the extra args.   There might not be any.
     */
    for(int i=0; i < extra_arg_count; i++) {
        entry->extra_args[i] = va_arg(extra_args, void *);
    }

    pdebug(DEBUG_INFO,"Done.");

    return entry;
}


void cleanup_entry_destroy(cleanup_p entry)
{
    pdebug(DEBUG_DETAIL,"Starting.");
    if(!entry) {
        pdebug(DEBUG_WARN,"Entry pointer is NULL!");
        return;
    }

    mem_free(entry);

    pdebug(DEBUG_DETAIL,"Done.");
}

