/*
   SipHash reference C implementation
   Copyright (c) 2012-2016 Jean-Philippe Aumasson
   <jeanphilippe.aumasson@gmail.com>
   Copyright (c) 2012-2014 Daniel J. Bernstein <djb@cr.yp.to>
   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.
   You should have received a copy of the CC0 Public Domain Dedication along
   with
   this software. If not, see
   <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#ifndef __DS_HASH_SIP_H__
#define __DS_HASH_SIP_H__

#include <ds/dsdefs.h>

DSDECL(uint64_t, siphash_64(
    _In_ const uint8_t* data, 
    _In_ const size_t   datalen, 
    _In_ const uint8_t* key));

DSDECL(int, siphash_128(
    _In_ const uint8_t* data, 
    _In_ const size_t   datalen, 
    _In_ const uint8_t* key,
    _In_ uint8_t*       hashOut));

#endif //!__DS_HASH_SIP_H__
