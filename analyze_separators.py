#!/usr/bin/env python3
import re
import os

# 解析grep结果
results = """
d:\\QtScrcpy2\\client\\src\\core\\impl\\ZeroCopyDecoder.cpp:296
d:\\QtScrcpy2\\client\\src\\core\\impl\\ZeroCopyDecoder.cpp:298
d:\\QtScrcpy2\\client\\src\\core\\impl\\ZeroCopyDecoder.cpp:311
d:\\QtScrcpy2\\client\\src\\core\\impl\\ZeroCopyDecoder.cpp:313
d:\\QtScrcpy2\\client\\src\\render\\qyuvopenglwidget.cpp:44
d:\\QtScrcpy2\\client\\src\\render\\qyuvopenglwidget.cpp:47
d:\\QtScrcpy2\\client\\src\\ui\\videoform.cpp:45
d:\\QtScrcpy2\\client\\src\\ui\\videoform.cpp:47
d:\\QtScrcpy2\\client\\src\\ui\\videoform.cpp:49
d:\\QtScrcpy2\\client\\src\\ui\\videoform.cpp:51
d:\\QtScrcpy2\\client\\src\\ui\\widgets\\keepratiowidget.h:7
d:\\QtScrcpy2\\client\\src\\ui\\widgets\\keepratiowidget.h:11
d:\\QtScrcpy2\\client\\src\\ui\\widgets\\magneticwidget.h:7
d:\\QtScrcpy2\\client\\src\\ui\\widgets\\magneticwidget.h:11
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.h:17
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.h:19
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.h:33
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.h:41
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:285
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:287
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:326
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:328
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:337
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:339
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:368
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:370
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:382
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:384
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:415
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:417
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:442
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:444
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:510
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:512
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:528
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:530
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:545
d:\\QtScrcpy2\\client\\src\\ui\\ScriptTipWidget.cpp:547
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:654
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:656
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:846
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:848
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:1009
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:1011
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:1441
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:1443
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:1592
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:1594
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2077
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2079
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2136
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2138
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2220
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2222
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2271
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2273
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2508
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2510
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2676
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2678
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2741
d:\\QtScrcpy2\\client\\src\\ui\\selectioneditordialog.h:2743
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:564
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:566
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:570
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:572
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:663
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:665
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:705
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:707
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:721
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:723
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:754
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:756
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:767
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:769
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:796
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:798
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:825
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:827
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:868
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:870
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:901
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:903
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:918
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:920
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:962
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:964
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:1000
d:\\QtScrcpy2\\client\\src\\ui\\scripteditordialog.h:1002
d:\\QtScrcpy2\\client\\src\\ui\\KeyMapEditView.h:17
d:\\QtScrcpy2\\client\\src\\ui\\KeyMapEditView.h:19
d:\\QtScrcpy2\\client\\src\\ui\\KeyMapEditView.h:36
d:\\QtScrcpy2\\client\\src\\ui\\KeyMapEditView.h:38
d:\\QtScrcpy2\\client\\src\\ui\\KeyMapEditView.h:54
d:\\QtScrcpy2\\client\\src\\ui\\KeyMapEditView.h:56
d:\\QtScrcpy2\\client\\src\\ui\\KeyMapEditView.h:73
d:\\QtScrcpy2\\client\\src\\ui\\KeyMapEditView.h:76
"""

# 按文件组织数据
files_data = {}
for line in results.strip().split('\n'):
    if ':' in line:
        parts = line.rsplit(':', 1)
        if len(parts) == 2:
            filepath = parts[0]
            linenum = int(parts[1])
            if filepath not in files_data:
                files_data[filepath] = []
            files_data[filepath].append(linenum)

# 查找连续两行的情况（行号相差1）
print("=" * 80)
print("分析结果：查找真正连续的双重分隔符（行号相差1）")
print("=" * 80)

consecutive_found = False
for filepath, lines in sorted(files_data.items()):
    lines.sort()
    consecutive_pairs = []
    
    for i in range(len(lines) - 1):
        # 检查是否行号连续（相差1）
        if lines[i+1] - lines[i] == 1:
            consecutive_pairs.append((lines[i], lines[i+1]))
    
    if consecutive_pairs:
        consecutive_found = True
        short_path = filepath.replace('d:\\QtScrcpy2\\client\\src\\', '')
        for pair in consecutive_pairs:
            # 读取文件验证
            try:
                with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                    file_lines = f.readlines()
                    line1 = file_lines[pair[0]-1].strip() if pair[0]-1 < len(file_lines) else ""
                    line2 = file_lines[pair[1]-1].strip() if pair[1]-1 < len(file_lines) else ""
                    # 检查是否都是纯分隔符
                    is_separator1 = bool(re.match(r'^//\s*[=\-]{5,}\s*$', line1))
                    is_separator2 = bool(re.match(r'^//\s*[=\-]{5,}\s*$', line2))
                    
                    if is_separator1 and is_separator2:
                        print(f"文件: {short_path}, 行{pair[0]}-{pair[1]}")
                        print(f"  {pair[0]}: {line1}")
                        print(f"  {pair[1]}: {line2}")
                        print()
            except Exception as e:
                print(f"无法读取文件 {filepath}: {e}")

if not consecutive_found:
    print("未找到真正连续的双重分隔符（行号相差1）")
    print()
    print("注：所有检测到的分隔符都是正常的三行格式（上分隔符 + 描述 + 下分隔符）")

print("=" * 80)
