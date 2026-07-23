import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_binop_precedence.py <pyxc.ebnf> <pyxc.cpp>")
        return 2

    ebnf = Path(sys.argv[1]).read_text()
    cpp = Path(sys.argv[2]).read_text()

    m = re.search(r"builtinbinaryop\s*=\s*([^;]+);", ebnf)
    if not m:
        print("builtinbinaryop not found")
        return 1
    ops = re.findall(r'"([+\-*/<>!=]+)"', m.group(1))

    m2 = re.search(r"DefaultBinopPrecedence\s*=\s*\{(.*?)\};", cpp, re.S)
    if not m2:
        print("DefaultBinopPrecedence not found")
        return 1
    ops_map = set(re.findall(r"\{\s*([^,\s]+)\s*,", m2.group(1)))

    missing = []
    for op in ops:
        if len(op) == 1:
            key = f"'{op}'"
        else:
            if op == "==":
                key = "tok_eq"
            elif op == "!=":
                key = "tok_neq"
            elif op == "<=":
                key = "tok_leq"
            elif op == ">=":
                key = "tok_geq"
            else:
                key = ""
        if key not in ops_map:
            missing.append(op)

    if missing:
        print(f"Missing in DefaultBinopPrecedence: {missing}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
