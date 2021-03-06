// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_OPTIMIZED_COMPILATION_INFO_H_
#define V8_CODEGEN_OPTIMIZED_COMPILATION_INFO_H_

#include <memory>

#include "src/codegen/bailout-reason.h"
#include "src/codegen/source-position-table.h"
#include "src/codegen/tick-counter.h"
#include "src/common/globals.h"
#include "src/diagnostics/basic-block-profiler.h"
#include "src/execution/frames.h"
#include "src/handles/handles.h"
#include "src/handles/persistent-handles.h"
#include "src/objects/objects.h"
#include "src/utils/utils.h"
#include "src/utils/vector.h"

namespace v8 {

namespace tracing {
class TracedValue;
}

namespace internal {

class DeferredHandles;
class FunctionLiteral;
class Isolate;
class JavaScriptFrame;
class JSGlobalObject;
class Zone;

namespace wasm {
struct WasmCompilationResult;
}

// OptimizedCompilationInfo encapsulates the information needed to compile
// optimized code for a given function, and the results of the optimized
// compilation.
class V8_EXPORT_PRIVATE OptimizedCompilationInfo final {
 public:
  // Various configuration flags for a compilation, as well as some properties
  // of the compiled code produced by a compilation.

#define FLAGS(V)                                                              \
  V(FunctionContextSpecializing, function_context_specializing, 0)            \
  V(Inlining, inlining, 1)                                                    \
  V(DisableFutureOptimization, disable_future_optimization, 2)                \
  V(Splitting, splitting, 3)                                                  \
  V(SourcePositions, source_positions, 4)                                     \
  V(BailoutOnUninitialized, bailout_on_uninitialized, 5)                      \
  V(LoopPeeling, loop_peeling, 6)                                             \
  V(UntrustedCodeMitigations, untrusted_code_mitigations, 7)                  \
  V(SwitchJumpTable, switch_jump_table, 8)                                    \
  V(CalledWithCodeStartRegister, called_with_code_start_register, 9)          \
  V(PoisonRegisterArguments, poison_register_arguments, 10)                   \
  V(AllocationFolding, allocation_folding, 11)                                \
  V(AnalyzeEnvironmentLiveness, analyze_environment_liveness, 12)             \
  V(TraceTurboJson, trace_turbo_json, 13)                                     \
  V(TraceTurboGraph, trace_turbo_graph, 14)                                   \
  V(TraceTurboScheduled, trace_turbo_scheduled, 15)                           \
  V(TraceTurboAllocation, trace_turbo_allocation, 16)                         \
  V(TraceHeapBroker, trace_heap_broker, 17)                                   \
  V(WasmRuntimeExceptionSupport, wasm_runtime_exception_support, 18)          \
  V(TurboControlFlowAwareAllocation, turbo_control_flow_aware_allocation, 19) \
  V(TurboPreprocessRanges, turbo_preprocess_ranges, 20)                       \
  V(ConcurrentInlining, concurrent_inlining, 21)                              \
  V(NativeContextIndependent, native_context_independent, 22)

