; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

declare double @printd(double)

define double @sq(double %x) {
entry:
  %multmp = fmul double %x, %x
  ret double %multmp
}

define double @__pyxc.user_main() {
entry:
  %calltmp = call double @sq(double 3.000000e+00)
  %calltmp1 = call double @printd(double %calltmp)
  ret double 0.000000e+00
}

define i32 @main() {
entry:
  %0 = call double @__pyxc.user_main()
  ret i32 0
}
