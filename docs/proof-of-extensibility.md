# Extensibility-Completeness of Rig's 8-Primitive Extension System

## Overview

Rig exposes 8 primitives to Lua extensions. This document proves - under explicitly stated architectural assumptions - that these 8 are sufficient for unbounded extensibility and that none can be removed without losing capability or violating operational requirements.

The proof has a formally rigorous core (Theorems 1 and 2) that depends on a clearly defined abstract model, plus a practical extension (the 8-primitive basis) justified by operational constraints. Assumptions are stated as hypotheses, not hidden.

---

## 1. Abstract Model of the Host

We model the host agent as a **Turing machine with a fixed set of oracles** - abstract I/O interfaces with well-defined signatures and opaque implementations. The host's behavior is any sequence of internal computation steps interleaved with oracle calls. The host has no I/O capability outside its oracles.

### 1.1 Oracle Definitions

| Oracle | Signature | Semantics |
|--------|-----------|-----------|
| **OS** | `exec(cmd) → result` | Create a child process, wait for termination, return output and exit code. |
| **LLM** | `complete(prompt) → response` | Send a prompt to the internal LLM provider stack, return the full response. |
| **UserOut** | `print(text)` | Append text to the user's display. |
| **UserIn** | `input(prompt) → text` | Display prompt, block until user types a line, return it. |
| **ReadState** | `get_state() → snapshot` | Return the current internal state of the agent. |
| **WriteState** | `set_state(update)` | Apply an update to the agent's internal state. |

### 1.2 The Independence Hypothesis

**Hypothesis I (Oracle Independence).** The host is constructed such that no oracle can be simulated by a program using only the other five. Formally: for each oracle `O_i`, there exists at least one effect achievable through `O_i` that no program with access to `{O_1, ..., O_6} \ {O_i}` can produce.

This is an **architectural invariant** that Rig enforces by design, not a universal property of all AI agents. A different agent could violate it (e.g., by exposing API keys as environment variables, collapsing LLM into OS). The theorems below hold for any host that satisfies Hypothesis I.

### 1.3 How Rig Enforces Hypothesis I

| Pair | Isolation mechanism |
|------|-------------------|
| LLM ≠ OS | API keys, provider protocols, and auth tokens are internal to the host process. Not exposed as environment variables, files, or arguments to child processes. `exec("curl ...")` cannot authenticate. |
| UserOut ≠ OS | Terminal is in raw/alt-screen mode, owned by the host. Child processes spawned via `exec` get piped stdout, not access to the host's TUI renderer. |
| UserIn ≠ OS | Host's key parser consumes stdin in raw mode. Extensions and child processes cannot `read(STDIN)` - the host is already reading it. |
| ReadState ≠ OS | Agent internals (message list, tool registry, hook chain, event bus) exist only in host process memory. No file, socket, or shared memory exposes them. |
| WriteState ≠ ReadState | Observing state does not grant mutation. Different operation, different permission boundary. |
| ReadState ≠ WriteState | Mutating state does not reveal what the state currently is without a read. |

---

## 2. Extension System Definition

An extension system is a pair (L, P) where:

- L is a **Turing-complete** programming language (Lua 5.4).
- P is a set of **primitive functions**, each mapping to exactly one host oracle.

### 2.1 Effect Set

The **effect set** E(H) of host H is the set of all observable I/O behaviors H can produce: every possible sequence of oracle calls interleaved with internal computation.

### 2.2 Extensibility-Completeness

An extension system (L, P) is **extensibility-complete** with respect to host H if: for every effect e in E(H), there exists a program p in L using only primitives in P that produces e.

---

## 3. Theorem 1: 6 Primitives Suffice

**Theorem 1 (Extensibility-Completeness).**  
Let H be a host whose behavior is a computable function using oracles {OS, LLM, UserOut, UserIn, ReadState, WriteState}. Let (L, P₆) be an extension system where L is Turing-complete and P₆ = {exec, completion, print, input, get_state, set_state}, with each primitive calling the corresponding oracle. Then (L, P₆) is extensibility-complete with respect to H.

