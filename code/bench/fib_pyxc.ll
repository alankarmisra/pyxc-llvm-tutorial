; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

declare double @printd(double) local_unnamed_addr

; Function Attrs: nofree nosync nounwind memory(none)
define i64 @fib(i64 %n) local_unnamed_addr #0 {
entry:
  %cmptmp11 = icmp slt i64 %n, 2
  br i1 %cmptmp11, label %common.ret, label %ifcont

common.ret:                                       ; preds = %ifcont, %entry
  %accumulator.tr.lcssa = phi i64 [ 0, %entry ], [ %addtmp, %ifcont ]
  %n.tr.lcssa = phi i64 [ %n, %entry ], [ %subtmp6, %ifcont ]
  %accumulator.ret.tr = add i64 %n.tr.lcssa, %accumulator.tr.lcssa
  ret i64 %accumulator.ret.tr

ifcont:                                           ; preds = %entry, %ifcont
  %n.tr13 = phi i64 [ %subtmp6, %ifcont ], [ %n, %entry ]
  %accumulator.tr12 = phi i64 [ %addtmp, %ifcont ], [ 0, %entry ]
  %subtmp = add nsw i64 %n.tr13, -1
  %calltmp = tail call i64 @fib(i64 %subtmp)
  %subtmp6 = add nsw i64 %n.tr13, -2
  %addtmp = add i64 %calltmp, %accumulator.tr12
  %cmptmp = icmp samesign ult i64 %n.tr13, 4
  br i1 %cmptmp, label %common.ret, label %ifcont
}

define void @__pyxc.user_main() local_unnamed_addr {
entry:
  %calltmp = tail call i64 @fib(i64 20)
  %sitofp = sitofp i64 %calltmp to double
  %calltmp2 = tail call double @printd(double %sitofp)
  ret void
}

define noundef i32 @main() local_unnamed_addr {
entry:
  %calltmp.i = tail call i64 @fib(i64 20)
  %sitofp.i = sitofp i64 %calltmp.i to double
  %calltmp2.i = tail call double @printd(double %sitofp.i)
  ret i32 0
}

attributes #0 = { nofree nosync nounwind memory(none) }
