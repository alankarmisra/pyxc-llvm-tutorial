; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

declare double @printd(double)

define i32 @add(i32 %a, i32 %b) {
entry:
  %b2 = alloca i32, align 4
  %a1 = alloca i32, align 4
  store i32 %a, ptr %a1, align 4
  store i32 %b, ptr %b2, align 4
  %a3 = load i32, ptr %a1, align 4
  %b4 = load i32, ptr %b2, align 4
  %addtmp = add i32 %a3, %b4
  ret i32 %addtmp
}

define i64 @__pyxc.user_main() {
entry:
  %y = alloca double, align 8
  %x = alloca i32, align 4
  %calltmp = call i32 @add(i32 10, i32 5)
  store i32 %calltmp, ptr %x, align 4
  %x1 = load i32, ptr %x, align 4
  %sitofp = sitofp i32 %x1 to double
  store double %sitofp, ptr %y, align 8
  %y2 = load double, ptr %y, align 8
  %calltmp3 = call double @printd(double %y2)
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
