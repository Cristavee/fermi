---
  name: Fermi codegen bugs — key fixes
  description: Root causes and fixes for the four main Fermi compiler bugs found and repaired in June 2026.
  ---

  ## Bug 1 — io::println undefined
  **Root cause**: `emit_ns_call` in `src/fecodegen/codegen.c` mangled `io::println` → `io__println` and emitted a FIR_CALL, but this function was never declared or defined anywhere.
  **Fix**: Added `emit_io_call()` before `emit_ns_call()`. Routes io namespace to inline printf/puts calls:
  - str arg → `puts(val)` for println, `printf("%s", val)` for print
  - i64 arg → `printf("%ld\n", val)`
  - i32 arg → `printf("%d\n", val)`
  - float/double → promoted to double, `printf("%g\n", val)`
  **Why**: printf/puts are in libc_decls (auto-declared), FIR_CONST_STR emits GEP to string global.

  ## Bug 2 — let type inference defaults to i32
  **Root cause**: `NODE_VAR_DECL` in codegen.c: `typenode_to_llvm(...) : "i32"` — always used i32 when no explicit type.
  **Fix**: When no explicit type AND there is an init expression, emit init first (which sets node->ty), then use `ast_type_str` as the inferred type. Early break after inferred path to avoid double-emit.
  **Why**: After `emit_expr(call_node)`, `call_node->ty` is correctly set (e.g. TY_LONG for i64 return), so `ast_type_str` returns "i64".

  ## Bug 3 — Arg coercion for function calls
  **Root cause**: In the NODE_CALL dispatch, args were emitted with `ast_type_str` types (e.g. i32 for int literals), but callee might expect i64. LLVM IR is strongly typed.
  **Fix**: After routing/ret_ty lookup, before building FIR_CALL: iterate `fn->params` (FirParam: name, type, next) and `coerce_to()` each arg to match the param's declared type.

  ## Bug 4 — Optional parens for if/while
  **Root cause**: `parse_if` and `parse_while` in `src/feparser/parser.c` used `int has_paren=chk(p,TOK_LPAREN); if(has_paren) adv(p);`.
  **Fix**: Replaced with `expect(p,TOK_LPAREN,"expected '(' after 'if/while'")`.
  **Note**: match already required parens; for already required parens; do-while left optional.

  ## Optimizer behavior
  The peephole optimizer (`src/feopt/opt.c`) converts `FIR_SEXT` of integer constants to `add i64 const, 0`. This is intentional — LLVM constant-folds it. Valid LLVM IR, not a bug.

  ## Key architecture notes
  - `ast_type_str(cg, node)` reads `node->ty` (TypeKind enum) → uses `typekind_to_llvm`
  - `typekind_to_llvm`: TY_LONG→"i64", TY_INT→"i32", TY_STR→"ptr", etc.
  - NODE_IDENT emit sets `n->ty` from `VarEntry.type`; NODE_CALL sets `n->ty` from ret_ty
  - `FirParam` struct: `{char *name; char *type; FirParam *next;}`
  - `FIR_CONST_STR` in llvm_emit: emits GEP to interned string global
  - libc_decls includes: printf, puts, fprintf, fputs, fclose, strlen, etc. (auto-emitted)
  - Changed files: `src/fecodegen/codegen.c`, `src/feparser/parser.c`
  