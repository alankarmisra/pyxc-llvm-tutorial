; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

%struct.Vec2 = type { double, double }

define i64 @__pyxc.user_main() {
entry:
  %v = alloca %struct.Vec2, align 8
  store %struct.Vec2 zeroinitializer, ptr %v, align 8
  %fieldptr = getelementptr inbounds nuw %struct.Vec2, ptr %v, i32 0, i32 0
  store double 1.000000e+00, ptr %fieldptr, align 8
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
