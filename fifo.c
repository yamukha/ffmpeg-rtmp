/*Queue - Linked List implementation*/

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
*/

#pragma ident	"@(#)fifo.c	1.2	05/06/08 SMI"

/*
 * Routines for manipulating a FIFO queue
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
//#include "fifo.h"
//#include "memory.h"

typedef struct fifonode {
	//AVPacket *fn_data;
	void *fn_data;
	struct fifonode *fn_next;
} fifonode_t;

struct fifo {
	fifonode_t *f_head;
	fifonode_t *f_tail;
};

typedef struct fifo fifo_t;

fifo_t * fifo_new(void)
{
	fifo_t *f;

	f = calloc(1,sizeof (fifonode_t));

	return (f);
}

// Add to the end of the fifo
void fifo_add(fifo_t *f, void *data)
{
	fifonode_t *fn = malloc(sizeof (fifonode_t));

	fn->fn_data = data;
	fn->fn_next = NULL;

	if (f->f_tail == NULL)
		f->f_head = f->f_tail = fn;
	else {
		f->f_tail->fn_next = fn;
		f->f_tail = fn;
	}
}

// Remove from the front of the fifo
void * fifo_remove(fifo_t *f)
{
	fifonode_t *fn;
	void *data;

	if ((fn = f->f_head) == NULL)
		return (NULL);

	data = fn->fn_data;
	if ((f->f_head = fn->fn_next) == NULL)
		f->f_tail = NULL;

	free(fn);

	return (data);
}

static void
fifo_nullfree(void *arg)
{
	// this function intentionally left blank
}

/* Free an entire fifo */
void fifo_free(fifo_t *f, void (*freefn)(void *))
{
	fifonode_t *fn = f->f_head;
	fifonode_t *tmp;

	if (freefn == NULL)
		freefn = fifo_nullfree;

	while (fn) {
		(*freefn)(fn->fn_data);

		tmp = fn;
		fn = fn->fn_next;
		free(tmp);
	}

	free(f);
}

int fifo_len(fifo_t *f)
{
	int i = 0;
	fifonode_t *fn;
	for (i = 0, fn = f->f_head; fn; fn = fn->fn_next, i++);
	return (i);
}

int fifo_empty(fifo_t *f)
{
    return (f->f_head == NULL);
}

int fifo_iter(fifo_t *f, int (*iter)(void *data, void *arg), void *arg)
{
    fifonode_t *fn;
    int rc;
    int ret = 0;

    for (fn = f->f_head; fn; fn = fn->fn_next) {
        if ((rc = iter(fn->fn_data, arg)) < 0)
            return (-1);
        ret += rc;
    }

    return (ret);
}

void list_print_head (fifo_t *p )
{
    fifonode_t *fn;
    fn = p->f_head;
    if (!fn)
    	return;
    else{
      printf("Num = %d\n", *(int *)fn->fn_data);
    }
    return;
  }

void * get_tail (fifo_t *p )
{
    fifonode_t *fn;
    fn = p->f_tail;
    if (!fn)
    	return;
    else{
      printf("Num = %d\n", *(int *)fn->fn_data);
    }
    return fn->fn_data;
  }

void * get_head (fifo_t *p )
{
    fifonode_t *fn;
    fn = p->f_head;
    if (!fn)
    	return;
    else{
      printf("Num = %d\n", *(int *)fn->fn_data);
    }
    return fn->fn_data;
}

int main (void){
	fifo_t * avpackets_queue ;
	avpackets_queue = fifo_new ();
    int data1 ;
    int data2 ;
    int data3 ;
    int data4 ;
    int data5 ;

    data1 = 1;
    data2 = 2;
    data3 = 3;
    data4 = 4;
    data5 = 5;
    fifo_add (avpackets_queue, (void*)&data1 );
    fifo_add (avpackets_queue, (void*)&data2 );
    fifo_add (avpackets_queue, (void*)&data3 );
    fifo_add (avpackets_queue, (void*)&data4 );
    printf ("%d\n",fifo_len (avpackets_queue));
    list_print_head(avpackets_queue);
    get_tail (avpackets_queue);
    printf ("remove 1 and add 5\n");
    fifo_remove (avpackets_queue);
    fifo_add (avpackets_queue, (void*)&data5 );
    list_print_head(avpackets_queue);
    get_tail (avpackets_queue);
    get_head (avpackets_queue);
    printf ("\ndata head %d\n",*(int*)get_head (avpackets_queue));
	return ;

}
