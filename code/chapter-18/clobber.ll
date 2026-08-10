; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

%struct.Box = type { i64 }

define void @clobber(%struct.Box %b) {
entry:
  %b1 = alloca %struct.Box, align 8
  store %struct.Box %b, ptr %b1, align 8
  %fieldptr = getelementptr inbounds nuw %struct.Box, ptr %b1, i32 0, i32 0
  store i64 0, ptr %fieldptr, align 8
  ret void
}

define i64 @__pyxc.user_main() {
entry:
  %b = alloca %struct.Box, align 8
  store %struct.Box zeroinitializer, ptr %b, align 8
  %fieldptr = getelementptr inbounds nuw %struct.Box, ptr %b, i32 0, i32 0
  store i64 99, ptr %fieldptr, align 8
  %b1 = load %struct.Box, ptr %b, align 8
  call void @clobber(%struct.Box %b1)
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
