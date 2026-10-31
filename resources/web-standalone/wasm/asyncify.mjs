// Minimal runtime for a WebAssembly module post-processed by Binaryen's
// Asyncify pass (`wasm-opt --asyncify`). It lets one import return a Promise:
// the module's call stack is unwound into a buffer in linear memory, the
// exported call returns to JS as a Promise, and once the import's Promise
// settles the stack is rewound and execution resumes after the import.
//
// Approach after GoogleChromeLabs/asyncify (Apache-2.0); written for wfweb
// because that library keeps its unwind buffer in the first KB of memory,
// which is too small for a deep Rust iterator chain. Ours is allocated by
// the module itself (any exported allocator) and sized by the caller.
//
// Protocol (Binaryen):
//   asyncify_get_state(): 0 normal, 1 unwinding, 2 rewinding
//   import wants to suspend  -> asyncify_start_unwind(data); return anything
//   export returns to JS     -> asyncify_stop_unwind(); await the Promise
//   resume                   -> asyncify_start_rewind(data); call the same
//                               export again with the same arguments; the
//                               import is re-entered: asyncify_stop_rewind()
//                               and return the Promise's value
//   data: [i32 current, i32 end] followed by the stack buffer.

const NORMAL = 0, UNWINDING = 1, REWINDING = 2;

export class Asyncify {
    constructor() {
        this.exports = null;
        this.dataAddr = 0;
        this.pending = null;   // the Promise the suspended import is waiting on
        this.resumeValue = 0;  // what the import returns when rewound
        this.busy = false;
    }

    /** Wrap an import that may return a Promise. */
    wrapImport(fn) {
        return (...args) => {
            if (this.exports.asyncify_get_state() === REWINDING) {
                this.exports.asyncify_stop_rewind();
                return this.resumeValue;
            }
            const v = fn(...args);
            if (!v || typeof v.then !== 'function') return v;
            this.pending = v;
            this.exports.asyncify_start_unwind(this.dataAddr);
            return 0;
        };
    }

    /** Wrap an export whose call path can reach a wrapped import. */
    wrapExport(fn) {
        return async (...args) => {
            if (this.busy) throw new Error('asyncify: one suspended call at a time');
            this.busy = true;
            try {
                let result = fn(...args);
                while (this.exports.asyncify_get_state() === UNWINDING) {
                    this.exports.asyncify_stop_unwind();
                    this.resumeValue = await this.pending;
                    this.pending = null;
                    this.exports.asyncify_start_rewind(this.dataAddr);
                    result = fn(...args);
                }
                return result;
            } finally {
                this.busy = false;
            }
        };
    }

    /**
     * Attach to an instance and reserve `size` bytes of unwind buffer with
     * `alloc(bytes) -> ptr`, an export of the module.
     */
    attach(instance, alloc, size) {
        this.exports = instance.exports;
        const total = size + 8;
        this.dataAddr = alloc(total);
        if (!this.dataAddr) throw new Error('asyncify: buffer allocation failed');
        const view = new Int32Array(this.exports.memory.buffer, this.dataAddr, 2);
        view[0] = this.dataAddr + 8;
        view[1] = this.dataAddr + total;
        if (this.exports.asyncify_get_state() !== NORMAL) throw new Error('asyncify: bad initial state');
    }
}
