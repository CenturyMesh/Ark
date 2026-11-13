#pragma once
namespace ark {
// Diagnostic/reference mode changes only this calling thread. Reference retains
// the pre-074B monolithic solve with no component reuse or warm start.
enum class AssemblySolveMode074B { Incremental, ColdReference };
AssemblySolveMode074B setAssemblySolveMode074B(AssemblySolveMode074B mode) noexcept;
void clearAssemblySolveCache074B() noexcept;
}
