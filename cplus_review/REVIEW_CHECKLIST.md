# Friday Review Checklist

Run against **every** artifact, every week. These are the 11 Code Review Rules from CLAUDE.md.

The mechanical half of this (const correctness, Rule of 0/3/5, `noexcept`, naming, raw `new`/`delete`) is already enforced by `.clang-tidy` and `-Werror` — so `./check.sh --tidy` must be green *before* the review starts. The review is for the half a linter can't see.

---

## 1. Correctness
- [ ] Does it do what it claims for the *typical* case?
- [ ] Edge cases: empty · single element · full · self-assignment · self-move
- [ ] Integer overflow · off-by-one · signed/unsigned mixing

## 2. Modern C++
- [ ] C++20/23 idioms, not C-with-classes
- [ ] Compiles clean under **both** `-std=c++20` and `-std=c++23`
- [ ] Would a C++23 feature make this simpler? (and does the C++20 path still work?)

## 3. Readability
- [ ] Can a reader understand it without the author present?
- [ ] Is the *invariant* stated somewhere the next reader will find it?

## 4. Maintainability
- [ ] One reason to change (SRP)
- [ ] No hidden coupling to global state

## 5. Exception Safety
- [ ] Which guarantee does each function give — **basic**, **strong**, or **nothrow**? State it.
- [ ] Is the strong guarantee actually achieved (copy-and-swap), or merely claimed?
- [ ] Can it leak or corrupt an invariant if an exception fires *mid-operation*?

## 6. Memory Safety
- [ ] Clean under ASAN + UBSAN (+ Valgrind)
- [ ] No dangling references returned; no use-after-move
- [ ] Ownership is *expressed in the type*, not in a comment

## 7. Thread Safety
- [ ] Clean under TSAN (Phase 5+)
- [ ] What is the documented thread-safety contract? ("not thread-safe" is a valid answer — an *undocumented* one isn't)
- [ ] Any lock ordering that could deadlock?

## 8. Performance
- [ ] Big-O stated — and is it the *right* complexity for the use case?
- [ ] Measured, or guessed? (guessed = not done)
- [ ] Unnecessary copies? Verify with `Probe`, don't assume.
- [ ] Cache behaviour considered where it matters

## 9. API Design
- [ ] Hard to misuse? Is the wrong call even *expressible*?
- [ ] `explicit` where it should be
- [ ] Does it return something that can dangle (`string_view`, `span`, iterator)? Is that documented?

## 10. Naming
- [ ] Expressive; reveals intent, not implementation

## 11. Testability
- [ ] Tested, and are the tests testing *behaviour* rather than restating the implementation?
- [ ] Are the failure paths tested, or only the happy path?

---

## Then, always:

> ## "What would a Staff Engineer improve?"

And the interview follow-ups:

- Why? How? What are the trade-offs?
- What happens internally?
- How is it implemented in libstdc++?
- **When should I NOT use this?**
- What would change in production?
