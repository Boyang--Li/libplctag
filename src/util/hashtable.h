/***************************************************************************
 *   Copyright (C) 2017 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library/Lesser General Public License as*
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

#ifndef __UTIL_HASHTABLE_H__
#define __UTIL_HASHTABLE_H__ 1


#include <util/vector.h>

typedef rc_ptr rc_hashtable;

extern rc_hashtable hashtable_create(int size);
extern rc_ptr hashtable_get(rc_hashtable table, void *key, int key_len);

extern int hashtable_put(rc_hashtable table, void *key, int key_len, rc_ptr data_ref);

extern rc_ptr hashtable_remove(rc_hashtable table, void *key, int key_len);


#endif