**Proof.**  
Every behavior of H is a computable function interleaved with oracle calls (by the model definition in §1). Since L is Turing-complete, it can compute any computable function. Each oracle call in H's behavior has a corresponding primitive in P₆. An extension program that simulates H's computation and replaces each oracle call with the corresponding primitive produces an identical trace of oracle interactions. Therefore every effect in E(H) is realizable by some program in (L, P₆). ∎

---

## 4. Theorem 2: No Proper Subset Suffices

**Theorem 2 (Minimality of the Oracle Set).**  
Assuming Hypothesis I (oracle independence), no proper subset of P₆ is extensibility-complete with respect to H.

**Proof.**  
By Hypothesis I, for each oracle O_i there exists an effect e_i achievable only through O_i. If we remove the primitive corresponding to O_i from P₆, no program in L can produce e_i (since L's only access to I/O is through the remaining primitives, which map to the other oracles, and by Hypothesis I those cannot simulate O_i). Therefore the reduced set is not extensibility-complete. Since this holds for each of the 6 primitives, no proper subset of P₆ is extensibility-complete. ∎

---

## 5. The Practical Minimal Basis: 8 Primitives

Theorems 1 and 2 establish that 6 primitives are sufficient and minimal for extensibility-completeness. However, the ReadState oracle admits two distinct access patterns that the 6-primitive model conflates:

- **Flow observation**: reacting to events as they occur (tool calls starting, messages arriving, lifecycle transitions)
- **State snapshots**: querying the current value of internal state at a point in time

A single `get_state()` primitive can serve both: poll repeatedly for snapshots (covering state queries) or register a callback that fires on changes (covering flow). But these simulations have operational deficiencies:

### 5.1 Why `get` Cannot Replace `hook`

Polling `get_state()` to detect changes requires busy-waiting: `while true do check_state(); sleep(dt) end`. This is:
- **Unbounded in CPU cost** - fires continuously even when nothing changes
- **Lossy** - events between polls are missed (tool call that starts and ends within one poll interval)
- **Latency-bound** - reaction time equals poll interval

A push-based `hook(event, fn)` primitive has O(0) cost when idle and O(1) latency on event. The two patterns are operationally non-equivalent.

### 5.2 Why `hook` Cannot Replace `get`

**Hypothesis II (Cold-Start Extensibility).** Extensions may be loaded at any point during the host's lifetime, not only at startup.

Under Hypothesis II, an extension loaded mid-session cannot reconstruct the current state from future events alone. A `hook` registered after 50 messages have been exchanged will never see those 50 messages. The shadow-copy simulation is:
- **Lossy** - misses all state set before hook registration
- **Fragile** - any missed event permanently corrupts the shadow

A `get(ns, key)` primitive returns a faithful snapshot regardless of when the extension was loaded.

### 5.3 Why `unhook` Cannot Be Simulated Cleanly

**Hypothesis III (Resource Cleanliness).** Extensions must be removable without residual effects on system performance.

Without `unhook`, the only way to disable a hook is to set a flag that the callback checks:

```lua
local disabled = false
hook("tool_call", function(data)
    if disabled then return end
    -- actual logic
end)
-- later:
disabled = true - -- hook still fires, checks flag, returns
```

The hook remains in the chain permanently. After N disabled hooks, every event dispatches through N no-op callbacks. Under Hypothesis III, this violates resource cleanliness. A dedicated `unhook(handle)` removes the callback from the chain entirely: O(1) removal, zero residual cost.

### 5.4 The 8-Primitive Basis

Splitting ReadState into three primitives (flow observation, flow removal, state snapshot) and keeping everything else from the 6-primitive basis:

| # | Primitive | Oracle | Access pattern |
|---|-----------|--------|---------------|
| 1 | `rig.exec(cmd, opts?)` | OS | Sync/async process execution with cancel |
| 2 | `rig.completion(params, opts?)` | LLM | Inference with optional streaming callback |
| 3 | `rig.print(text, opts?)` | UserOut | Styled TUI output |
| 4 | `rig.input(prompt)` | UserIn | Blocking keyboard input |
| 5 | `rig.hook(event, fn) → handle` | ReadState | Push-based event observation |
| 6 | `rig.unhook(handle)` | ReadState | Event observer removal |
| 7 | `rig.get(ns, key?)` | ReadState | Pull-based state snapshot |
| 8 | `rig.set(ns, key, value)` | WriteState | Universal state mutation |

**Theorem 3 (Practical Minimality).**  
Under Hypotheses I, II, and III, the 8-primitive set P₈ is extensibility-complete (by Theorem 1, since P₈ ⊇ P₆ in capability) and practically minimal: removing any primitive either loses extensibility-completeness (primitives 1-4, 8) or violates an operational hypothesis (primitives 5-7).

---

## 6. `set` as Universal Mutator

The WriteState oracle is served by a single polymorphic primitive `set(namespace, key, value)` where `nil` = delete:

```lua
rig.set("tools", "screenshot", { description = "...", params = {...}, run = fn })
rig.set("tools", "screenshot", nil)                    -- remove tool
rig.set("commands", "deploy", fn)                      -- add slash command
rig.set("messages", "append", { role = "system", content = "..." })
rig.set("config", "model", "claude-sonnet-4-20250514")
rig.set("config", "thinking", "high")
```

The namespace determines validation rules enforced by the C bridge. This single primitive replaces what would otherwise be ~15 specialized mutation functions, without losing type safety (validation is per-namespace in C).

---

## 7. Grounding: Extension Patterns

Every extension pattern maps to a subset of the 8 primitives:

| Pattern | Primitives used |
|---------|----------------|
| System prompt injection | `hook` + `set` |
| Custom LLM tools | `set` (tools namespace) |
| Slash commands | `set` (commands namespace) |
| Sub-agents | `completion` |
| Plan-then-execute | `completion` + `input` + `print` |
| Permission gates | `hook` + `input` |
| RAG / long-term memory | `exec` + `hook` + `completion` |
| Custom TUI rendering | `hook` + `print` |
| MCP bridge | `exec` + `set` |
| CI/CD pipelines | `exec` + `print` + `input` + `completion` |

Every primitive appears in at least one pattern. No pattern requires a primitive outside P₈.

---

## 8. Future-Proofing

**Corollary (Extensibility Under New Oracles).**  
If the host gains a new oracle O_new (GPU compute, GUI, hardware sensors, inter-agent communication) that is independent of the existing 6, extensibility-completeness is restored by adding exactly one primitive that provides access to O_new. The existing 8 primitives remain valid and unchanged.

This follows directly from Theorem 1: completeness requires one primitive per oracle. New oracle = new primitive. No redesign.

---

## 9. Assumptions and Limitations

This proof is honest about what it assumes and where it does not apply:

1. **Hypothesis I is enforced, not fundamental.** Oracle independence is an architectural property of Rig, not a law of computation. An agent that exposes API keys as environment variables collapses LLM into OS, invalidating the 6-oracle decomposition. The theorems apply to agents that enforce the isolation described in §1.3.

2. **The oracle set is not proven exhaustive.** We identified 6 oracles for the current host. A future host with GPU compute, GUI subsystems, or hardware sensors would have additional oracles. The architecture handles this gracefully (§8) but we do not claim the current set covers all possible agents.

3. **Primitive semantics are specified informally.** A fully formal proof would require a specification of each primitive's exact behavior (what events `hook` delivers, what namespaces `set` accepts, what `exec` does with signals). We specify semantics by contract in the implementation, not in this document.

4. **Completeness is about capability, not ergonomics.** An extension *can* build any behavior, but some behaviors may require complex Lua code. The proof says nothing about how *easy* it is to build a given extension, only that it's *possible*.

---

## 10. Summary

| Property | Value | Justification |
|----------|-------|---------------|
| Oracles | 6 | Mutually independent under Hypothesis I (§1.2) |
| Theoretical minimal primitives | 6 | Theorem 2 (§4) |
| Practical minimal primitives | 8 | Theorem 3, under Hypotheses I + II + III (§5.4) |
| Extensibility | Unbounded | Theorem 1: Turing-complete + complete oracle access (§3) |
| Independence basis | Architectural | Enforced by Rig's isolation design (§1.3) |
| Future-proof | 1 new primitive per new oracle | Corollary (§8) |

The extension system (Lua, P₈) is extensibility-complete: any computable behavior the host can produce is realizable by a Lua extension using 8 primitives. This is the smallest set that achieves completeness while satisfying cold-start extensibility and resource cleanliness. The proof rests on architectural hypotheses that Rig enforces by design, and is transparent about where those hypotheses are design choices rather than universal constraints.
