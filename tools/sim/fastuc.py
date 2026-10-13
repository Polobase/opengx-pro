"""Direct ctypes access to the Unicorn C API for the hot paths (block/code hooks, MMIO callbacks,
register access). The public Python binding wraps every callback twice and converts register
types on each access, which dominates the run time of an interrupt-heavy emulation.

Exceptions raised inside callbacks stop the emulation and are re-raised by `check()`.
"""

import ctypes

from unicorn import UC_HOOK_BLOCK, UC_HOOK_CODE
from unicorn.unicorn_py3 import unicorn as _u

uclib = _u.uclib


class RegBatch:
    def __init__(self, handle, ids):
        n = len(ids)
        self.h, self.n = handle, n
        self.ids = (ctypes.c_int * n)(*ids)
        self.vals = (ctypes.c_uint64 * n)()
        base = ctypes.addressof(self.vals)
        self.ptrs = (ctypes.c_void_p * n)(*[base + 8 * i for i in range(n)])

    def read(self):
        uclib.uc_reg_read_batch(self.h, self.ids, self.ptrs, self.n)
        return [v & 0xFFFFFFFF for v in self.vals]

    def write(self, values):
        self.vals[:] = values
        uclib.uc_reg_write_batch(self.h, self.ids, self.ptrs, self.n)


class Fast:
    def __init__(self, uc):
        self.uc = uc
        self.h = uc._uch
        self.keep = []
        self.error = None
        self._v = ctypes.c_uint64()
        self._ref = ctypes.byref(self._v)

    def check(self):
        if self.error is not None:
            e, self.error = self.error, None
            raise e

    def _fail(self, e):
        if self.error is None:
            self.error = e
        uclib.uc_emu_stop(self.h)

    def reg(self, rid):
        self._v.value = 0
        uclib.uc_reg_read(self.h, rid, self._ref)
        return self._v.value & 0xFFFFFFFF

    def set_reg(self, rid, value):
        self._v.value = value & 0xFFFFFFFF
        uclib.uc_reg_write(self.h, rid, self._ref)

    def batch(self, ids):
        return RegBatch(self.h, ids)

    def _hook(self, htype, fn, begin, end):
        def cb(handle, address, size, user):
            try:
                fn(address, size)
            except BaseException as e:     # noqa: BLE001 - reported by check()
                self._fail(e)
        f = _u.HOOK_CODE_CFUNC(cb)
        self.keep.append(f)
        handle = _u.uc_hook_h()
        status = uclib.uc_hook_add(self.h, ctypes.byref(handle), htype, ctypes.cast(f, ctypes.c_void_p), None,
                                   ctypes.c_uint64(begin), ctypes.c_uint64(end))
        if status:
            raise _u.UcError(status)

    def hook_block(self, fn, begin=1, end=0):
        self._hook(UC_HOOK_BLOCK, fn, begin, end)

    def hook_code(self, fn, begin, end):
        self._hook(UC_HOOK_CODE, fn, begin, end)

    def mmio_map(self, base, size, read, write):
        def rcb(handle, off, sz, user):
            try:
                return read(off, sz)
            except BaseException as e:     # noqa: BLE001
                self._fail(e)
                return 0

        def wcb(handle, off, sz, value, user):
            try:
                write(off, sz, value)
            except BaseException as e:     # noqa: BLE001
                self._fail(e)
        fr, fw = _u.MMIO_READ_CFUNC(rcb), _u.MMIO_WRITE_CFUNC(wcb)
        self.keep += [fr, fw]
        status = uclib.uc_mmio_map(self.h, base, size, fr, None, fw, None)
        if status:
            raise _u.UcError(status)