  enum Flag {
#define DEF_ENUM(Camel, Lower, Bit) k##Camel = 1 << Bit,
    FLAGS(DEF_ENUM)
#undef DEF_ENUM
  };

#define DEF_GETTER(Camel, Lower, Bit) \
  bool Lower() const {                \
    DCHECK(FlagGetIsValid(k##Camel)); \
    return GetFlag(k##Camel);         \
  }
  FLAGS(DEF_GETTER)
#undef DEF_GETTER

#define DEF_SETTER(Camel, Lower, Bit) \
  void set_##Lower() {                \
    DCHECK(FlagSetIsValid(k##Camel)); \
    SetFlag(k##Camel);                \
  }
  FLAGS(DEF_SETTER)
#undef DEF_SETTER

#ifdef DEBUG
  bool FlagGetIsValid(Flag flag) const;
  bool FlagSetIsValid(Flag flag) const;
#endif  // DEBUG

  // Construct a compilation info for optimized compilation.
  OptimizedCompilationInfo(Zone* zone, Isolate* isolate,
                           Handle<SharedFunctionInfo> shared,
                           Handle<JSFunction> closure,
                           bool native_context_independent);
  // Construct a compilation info for stub compilation, Wasm, and testing.
  OptimizedCompilationInfo(Vector<const char> debug_name, Zone* zone,
                           Code::Kind code_kind);

  ~OptimizedCompilationInfo();

  Zone* zone() { return zone_; }
  bool is_osr() const { return !osr_offset_.IsNone(); }
  Handle<SharedFunctionInfo> shared_info() const { return shared_info_; }
  bool has_shared_info() const { return !shared_info().is_null(); }
  Handle<BytecodeArray> bytecode_array() const { return bytecode_array_; }
  bool has_bytecode_array() const { return !bytecode_array_.is_null(); }
  Handle<JSFunction> closure() const { return closure_; }
  Handle<Code> code() const { return code_; }
  Code::Kind code_kind() const { return code_kind_; }
  int32_t builtin_index() const { return builtin_index_; }
  void set_builtin_index(int32_t index) { builtin_index_ = index; }
  BailoutId osr_offset() const { return osr_offset_; }
  JavaScriptFrame* osr_frame() const { return osr_frame_; }

  void SetPoisoningMitigationLevel(PoisoningMitigationLevel poisoning_level) {
    poisoning_level_ = poisoning_level;
  }
  PoisoningMitigationLevel GetPoisoningMitigationLevel() const {
    return poisoning_level_;
  }

  // Code getters and setters.

  void SetCode(Handle<Code> code) { code_ = code; }

  void SetWasmCompilationResult(std::unique_ptr<wasm::WasmCompilationResult>);
  std::unique_ptr<wasm::WasmCompilationResult> ReleaseWasmCompilationResult();

  bool has_context() const;
  Context context() const;

  bool has_native_context() const;
  NativeContext native_context() const;

  bool has_global_object() const;
  JSGlobalObject global_object() const;

  // Accessors for the different compilation modes.
  bool IsOptimizing() const { return code_kind() == Code::OPTIMIZED_FUNCTION; }
  bool IsWasm() const { return code_kind() == Code::WASM_FUNCTION; }
  bool IsNotOptimizedFunctionOrWasmFunction() const {
    return code_kind() != Code::OPTIMIZED_FUNCTION &&
           code_kind() != Code::WASM_FUNCTION;
  }
  void SetOptimizingForOsr(BailoutId osr_offset, JavaScriptFrame* osr_frame) {
    DCHECK(IsOptimizing());
    osr_offset_ = osr_offset;
    osr_frame_ = osr_frame;
  }

  void set_persistent_handles(
      std::unique_ptr<PersistentHandles> persistent_handles);

  void ReopenHandlesInNewHandleScope(Isolate* isolate);

  void AbortOptimization(BailoutReason reason);

  void RetryOptimization(BailoutReason reason);

  BailoutReason bailout_reason() const { return bailout_reason_; }

  int optimization_id() const {
    DCHECK(IsOptimizing());
    return optimization_id_;
  }

  unsigned inlined_bytecode_size() const { return inlined_bytecode_size_; }

  void set_inlined_bytecode_size(unsigned size) {
    inlined_bytecode_size_ = size;
  }

  struct InlinedFunctionHolder {
    Handle<SharedFunctionInfo> shared_info;
    Handle<BytecodeArray> bytecode_array;  // Explicit to prevent flushing.
    InliningPosition position;

    InlinedFunctionHolder(Handle<SharedFunctionInfo> inlined_shared_info,
                          Handle<BytecodeArray> inlined_bytecode,
                          SourcePosition pos);

    void RegisterInlinedFunctionId(size_t inlined_function_id) {
      position.inlined_function_id = static_cast<int>(inlined_function_id);
    }
  };

  using InlinedFunctionList = std::vector<InlinedFunctionHolder>;
  InlinedFunctionList& inlined_functions() { return inlined_functions_; }

  // Returns the inlining id for source position tracking.
  int AddInlinedFunction(Handle<SharedFunctionInfo> inlined_function,
                         Handle<BytecodeArray> inlined_bytecode,
                         SourcePosition pos);

  std::unique_ptr<char[]> GetDebugName() const;

  StackFrame::Type GetOutputStackFrameType() const;

  const char* trace_turbo_filename() const {
    return trace_turbo_filename_.get();
  }

  void set_trace_turbo_filename(std::unique_ptr<char[]> filename) {
    trace_turbo_filename_ = std::move(filename);
  }

  TickCounter& tick_counter() { return tick_counter_; }

  BasicBlockProfilerData* profiler_data() const { return profiler_data_; }
  void set_profiler_data(BasicBlockProfilerData* profiler_data) {
    profiler_data_ = profiler_data;
  }

  std::unique_ptr<PersistentHandles> DetachPersistentHandles() {
    return std::move(ph_);
  }

 private:
  void ConfigureFlags();

  void SetFlag(Flag flag) { flags_ |= flag; }
  bool GetFlag(Flag flag) const { return (flags_ & flag) != 0; }

  void SetTracingFlags(bool passes_filter);

  // Compilation flags.
  unsigned flags_ = 0;
  PoisoningMitigationLevel poisoning_level_ =
      PoisoningMitigationLevel::kDontPoison;

  const Code::Kind code_kind_;
  int32_t builtin_index_ = -1;

  // We retain a reference the bytecode array specifically to ensure it doesn't
  // get flushed while we are optimizing the code.
  Handle<BytecodeArray> bytecode_array_;
  Handle<SharedFunctionInfo> shared_info_;
  Handle<JSFunction> closure_;

  // The compiled code.
  Handle<Code> code_;

  // Basic block profiling support.
  BasicBlockProfilerData* profiler_data_ = nullptr;

  // The WebAssembly compilation result, not published in the NativeModule yet.
  std::unique_ptr<wasm::WasmCompilationResult> wasm_compilation_result_;

  // Entry point when compiling for OSR, {BailoutId::None} otherwise.
  BailoutId osr_offset_ = BailoutId::None();

  // The zone from which the compilation pipeline working on this
  // OptimizedCompilationInfo allocates.
  Zone* const zone_;

  std::unique_ptr<PersistentHandles> persistent_handles_;

  BailoutReason bailout_reason_ = BailoutReason::kNoReason;

  InlinedFunctionList inlined_functions_;

  static constexpr int kNoOptimizationId = -1;
  const int optimization_id_;
  unsigned inlined_bytecode_size_ = 0;

  // The current OSR frame for specialization or {nullptr}.
  JavaScriptFrame* osr_frame_ = nullptr;

  Vector<const char> debug_name_;
  std::unique_ptr<char[]> trace_turbo_filename_;

  TickCounter tick_counter_;

  // This PersistentHandles container is owned first by
  // OptimizedCompilationInfo, then by JSHeapBroker, then by LocalHeap (when we
  // go to the background thread), then again by JSHeapBroker (right before
  // returning to the main thread), which gets destroyed when PipelineData gets
  // destroyed when e.g. PipelineCompilationJob gets destroyed. Since it is a
  // member of OptimizedCompilationInfo, we make sure that we have one and only
  // one per compilation job.
  std::unique_ptr<PersistentHandles> ph_;

  DISALLOW_COPY_AND_ASSIGN(OptimizedCompilationInfo);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_OPTIMIZED_COMPILATION_INFO_H_
