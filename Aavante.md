================================================================================
AGENT GUIDELINES
================================================================================

1) BUILD
--------------------------------------------------------------------------------
Primary:    python build.py        (from repo root, uses Ninja)
Alternative: make compile          (from src/, if using Make)
Clean:      delete Build/ directory and rebuild

Windows: Use Visual Studio Developer Command Prompt for proper environment.

2) TEST
--------------------------------------------------------------------------------
Full suite:     python test.py

Single test:
  Windows:  Build\tests\CommonTest.exe --gtest_filter=SuiteName.TestName
  Linux:    Build/tests/CommonTest --gtest_filter=SuiteName.TestName

Wildcards:  --gtest_filter=SuiteName.*  or  --gtest_filter=*TestName
List tests: Build/tests/CommonTest --gtest_list_tests

Test location: Build/tests/ (binaries auto-discovered by CMake)

3) LINT / FORMAT
--------------------------------------------------------------------------------
Config:  .clang-format (4-space indent, no tabs)
Run:     clang-format -i **/*.cpp **/*.hpp

4) CODE STYLE
--------------------------------------------------------------------------------
Includes:
  - Order: standard → third-party → project headers
  - Use <> for system, "" for local
  - Forward declare when possible

Naming:
  - Classes: PascalCase
  - Members: camelCase (trailing underscore optional)
  - Shared pointers: TypeSp suffix (e.g., TcpConnectionSp)
  - Singletons: getInstance()

Memory:
  - std::shared_ptr for shared ownership
  - std::unique_ptr for exclusive ownership
  - Use aliases: using FooSp = std::shared_ptr<Foo>;

Error Handling:
  - Use return codes, std::optional, or std::expected
  - Log meaningful error messages
  - No silent failures

Threading:
  - std::mutex with std::lock_guard
  - std::atomic for simple flags
  - Call stop() before destruction

Logging:
  - Use Log.h macros: log_debug, log_info, log_warning, log_error

Comments:
  - Explain non-obvious logic only
  - Document public API ownership/lifecycle/errors

Testing:
  - Location: src/tests/
  - Framework: Google Test
  - Use mocks for isolation (see MockSocket.h)

Security:
  - Validate inputs
  - Check buffer bounds
  - Guard against null dereferences

5) AGENT RULES
--------------------------------------------------------------------------------
Copilot:
  - Follow existing architecture and naming
  - No insecure/unsafe constructs
  - Search repo for conventions before changes
  - Include minimal focused tests

Cursor:
  - No .cursorrules file present
  - Add rules here if created

6) WORKFLOW
--------------------------------------------------------------------------------
Before changes:  Ensure tests compile in target environment
After changes:   python build.py → python test.py → open PR
PR content:      Include justification (why, not just what)

Quick verify:
  1. python build.py
  2. Build/tests/CommonTest --gtest_filter=TestName
  3. python test.py

================================================================================
END
================================================================================
