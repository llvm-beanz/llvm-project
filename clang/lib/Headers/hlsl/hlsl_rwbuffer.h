//===----- hlsl_rwbuffer.h - HLSL definitions for RWBuffer types ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _HLSL_HLSL_RWBUFFER_H_
#define _HLSL_HLSL_RWBUFFER_H_


namespace hlsl2 {

template<typename T> requires __detail::is_line_vector<T>
struct RWBuffer {
  RWBuffer() {}
  using handle_t = __hlsl_resource_t [[hlsl::contained_type(T)]] [[hlsl::resource_class(UAV)]];
  //handle_t h;

  T TmpVal;

  T &operator[](uint) {
    return TmpVal;
  }

  T &operator[](uint) const {
    return TmpVal;
  }
};

}

#endif // _HLSL_HLSL_RWBUFFER_H_
