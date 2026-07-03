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


def run_integration_test(extra_args=None):
    print("\n--- Running integration test ---")
    cmd = [sys.executable, "test/integration_test.py"]
    if extra_args:
        cmd.extend(extra_args)
    result = subprocess.run(cmd, shell=False, capture_output=False)
    return result.returncode == 0


def main():
    # Separate our own args from args to pass through to the integration test.
    # Usage: python test.py [--unit-only] [--int-only] [--int-args ...]
    our_args = []
    int_args = []
    in_int_args = False
    for a in sys.argv[1:]:
        if a == "--int-args":
            in_int_args = True
            continue
        if in_int_args:
            int_args.append(a)
        else:
            our_args.append(a)

    run_unit = "--int-only" not in our_args
    run_int = "--unit-only" not in our_args

    unit_ok = True
    int_ok = True

    if run_unit:
        unit_ok = run_unit_tests()
        print(f"Unit tests {'PASSED' if unit_ok else 'FAILED'}")

    if run_int:
        int_ok = run_integration_test(int_args if int_args else None)
        print(f"Integration test {'PASSED' if int_ok else 'FAILED'}")

    if unit_ok and int_ok:
        print("\nAll tests passed.")
        sys.exit(0)
    else:
        print("\nSome tests failed.")
        sys.exit(1)


if __name__ == "__main__":
    main()
