/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal list primitives required by the ArtInChip GE HAL.
 */

#ifndef __AIC_LIST_H__
#define __AIC_LIST_H__

struct list_head
{
    struct list_head *next;
    struct list_head *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

#endif /* __AIC_LIST_H__ */
