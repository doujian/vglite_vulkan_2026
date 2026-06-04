# SPV Shader Regeneration Guide

## Prerequisites

```bash
# Verify glslangValidator is available
glslangValidator --version
```

## Steps

### 1. Compile shader to raw SPIR-V binary

```bash
glslangValidator -V shaders/blit.frag -o build/blit.frag.spv
glslangValidator -V shaders/blit_native.frag -o build/blit_native.frag.spv
```

**DO NOT use `--vn` flag** — it outputs C source text, not raw SPV binary.

### 2. Convert SPV binary to C header

```python
python -c "
import sys
data = open('build/blit.frag.spv','rb').read()
words = len(data)//4
lines = []
lines.append('// SPIR-V shader data - %d words, %d bytes' % (words, len(data)))
lines.append('const uint32_t g_frag_spv_size = %d;' % words)
lines.append('const uint32_t g_frag_spv_data[] = {')
for i in range(0, len(data), 32):
    chunk = data[i:i+32]
    vals = ['0x%08xu' % int.from_bytes(chunk[j:j+4],'little') for j in range(0,len(chunk),4)]
    lines.append('    ' + ', '.join(vals) + ',')
lines.append('};')
with open('src/spv_frag.h', 'wb') as f:
    f.write(('\n'.join(lines)).encode('ascii'))
print('Done: %d words' % words)
"
```

### 3. Verify

```python
python -c "
data = open('build/blit.frag.spv','rb').read()
w = int.from_bytes(data[0:4], 'little')
print('Magic: 0x%08x (expect 0x07230203)' % w)
assert w == 0x07230203, 'NOT a valid SPIR-V binary!'
print('OK')
"
```

### 4. Rebuild

```bash
cmake --build build --config Release --clean-first
```

Must use `--clean-first` because CMake may not detect header-only changes.

## Common Pitfalls

### Pitfall 1: `--vn` outputs C text, not binary

```bash
# WRONG — outputs C source like "const uint32_t spv_data[] = { 0x07230203, ... }"
glslangValidator -V shader.frag -o output.spv --vn spv_data

# CORRECT — outputs raw SPIR-V binary
glslangValidator -V shader.frag -o output.spv
```

### Pitfall 2: Python `>` redirect causes CRLF corruption on Windows

```python
# WRONG — Windows text mode adds \r, may corrupt binary data
python script.py > src/spv_frag.h

# CORRECT — use 'wb' mode in Python
with open('src/spv_frag.h', 'wb') as f:
    f.write(content.encode('ascii'))
```

### Pitfall 3: Incremental build doesn't pick up header changes

SPV headers are `#include`d into .c files. If only the header changed but the .c
timestamp wasn't updated, CMake may skip recompilation. Always `--clean-first`.

### Pitfall 4: Invalid magic number in runtime

```
[Vulkan] Validation Error: Invalid SPIR-V magic number
```

Check with:
```python
python -c "
h = open('src/spv_frag.h','r').read()
import re
m = re.search(r'0x([0-9a-f]+)u', h)
print('First word:', m.group(0))
# Should be 0x07230203u, NOT 0x202f2f09u (text) or anything else
"
```

If it's `0x202f2f09u`, the "SPV" file is actually C source text — redo step 1.

## File Reference

| Shader | SPV Output | C Header | Symbol Name |
|--------|-----------|----------|-------------|
| `shaders/blit.vert` | `build/blit.vert.spv` | `src/spv_vert.h` | `g_vert_spv_data/size` |
| `shaders/blit.frag` | `build/blit.frag.spv` | `src/spv_frag.h` | `g_frag_spv_data/size` |
| `shaders/blit_native.frag` | `build/blit_native.frag.spv` | `src/spv_native_frag.h` | `g_native_frag_spv_data/size` |
