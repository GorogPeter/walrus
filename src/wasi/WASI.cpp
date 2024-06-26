/*
 * Copyright (c) 2023-present Samsung Electronics Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef ENABLE_WASI

#include "wasi/WASI.h"
#include "runtime/Value.h"
#include "runtime/Memory.h"
#include "runtime/Instance.h"

// https://github.com/WebAssembly/WASI/blob/main/legacy/preview1/docs.md

namespace Walrus {

uvwasi_t* WASI::g_uvwasi;
WASI::WasiFuncInfo WASI::g_wasiFunctions[WasiFuncIndex::FuncEnd];

void WASI::initialize(uvwasi_t* uvwasi)
{
    ASSERT(!!uvwasi);
    g_uvwasi = uvwasi;

    // fill wasi function table
#define WASI_FUNC_TABLE(NAME, FUNCTYPE)                                                       \
    g_wasiFunctions[WasiFuncIndex::NAME##FUNC].name = #NAME;                                  \
    g_wasiFunctions[WasiFuncIndex::NAME##FUNC].functionType = DefinedFunctionTypes::FUNCTYPE; \
    g_wasiFunctions[WasiFuncIndex::NAME##FUNC].ptr = &WASI::NAME;
    FOR_EACH_WASI_FUNC(WASI_FUNC_TABLE)
#undef WASI_FUNC_TABLE
}

WASI::WasiFuncInfo* WASI::find(const std::string& funcName)
{
    for (unsigned i = 0; i < WasiFuncIndex::FuncEnd; ++i) {
        if (g_wasiFunctions[i].name == funcName) {
            return &g_wasiFunctions[i];
        }
    }
    return nullptr;
}

void WASI::proc_exit(ExecutionState& state, Value* argv, Value* result, Instance* instance)
{
    ASSERT(argv[0].type() == Value::I32);
    uvwasi_proc_exit(WASI::g_uvwasi, argv[0].asI32());
    ASSERT_NOT_REACHED();
}

void WASI::proc_raise(ExecutionState& state, Value* argv, Value* result, Instance* instance)
{
    ASSERT(argv[0].type() == Value::I32);
    result[0] = Value(uvwasi_proc_raise(WASI::g_uvwasi, argv[0].asI32()));
}

void WASI::fd_write(ExecutionState& state, Value* argv, Value* result, Instance* instance)
{
    uint32_t fd = argv[0].asI32();
    uint32_t iovptr = argv[1].asI32();
    uint32_t iovcnt = argv[2].asI32();
    uint32_t out = argv[3].asI32();

    if (uint64_t(iovptr) + iovcnt >= instance->memory(0)->sizeInByte()) {
        result[0] = Value(static_cast<int16_t>(WasiErrNo::inval));
        return;
    }

    std::vector<uvwasi_ciovec_t> iovs(iovcnt);
    for (uint32_t i = 0; i < iovcnt; i++) {
        iovs[i].buf = instance->memory(0)->buffer() + *reinterpret_cast<uint32_t*>(instance->memory(0)->buffer() + iovptr + i * 8);
        iovs[i].buf_len = *reinterpret_cast<uint32_t*>(instance->memory(0)->buffer() + iovptr + 4 + i * 8);
    }

    uvwasi_size_t* out_addr = (uvwasi_size_t*)(instance->memory(0)->buffer() + out);

    result[0] = Value(static_cast<int16_t>(uvwasi_fd_write(WASI::g_uvwasi, fd, iovs.data(), iovs.size(), out_addr)));
    *(instance->memory(0)->buffer() + out) = *out_addr;
}

void WASI::fd_read(ExecutionState& state, Value* argv, Value* result, Instance* instance)
{
    uint32_t fd = argv[0].asI32();
    uint32_t iovptr = argv[1].asI32();
    uint32_t iovcnt = argv[2].asI32();
    uint32_t out = argv[3].asI32();

    std::vector<uvwasi_iovec_t> iovs(iovcnt);
    for (uint32_t i = 0; i < iovcnt; i++) {
        iovs[i].buf = instance->memory(0)->buffer() + *reinterpret_cast<uint32_t*>(instance->memory(0)->buffer() + iovptr + i * 8);
        iovs[i].buf_len = *reinterpret_cast<uint32_t*>(instance->memory(0)->buffer() + iovptr + 4 + i * 8);
    }

    uvwasi_size_t* out_addr = (uvwasi_size_t*)(instance->memory(0)->buffer() + out);

    result[0] = Value(static_cast<int16_t>(uvwasi_fd_read(WASI::g_uvwasi, fd, iovs.data(), iovs.size(), out_addr)));
    *(instance->memory(0)->buffer() + out) = *out_addr;
}

void WASI::fd_close(ExecutionState& state, Value* argv, Value* result, Instance* instance)
{
    uint32_t fd = argv[0].asI32();

    result[0] = Value(static_cast<int16_t>(uvwasi_fd_close(WASI::g_uvwasi, fd)));
}

void WASI::fd_seek(ExecutionState& state, Value* argv, Value* result, Instance* instance)
{
    uint32_t fd = argv[0].asI32();
    uint64_t fileDelta = argv[1].asI32();
    uint32_t whence = argv[2].asI32();
    uint64_t newOffset = argv[3].asI32();

    uvwasi_filesize_t out_addr = *(instance->memory(0)->buffer() + newOffset);

    result[0] = Value(static_cast<int16_t>(uvwasi_fd_seek(WASI::g_uvwasi, fd, fileDelta, whence, &out_addr)));
    *(instance->memory(0)->buffer() + newOffset) = out_addr;
}

void WASI::path_open(ExecutionState& state, Value* argv, Value* result, Instance* instance)
{
    uint32_t fd = argv[0].asI32();
    uint32_t dirflags = argv[1].asI32();
    uint32_t path_offset = argv[2].asI32();
    uint32_t len = argv[3].asI32();
    uint32_t oflags = argv[4].asI32();
    uint64_t rights = argv[5].asI64();
    uint64_t right_inheriting = argv[6].asI64();
    uint32_t fdflags = argv[7].asI32();
    uint32_t ret_fd_offset = argv[8].asI32();

    uvwasi_fd_t* ret_fd = reinterpret_cast<uvwasi_fd_t*>(instance->memory(0)->buffer() + ret_fd_offset);

    const char* path = reinterpret_cast<char*>(instance->memory(0)->buffer() + path_offset);

    result[0] = Value(static_cast<uint16_t>(
        uvwasi_path_open(WASI::g_uvwasi,
                         fd,
                         dirflags,
                         path,
                         len,
                         oflags,
                         rights,
                         right_inheriting,
                         fdflags,
                         ret_fd)));
}

void WASI::environ_sizes_get(ExecutionState& state, Value* argv, Value* result, Instance* instance)
{
    uint32_t count = argv[0].asI32();
    uint32_t buf = argv[1].asI32();

    uvwasi_size_t* uvCount = reinterpret_cast<uvwasi_size_t*>(instance->memory(0)->buffer() + count);
    uvwasi_size_t* uvBufSize = reinterpret_cast<uvwasi_size_t*>(instance->memory(0)->buffer() + buf);

    result[0] = Value(static_cast<uint16_t>(uvwasi_environ_sizes_get(WASI::g_uvwasi, uvCount, uvBufSize)));
}

void WASI::environ_get(ExecutionState& state, Value* argv, Value* result, Instance* instance)
{
    uint32_t env = argv[0].asI32();
    uint32_t environBuf = argv[1].asI32();

    char** uvEnviron = reinterpret_cast<char**>(instance->memory(0)->buffer() + env);
    char* uvEnvironBuf = reinterpret_cast<char*>(instance->memory(0)->buffer() + environBuf);

    result[0] = Value(static_cast<uint16_t>(uvwasi_environ_get(WASI::g_uvwasi, uvEnviron, uvEnvironBuf)));
}

void WASI::random_get(ExecutionState& state, Value* argv, Value* result, Instance* instance)
{
    ASSERT(argv[0].type() == Value::I32);
    ASSERT(argv[1].type() == Value::I32);
    if (uint64_t(argv[0].asI32()) + argv[1].asI32() >= instance->memory(0)->sizeInByte()) {
        result[0] = Value(WasiErrNo::inval);
    }

    void* buf = (void*)(instance->memory(0)->buffer() + argv[0].asI32());
    uvwasi_size_t length = argv[1].asI32();
    result[0] = Value(uvwasi_random_get(WASI::g_uvwasi, buf, length));
}
} // namespace Walrus

#endif
