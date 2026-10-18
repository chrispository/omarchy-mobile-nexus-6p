# SPDX-License-Identifier: MIT
"""Print model, qcom,msm-id, qcom,board-id and qcom,pmic-id of every DTB in a file
(an appended-DTB kernel such as Image.gz-dtb). The angler bootloader picks the
DTB whose IDs match the phone; a mismatch fails with "dtb not found"."""
import sys, struct
data = open(sys.argv[1], 'rb').read()
i = 0
while True:
    i = data.find(b'\xd0\x0d\xfe\xed', i)
    if i < 0: break
    tot, off_struct, off_strings = struct.unpack('>III', data[i+4:i+16])
    if tot < 0x100 or i + tot > len(data): i += 4; continue
    blob = data[i:i+tot]; strs = blob[off_strings:]
    p = off_struct; depth = 0; out = {}; model = None
    while p < tot:
        tag = struct.unpack('>I', blob[p:p+4])[0]; p += 4
        if tag == 1:
            e = blob.index(b'\0', p); p = (e + 4) & ~3; depth += 1
        elif tag == 2: depth -= 1
        elif tag == 3:
            ln, no = struct.unpack('>II', blob[p:p+8]); p += 8
            name = strs[no:strs.index(b'\0', no)].decode(); val = blob[p:p+ln]; p = (p + ln + 3) & ~3
            if depth == 1 and name in ('qcom,msm-id', 'qcom,board-id', 'qcom,pmic-id'):
                out[name] = [hex(x) for x in struct.unpack('>%dI' % (ln // 4), val)]
            if depth == 1 and name == 'model': model = val.rstrip(b'\0').decode(errors='replace')
        elif tag == 9: break
    print(hex(i), tot, model, out)
    i += tot
