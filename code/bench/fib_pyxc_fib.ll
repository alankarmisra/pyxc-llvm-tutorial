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
