#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

#define decaf_handle_assert(x, e, m) \
   if (!(x)) { \
      std::fprintf(stderr,"Latte assertion %s:%u: %s (%s)\n",__FILE__,__LINE__,e,std::string(m).c_str()); \
      abort(); \
   }

#define decaf_handle_warn_once_assert(x, e, m) \
   if (!(x)) { \
      static bool seen_this_error_before = false; \
      if (!seen_this_error_before) { \
         seen_this_error_before = true; \
         std::fprintf(stderr,"Latte warning %s:%u: %s (%s)\n",__FILE__,__LINE__,e,std::string(m).c_str()); \
      } \
   }

#define decaf_assert(x, m) \
   decaf_handle_assert(x, #x, m)

#define decaf_check(x) \
   decaf_handle_assert(x, #x, "")

#define decaf_check_warn_once(x) \
   decaf_handle_warn_once_assert(x, #x, "")

#define decaf_abort(m) \
   decaf_handle_assert(false, "0", m)
