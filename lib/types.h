#pragma once

extern "C" {

    typedef unsigned long long   uint64_t;
    typedef unsigned int         uint32_t;
    typedef unsigned short       uint16_t; 
    typedef unsigned char        uint8_t;
    typedef unsigned char        byte_t;

    typedef long long            int64_t;
    typedef long                 int32_t;
    typedef short                int16_t;
    typedef char                 int8_t;
    typedef unsigned int size_t;
    void  assert( uint32_t test, const char* cstr );

}

