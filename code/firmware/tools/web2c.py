#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
web2c.py —— 把 Web 页面源码转换为 C 字节数组（PRD NFR-8 / FR-WEB-2）

用法:
    python web2c.py <input.html> <output.h>

生成 <output.h>，内含:
    static const unsigned char smu_webpage[] = {...};
    #define SMU_WEBPAGE_LEN <bytes>

设计约束: 单页应用 ≤64KB（构建时检查，超限报错）。
"""
import sys
import os

MAX_BYTES = 64 * 1024


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: web2c.py <input.html> <output.h>")
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()

    if len(data) > MAX_BYTES:
        print("ERROR: %s is %d bytes > %d limit (PRD FR-WEB-2)"
              % (src, len(data), MAX_BYTES))
        return 2

    guard = os.path.basename(dst).upper().replace(".", "_")
    lines = []
    lines.append("/**")
    lines.append(" * @file   %s" % os.path.basename(dst))
    lines.append(" * @brief  自动生成，请勿手改！源: %s (%d bytes)" % (os.path.basename(src), len(data)))
    lines.append(" * @note   由 tools/web2c.py 生成（PRD NFR-8）")
    lines.append(" */")
    lines.append("#ifndef %s" % guard)
    lines.append("#define %s" % guard)
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("#define SMU_WEBPAGE_LEN %dU" % len(data))
    lines.append("")
    lines.append("static const uint8_t smu_webpage[] = {")
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("  " + ",".join("0x%02X" % b for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* %s */" % guard)
    lines.append("")

    open(dst, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    print("OK: %s -> %s (%d bytes, limit %d)" % (src, dst, len(data), MAX_BYTES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
