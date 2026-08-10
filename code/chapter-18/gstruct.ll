; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

%struct.Counter = type { i64 }

@g = global %struct.Counter zeroinitializer
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__pyxc.global_init, ptr null }]

define void @__pyxc.global_init() {
entry:
  store %struct.Counter zeroinitializer, ptr @g, align 8
  ret void
}
