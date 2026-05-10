import os
import subprocess
import sys


def run_unit_tests():
    build_tests_path = os.path.join("Build", "tests")
    original_dir = os.getcwd()

    if os.path.exists(build_tests_path):
        os.chdir(build_tests_path)
    else:
        print(f"Error: Directory {build_tests_path} not found")
        return False

    result = subprocess.run(
        ["./CommonTest" if os.name != 'nt' else "CommonTest.exe"],
        shell=False,
        capture_output=False,
    )
    os.chdir(original_dir)
    return result.returncode == 0


def run_integration_test():
    print("\n--- Running integration test ---")
    result = subprocess.run(
        [sys.executable, "test/integration_test.py"],
        shell=False,
        capture_output=False,
    )
    return result.returncode == 0


def main():
    unit_ok = run_unit_tests()
    print(f"Unit tests {'PASSED' if unit_ok else 'FAILED'}")

    int_ok = run_integration_test()
    print(f"Integration test {'PASSED' if int_ok else 'FAILED'}")

    if unit_ok and int_ok:
        print("\nAll tests passed.")
        sys.exit(0)
    else:
        print("\nSome tests failed.")
        sys.exit(1)


if __name__ == "__main__":
    main()
