; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

@.str.0 = private unnamed_addr constant [6 x i8] c"hello\00", align 1

declare i64 @puts(ptr)

define i64 @__pyxc.user_main() {
entry:
  %strptr = getelementptr inbounds [6 x i8], ptr @.str.0, i64 0, i64 0
  %calltmp = call i64 @puts(ptr %strptr)
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
